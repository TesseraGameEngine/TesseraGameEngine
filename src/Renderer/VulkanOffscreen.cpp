#include "VulkanOffscreen.h"
#include "VulkanGpuProfiler.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <tuple>
#include <cmath>

static void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageTiling tiling,
                        VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                        VkImage& image, VkDeviceMemory& imageMemory) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = ctx.FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

static VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t baseMip = 0) {
    auto& ctx = VulkanContext::Get();
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = baseMip;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(ctx.GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view!");
    }
    return imageView;
}

// Puts every mip of a colour image into SHADER_READ_ONLY_OPTIMAL, so it is valid to sample
// even before anything has rendered into it.
static void TransitionToShaderRead(VkImage image, uint32_t mipLevels, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    ctx.EndSingleTimeCommands(cmd);
}

static std::vector<char> ReadSpirv(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to open shader file: ") + path);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

static VkShaderModule CreateShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(VulkanContext::Get().GetDevice(), &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process shader module!");
    }
    return module;
}

void VulkanOffscreen::Init(uint32_t width, uint32_t height) {
    m_width = (width > 0) ? width : 800;
    m_height = (height > 0) ? height : 600;

    m_sceneRenderPass = CreateScenePassHalf(false);
    m_sceneLoadRenderPass = CreateScenePassHalf(true);
    CreatePrepassRenderPass();
    m_bloomDownRenderPass = CreatePostRenderPass(kHdrFormat, false);
    m_bloomUpRenderPass = CreatePostRenderPass(kHdrFormat, true);
    m_compositeRenderPass = CreatePostRenderPass(kDisplayFormat, false);
    CreateOverlayRenderPass();
    m_aoRenderPass = CreatePostRenderPass(kAOFormat, false);
    CreateSampler();
    CreatePostPipelines();
    CreateResources();
    CreateFramebuffers();
    CreatePostDescriptorSets();
}

void VulkanOffscreen::Cleanup() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    CleanupResources();

    if (m_postDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_postDescriptorPool, nullptr); m_postDescriptorPool = VK_NULL_HANDLE; }
    if (m_bloomDownPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_bloomDownPipeline, nullptr); m_bloomDownPipeline = VK_NULL_HANDLE; }
    if (m_bloomUpPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_bloomUpPipeline, nullptr); m_bloomUpPipeline = VK_NULL_HANDLE; }
    if (m_compositePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_compositePipeline, nullptr); m_compositePipeline = VK_NULL_HANDLE; }
    if (m_ssaoPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_ssaoPipeline, nullptr); m_ssaoPipeline = VK_NULL_HANDLE; }
    if (m_ssaoBlurPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_ssaoBlurPipeline, nullptr); m_ssaoBlurPipeline = VK_NULL_HANDLE; }
    if (m_fxaaPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_fxaaPipeline, nullptr); m_fxaaPipeline = VK_NULL_HANDLE; }
    if (m_taaPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_taaPipeline, nullptr); m_taaPipeline = VK_NULL_HANDLE; }
    for (VkPipeline* pipeline : { &m_ssrPipeline, &m_dofPipeline, &m_motionBlurPipeline, &m_exposurePipeline }) {
        if (*pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, *pipeline, nullptr); *pipeline = VK_NULL_HANDLE; }
    }
    if (m_pointSampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_pointSampler, nullptr); m_pointSampler = VK_NULL_HANDLE; }
    if (m_postPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_postPipelineLayout, nullptr); m_postPipelineLayout = VK_NULL_HANDLE; }
    if (m_postSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_postSetLayout, nullptr); m_postSetLayout = VK_NULL_HANDLE; }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    for (VkRenderPass* rp : { &m_sceneRenderPass, &m_sceneLoadRenderPass, &m_prepassRenderPass, &m_bloomDownRenderPass, &m_bloomUpRenderPass,
                              &m_compositeRenderPass, &m_overlayRenderPass, &m_aoRenderPass }) {
        if (*rp != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, *rp, nullptr);
            *rp = VK_NULL_HANDLE;
        }
    }
}

void VulkanOffscreen::CleanupResources() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    if (m_descriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_descriptorSet);
        m_descriptorSet = VK_NULL_HANDLE;
    }
    if (m_postDescriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device, m_postDescriptorPool, 0);
    }
    m_bloomDownSets.clear();
    m_bloomUpSets.clear();
    m_ssaoSet = VK_NULL_HANDLE;
    m_ssaoBlurSet = VK_NULL_HANDLE;
    m_fxaaSet = VK_NULL_HANDLE;
    m_taaSet = VK_NULL_HANDLE;
    m_ssrSet = VK_NULL_HANDLE;
    m_dofSet = VK_NULL_HANDLE;
    m_motionBlurSet = VK_NULL_HANDLE;
    m_compositeSets[0] = m_compositeSets[1] = VK_NULL_HANDLE;
    m_exposureSets[0] = m_exposureSets[1] = VK_NULL_HANDLE;

    auto destroyFramebuffer = [&](VkFramebuffer& fb) {
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, fb, nullptr); fb = VK_NULL_HANDLE; }
    };
    auto destroyView = [&](VkImageView& view) {
        if (view != VK_NULL_HANDLE) { vkDestroyImageView(device, view, nullptr); view = VK_NULL_HANDLE; }
    };
    auto destroyImage = [&](VkImage& image, VkDeviceMemory& memory) {
        if (image != VK_NULL_HANDLE) { vkDestroyImage(device, image, nullptr); image = VK_NULL_HANDLE; }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(device, memory, nullptr); memory = VK_NULL_HANDLE; }
    };

    destroyFramebuffer(m_sceneFramebuffer);
    destroyFramebuffer(m_compositeFramebuffer);
    destroyFramebuffer(m_overlayFramebuffer);
    destroyFramebuffer(m_prepassFramebuffer);
    destroyFramebuffer(m_taaFramebuffer);
    destroyFramebuffer(m_ssrFramebuffer);
    destroyView(m_ssrImageView);
    destroyImage(m_ssrImage, m_ssrImageMemory);
    destroyFramebuffer(m_dofFramebuffer);
    destroyFramebuffer(m_motionBlurFramebuffer);
    for (int i = 0; i < 2; ++i) {
        destroyFramebuffer(m_exposureFramebuffers[i]);
        destroyView(m_exposureImageViews[i]);
        destroyImage(m_exposureImages[i], m_exposureImageMemory[i]);
    }
    destroyView(m_dofImageView);
    destroyView(m_motionBlurImageView);
    destroyView(m_velocityImageView);
    destroyImage(m_dofImage, m_dofImageMemory);
    destroyImage(m_motionBlurImage, m_motionBlurImageMemory);
    destroyImage(m_velocityImage, m_velocityImageMemory);
    destroyView(m_taaResolveImageView);
    destroyView(m_taaHistoryImageView);
    destroyImage(m_taaResolveImage, m_taaResolveImageMemory);
    destroyImage(m_taaHistoryImage, m_taaHistoryImageMemory);
    destroyView(m_sceneColorImageView);
    destroyImage(m_sceneColorImage, m_sceneColorImageMemory);
    destroyFramebuffer(m_compositeToLdrFramebuffer);
    destroyView(m_ldrImageView);
    destroyImage(m_ldrImage, m_ldrImageMemory);
    destroyFramebuffer(m_aoRawFramebuffer);
    destroyFramebuffer(m_aoFramebuffer);
    destroyView(m_normalImageView);
    destroyView(m_prepassDepthImageView);
    destroyView(m_aoRawImageView);
    destroyView(m_aoImageView);
    destroyImage(m_normalImage, m_normalImageMemory);
    destroyImage(m_prepassDepthImage, m_prepassDepthImageMemory);
    destroyImage(m_aoRawImage, m_aoRawImageMemory);
    destroyImage(m_aoImage, m_aoImageMemory);
    for (auto& fb : m_bloomFramebuffers) destroyFramebuffer(fb);
    m_bloomFramebuffers.clear();

    destroyView(m_hdrImageView);
    destroyView(m_depthImageView);
    destroyView(m_colorImageView);
    for (auto& view : m_bloomViews) destroyView(view);
    m_bloomViews.clear();
    m_bloomExtents.clear();

    destroyImage(m_hdrImage, m_hdrImageMemory);
    destroyImage(m_depthImage, m_depthImageMemory);
    destroyImage(m_colorImage, m_colorImageMemory);
    destroyImage(m_bloomImage, m_bloomImageMemory);
}

// Display image (loaded, stays sampleable for ImGui) + scene depth (read-only depth test)
void VulkanOffscreen::CreateOverlayRenderPass() {
    auto& ctx = VulkanContext::Get();
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = kDisplayFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = ctx.FindDepthFormat();
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    // in: the composite / FXAA writes and TAA's depth reads; out: ImGui sampling the image
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    if (vkCreateRenderPass(ctx.GetDevice(), &info, nullptr, &m_overlayRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create overlay render pass!");
    }
}

void VulkanOffscreen::BeginOverlayPass(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = m_overlayRenderPass;
    info.framebuffer = m_overlayFramebuffer;
    info.renderArea.extent = { m_width, m_height };
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, { m_width, m_height } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanOffscreen::EndOverlayPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanOffscreen::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == m_width && height == m_height)) {
        return;
    }
    auto& ctx = VulkanContext::Get();
    vkDeviceWaitIdle(ctx.GetDevice());

    m_width = width;
    m_height = height;
    m_taaHistoryValid = false; // the old history no longer lines up

    CleanupResources();
    CreateResources();
    CreateFramebuffers();
    CreatePostDescriptorSets();
}

// The scene pass is split in two around a copy of the opaque image (for refraction):
//   first half:  clears, colour ends in TRANSFER_SRC for the copy, depth is kept
//   second half: loads colour + depth, colour ends sampleable for bloom / composite / mirrors
VkRenderPass VulkanOffscreen::CreateScenePassHalf(bool secondHalf) {
    auto& ctx = VulkanContext::Get();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kHdrFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = secondHalf ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = secondHalf ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = secondHalf ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = ctx.FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = secondHalf ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = secondHalf ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    // After the second half the depth is sampled by TAA reprojection
    depthAttachment.finalLayout = secondHalf ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthAttachmentRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Both halves must have identical dependencies to stay render-pass compatible (the scene
    // pipelines and framebuffer are shared), so each dependency covers both halves' needs:
    //  - in:  last frame's sampling (first half) / the refraction copy and kept depth (second half)
    //  - out: the refraction copy and kept depth (first half) / bloom, composite, mirrors (second half)
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_SHADER_READ_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    if (vkCreateRenderPass(ctx.GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen render pass!");
    }
    return renderPass;
}

// View-space normals + depth, both left sampleable for the SSAO pass
void VulkanOffscreen::CreatePrepassRenderPass() {
    auto& ctx = VulkanContext::Get();

    std::array<VkAttachmentDescription, 3> attachments{};
    attachments[0].format = kHdrFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format = ctx.FindDepthFormat();
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Motion since last frame (TAA, motion blur)
    attachments[2] = attachments[0];

    VkAttachmentReference colorRefs[2] = {
        { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
    };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2;
    subpass.pColorAttachments = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(ctx.GetDevice(), &renderPassInfo, nullptr, &m_prepassRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO prepass render pass!");
    }
}

// Single colour attachment, left in SHADER_READ_ONLY_OPTIMAL for the next pass to sample.
// loadExisting keeps the current contents (for additive bloom upsampling).
VkRenderPass VulkanOffscreen::CreatePostRenderPass(VkFormat format, bool loadExisting) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = loadExisting ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = loadExisting ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Earlier passes' writes must be visible to this pass's reads, and earlier reads of
    // the target must finish before it is overwritten.
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    if (vkCreateRenderPass(VulkanContext::Get().GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process render pass!");
    }
    return renderPass;
}

void VulkanOffscreen::CreateSampler() {
    auto& ctx = VulkanContext::Get();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(ctx.GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen sampler!");
    }

    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(ctx.GetDevice(), &samplerInfo, nullptr, &m_pointSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen point sampler!");
    }
}

void VulkanOffscreen::CreatePostPipelines() {
    VkDevice device = VulkanContext::Get().GetDevice();

    // Binding 0: source image; binding 1: second source (bloom, composite only)
    // Binding 0: source image; bindings 1-3: extra sources (bloom, history, depth, velocity, exposure)
    VkDescriptorSetLayoutBinding bindings[kPostBindings]{};
    for (uint32_t b = 0; b < kPostBindings; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kPostBindings;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_postSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process descriptor set layout!");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PostPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_postSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_postPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process pipeline layout!");
    }

    m_bloomDownPipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/bloom_down_frag.spv", false);
    m_bloomUpPipeline = CreatePostPipeline(m_bloomUpRenderPass, "shaders/bloom_up_frag.spv", true);
    m_compositePipeline = CreatePostPipeline(m_compositeRenderPass, "shaders/composite_frag.spv", false);
    m_ssaoPipeline = CreatePostPipeline(m_aoRenderPass, "shaders/ssao_frag.spv", false);
    m_ssaoBlurPipeline = CreatePostPipeline(m_aoRenderPass, "shaders/ssao_blur_frag.spv", false);
    m_fxaaPipeline = CreatePostPipeline(m_compositeRenderPass, "shaders/fxaa_frag.spv", false);
    m_taaPipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/taa_frag.spv", false);
    m_ssrPipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/ssr_frag.spv", false);
    m_dofPipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/dof_frag.spv", false);
    m_motionBlurPipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/motionblur_frag.spv", false);
    m_exposurePipeline = CreatePostPipeline(m_bloomDownRenderPass, "shaders/exposure_frag.spv", false);

    // Enough sets for every bloom level (down + up), the composite, the two SSAO passes and FXAA
    // bloom (down + up), 2 composites, SSAO x2, FXAA, SSR, TAA, DOF, motion blur, 2 exposure
    const uint32_t maxSets = kMaxBloomLevels * 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1 + 2;
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxSets * kPostBindings;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxSets;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_postDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process descriptor pool!");
    }
}

VkPipeline VulkanOffscreen::CreatePostPipeline(VkRenderPass renderPass, const char* fragSpvPath, bool additiveBlend) {
    VkDevice device = VulkanContext::Get().GetDevice();

    VkShaderModule vertModule = CreateShaderModule(ReadSpirv("shaders/post_vert.spv"));
    VkShaderModule fragModule = CreateShaderModule(ReadSpirv(fragSpvPath));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = additiveBlend ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_postPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error(std::string("Failed to create post-process pipeline: ") + fragSpvPath);
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    return pipeline;
}

void VulkanOffscreen::CreateResources() {
    auto& ctx = VulkanContext::Get();

    // 1. HDR scene colour
    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_hdrImage, m_hdrImageMemory);
    m_hdrImageView = CreateImageView(m_hdrImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_hdrImage, 1); // sampled by mirrors before this target's first frame

    // TAA: this frame's resolved image (read by bloom / composite) and the previous frame's
    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_taaResolveImage, m_taaResolveImageMemory);
    m_taaResolveImageView = CreateImageView(m_taaResolveImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_taaResolveImage, 1);
    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_taaHistoryImage, m_taaHistoryImageMemory);
    m_taaHistoryImageView = CreateImageView(m_taaHistoryImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_taaHistoryImage, 1);
    m_taaHistoryValid = false;

    // Depth of field and motion blur outputs (HDR), then 2 alternating 1x1 auto exposure values
    for (auto [image, memory, view] : { std::tie(m_ssrImage, m_ssrImageMemory, m_ssrImageView),
                                        std::tie(m_dofImage, m_dofImageMemory, m_dofImageView),
                                        std::tie(m_motionBlurImage, m_motionBlurImageMemory, m_motionBlurImageView) }) {
        CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
        view = CreateImageView(image, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionToShaderRead(image, 1);
    }
    for (int i = 0; i < 2; ++i) {
        CreateImage(1, 1, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_exposureImages[i], m_exposureImageMemory[i]);
        m_exposureImageViews[i] = CreateImageView(m_exposureImages[i], kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionToShaderRead(m_exposureImages[i], 1);
    }
    m_exposureValid = false;

    // Copy of the opaque scene that transparent surfaces refract
    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_sceneColorImage, m_sceneColorImageMemory);
    m_sceneColorImageView = CreateImageView(m_sceneColorImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_sceneColorImage, 1);

    // 2. Depth buffer
    VkFormat depthFormat = ctx.FindDepthFormat();
    CreateImage(m_width, m_height, 1, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthImageMemory);
    m_depthImageView = CreateImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // 3. Bloom chain, starting at half resolution
    uint32_t bloomWidth = std::max(1u, m_width / 2);
    uint32_t bloomHeight = std::max(1u, m_height / 2);
    uint32_t levels = 1;
    while (levels < kMaxBloomLevels && std::min(bloomWidth >> levels, bloomHeight >> levels) >= 4) {
        ++levels;
    }
    CreateImage(bloomWidth, bloomHeight, levels, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_bloomImage, m_bloomImageMemory);
    for (uint32_t i = 0; i < levels; ++i) {
        m_bloomViews.push_back(CreateImageView(m_bloomImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, i));
        m_bloomExtents.push_back({ std::max(1u, bloomWidth >> i), std::max(1u, bloomHeight >> i) });
    }
    // Valid to sample even on frames where bloom is switched off
    TransitionToShaderRead(m_bloomImage, levels);

    // 4. SSAO: prepass normals + sampleable depth, raw and blurred AO
    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_normalImage, m_normalImageMemory);
    m_normalImageView = CreateImageView(m_normalImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    CreateImage(m_width, m_height, 1, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_prepassDepthImage, m_prepassDepthImageMemory);
    m_prepassDepthImageView = CreateImageView(m_prepassDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    // Sampled (SSR, SSAO, water, decals) even by a target whose prepass hasn't run (or never
    // runs: probe captures, material previews)
    TransitionToShaderRead(m_normalImage, 1);
    TransitionToShaderRead(m_prepassDepthImage, 1,
                           (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
                               ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT);

    CreateImage(m_width, m_height, 1, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_velocityImage, m_velocityImageMemory);
    m_velocityImageView = CreateImageView(m_velocityImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_velocityImage, 1); // sampled by TAA even before the first prepass

    for (auto [image, memory, view] : { std::tie(m_aoRawImage, m_aoRawImageMemory, m_aoRawImageView),
                                        std::tie(m_aoImage, m_aoImageMemory, m_aoImageView) }) {
        CreateImage(m_width, m_height, 1, kAOFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
        view = CreateImageView(image, kAOFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionToShaderRead(image, 1);
    }

    // 5. Display image (tone-mapped result)
    CreateImage(m_width, m_height, 1, kDisplayFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_colorImage, m_colorImageMemory);
    m_colorImageView = CreateImageView(m_colorImage, kDisplayFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    TransitionToShaderRead(m_colorImage, 1);

    CreateImage(m_width, m_height, 1, kDisplayFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_ldrImage, m_ldrImageMemory);
    m_ldrImageView = CreateImageView(m_ldrImage, kDisplayFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    // 6. ImGui texture descriptor
    m_descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanOffscreen::CreateFramebuffers() {
    VkDevice device = VulkanContext::Get().GetDevice();

    auto makeFramebuffer = [&](VkRenderPass renderPass, const VkImageView* views, uint32_t count, VkExtent2D extent) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = count;
        framebufferInfo.pAttachments = views;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        VkFramebuffer fb;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &fb) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create offscreen framebuffer!");
        }
        return fb;
    };

    std::array<VkImageView, 2> sceneAttachments = { m_hdrImageView, m_depthImageView };
    m_sceneFramebuffer = makeFramebuffer(m_sceneRenderPass, sceneAttachments.data(), 2, { m_width, m_height });

    // The down and up bloom passes are compatible (same format), so one framebuffer per mip serves both
    for (size_t i = 0; i < m_bloomViews.size(); ++i) {
        m_bloomFramebuffers.push_back(makeFramebuffer(m_bloomDownRenderPass, &m_bloomViews[i], 1, m_bloomExtents[i]));
    }

    m_compositeFramebuffer = makeFramebuffer(m_compositeRenderPass, &m_colorImageView, 1, { m_width, m_height });
    std::array<VkImageView, 2> overlayAttachments = { m_colorImageView, m_depthImageView };
    m_overlayFramebuffer = makeFramebuffer(m_overlayRenderPass, overlayAttachments.data(), 2, { m_width, m_height });
    m_compositeToLdrFramebuffer = makeFramebuffer(m_compositeRenderPass, &m_ldrImageView, 1, { m_width, m_height });
    m_taaFramebuffer = makeFramebuffer(m_bloomDownRenderPass, &m_taaResolveImageView, 1, { m_width, m_height });
    m_ssrFramebuffer = makeFramebuffer(m_bloomDownRenderPass, &m_ssrImageView, 1, { m_width, m_height });
    m_dofFramebuffer = makeFramebuffer(m_bloomDownRenderPass, &m_dofImageView, 1, { m_width, m_height });
    m_motionBlurFramebuffer = makeFramebuffer(m_bloomDownRenderPass, &m_motionBlurImageView, 1, { m_width, m_height });
    for (int i = 0; i < 2; ++i) {
        m_exposureFramebuffers[i] = makeFramebuffer(m_bloomDownRenderPass, &m_exposureImageViews[i], 1, { 1, 1 });
    }

    std::array<VkImageView, 3> prepassAttachments = { m_normalImageView, m_prepassDepthImageView, m_velocityImageView };
    m_prepassFramebuffer = makeFramebuffer(m_prepassRenderPass, prepassAttachments.data(), 3, { m_width, m_height });
    m_aoRawFramebuffer = makeFramebuffer(m_aoRenderPass, &m_aoRawImageView, 1, { m_width, m_height });
    m_aoFramebuffer = makeFramebuffer(m_aoRenderPass, &m_aoImageView, 1, { m_width, m_height });
}

void VulkanOffscreen::CreatePostDescriptorSets() {
    VkDevice device = VulkanContext::Get().GetDevice();
    const size_t levels = m_bloomViews.size();

    // Allocates a post-process set; bindings not listed repeat the first source (they must stay valid)
    struct Source { VkImageView view; VkSampler sampler; };
    auto allocateSet = [&](std::initializer_list<Source> sources) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_postDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_postSetLayout;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate post-process descriptor set!");
        }

        VkDescriptorImageInfo imageInfos[kPostBindings]{};
        VkWriteDescriptorSet writes[kPostBindings]{};
        const Source* list = sources.begin();
        for (uint32_t b = 0; b < kPostBindings; ++b) {
            const Source& src = (b < sources.size()) ? list[b] : list[0];
            imageInfos[b] = { src.sampler ? src.sampler : m_sampler, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = set;
            writes[b].dstBinding = b;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo = &imageInfos[b];
        }
        vkUpdateDescriptorSets(device, kPostBindings, writes, 0, nullptr);
        return set;
    };
    auto allocate = [&](VkImageView source0, VkImageView source1,
                        VkSampler sampler0 = VK_NULL_HANDLE, VkSampler sampler1 = VK_NULL_HANDLE) {
        return allocateSet({ { source0, sampler0 }, { source1, sampler1 } });
    };

    // Binding 1 is unused by the bloom passes but must still point at a valid image
    for (size_t i = 0; i < levels; ++i) {
        VkImageView source = (i == 0) ? m_motionBlurImageView : m_bloomViews[i - 1];
        m_bloomDownSets.push_back(allocate(source, source));
    }
    for (size_t i = 0; i + 1 < levels; ++i) {
        m_bloomUpSets.push_back(allocate(m_bloomViews[i + 1], m_bloomViews[i + 1]));
    }
    // Effects chain: scene -> SSR -> TAA resolve -> depth of field -> motion blur -> bloom / composite
    m_ssrSet = allocateSet({ { m_hdrImageView, m_sampler }, { m_prepassDepthImageView, m_pointSampler },
                             { m_normalImageView, m_pointSampler } });
    m_taaSet = allocateSet({ { m_ssrImageView, m_sampler }, { m_taaHistoryImageView, m_sampler },
                             { m_depthImageView, m_pointSampler }, { m_velocityImageView, m_pointSampler } });
    m_dofSet = allocateSet({ { m_taaResolveImageView, m_sampler }, { m_depthImageView, m_pointSampler } });
    m_motionBlurSet = allocateSet({ { m_dofImageView, m_sampler }, { m_depthImageView, m_pointSampler },
                                    { m_velocityImageView, m_pointSampler } });
    // Tiny viewports may have no bloom levels; bloom and exposure are skipped then, but the sets still need images
    const VkImageView bloomLargest = levels > 0 ? m_bloomViews.front() : m_motionBlurImageView;
    const VkImageView bloomSmallest = levels > 0 ? m_bloomViews.back() : m_motionBlurImageView;
    for (int i = 0; i < 2; ++i) {
        // Exposure i reads the scene's smallest bloom level and the other exposure image (last frame)
        m_exposureSets[i] = allocateSet({ { bloomSmallest, m_sampler }, { m_exposureImageViews[1 - i], m_pointSampler } });
        // Binding 3: the colour LUT, or any valid image while none is set (strength is 0 then)
        m_compositeSets[i] = allocateSet({ { m_motionBlurImageView, m_sampler }, { bloomLargest, m_sampler },
                                           { m_exposureImageViews[i], m_pointSampler },
                                           { m_lutView ? m_lutView : m_motionBlurImageView, m_lutView ? m_lutSampler : m_sampler } });
    }

    m_ssaoSet = allocate(m_prepassDepthImageView, m_normalImageView, m_pointSampler, m_pointSampler);
    m_ssaoBlurSet = allocate(m_aoRawImageView, m_prepassDepthImageView, m_sampler, m_pointSampler);
    m_fxaaSet = allocate(m_ldrImageView, m_ldrImageView);
}

void VulkanOffscreen::BeginPrepass(VkCommandBuffer cmd) {
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}; // zero normal = nothing drawn
    clearValues[1].depthStencil = { 1.0f, 0 };
    clearValues[2].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}; // w = 0: no motion written

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_prepassRenderPass;
    renderPassInfo.framebuffer = m_prepassFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = { m_width, m_height };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, { m_width, m_height } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanOffscreen::EndPrepass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanOffscreen::ComputeSSAO(VkCommandBuffer cmd, const glm::mat4& proj, const SSAOSettings& settings) {
    const glm::vec4 texel(1.0f / m_width, 1.0f / m_height, 0.0f, 0.0f);

    PostPushConstants push{};
    push.params0 = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
    push.params1 = glm::vec4(settings.radius, settings.bias, 0.0f, settings.power);
    push.params2 = texel;
    DrawFullscreen(cmd, m_aoRenderPass, m_aoRawFramebuffer, { m_width, m_height }, m_ssaoPipeline, m_ssaoSet, push);

    PostPushConstants blurPush{};
    blurPush.params1 = texel;
    DrawFullscreen(cmd, m_aoRenderPass, m_aoFramebuffer, { m_width, m_height }, m_ssaoBlurPipeline, m_ssaoBlurSet, blurPush);
}

void VulkanOffscreen::BeginRenderPass(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_sceneRenderPass;
    renderPassInfo.framebuffer = m_sceneFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = { m_width, m_height };

    std::array<VkClearValue, 2> clearValues{};
    // Linear HDR value that tone-maps to the dark editor background (~#1a1a1e) when the sky is off
    clearValues[0].color = {{ 0.03f, 0.03f, 0.037f, 1.0f }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    SetFullViewport(cmd);
    m_inOpaqueHalf = true;
}

void VulkanOffscreen::SetFullViewport(VkCommandBuffer cmd) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = { m_width, m_height };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanOffscreen::NextScenePass(VkCommandBuffer cmd) {
    if (!m_inOpaqueHalf) return;
    vkCmdEndRenderPass(cmd);
    m_inOpaqueHalf = false;
    // Copy the opaque image for refraction (the render passes' dependencies order the HDR image)
    CopySceneColorAndResume(cmd);
}

void VulkanOffscreen::RefreshSceneColor(VkCommandBuffer cmd) {
    if (m_inOpaqueHalf) {
        NextScenePass(cmd);
        return;
    }
    // The second half leaves colour and depth sampleable: back to where the copy and the second
    // half expect them
    vkCmdEndRenderPass(cmd);
    VkFormat depthFormat = VulkanContext::Get().FindDepthFormat();
    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    VkImageMemoryBarrier barriers[2]{};
    for (VkImageMemoryBarrier& b : barriers) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    barriers[0].image = m_hdrImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].image = m_depthImage;
    barriers[1].subresourceRange.aspectMask = depthAspect;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    CopySceneColorAndResume(cmd);
}

void VulkanOffscreen::CopySceneColorAndResume(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_sceneColorImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { m_width, m_height, 1 };
    vkCmdCopyImage(cmd, m_hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_sceneColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_sceneLoadRenderPass;
    renderPassInfo.framebuffer = m_sceneFramebuffer; // compatible with both halves
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = { m_width, m_height };
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    SetFullViewport(cmd);
}

void VulkanOffscreen::EndRenderPass(VkCommandBuffer cmd) {
    // Always finish through the second half so the HDR image ends up sampleable
    NextScenePass(cmd);
    vkCmdEndRenderPass(cmd);
}

void VulkanOffscreen::DrawFullscreen(VkCommandBuffer cmd, VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent,
                                     VkPipeline pipeline, VkDescriptorSet descriptorSet, const PostPushConstants& push) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPushConstants), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void VulkanOffscreen::SetColorLUT(VkImageView view, VkSampler sampler, uint32_t size) {
    m_lutView = view;
    m_lutSampler = sampler;
    m_lutSize = view ? size : 0;

    VkDescriptorImageInfo info{ view ? sampler : m_sampler, view ? view : m_motionBlurImageView,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_compositeSets[i];
        writes[i].dstBinding = 3;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &info;
    }
    if (m_compositeSets[0] && m_compositeSets[1]) {
        vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), 2, writes, 0, nullptr);
    }
}

void VulkanOffscreen::ApplyPostProcess(VkCommandBuffer cmd, const PostProcessSettings& settings) {
    const VkExtent2D fullSize{ m_width, m_height };
    const glm::vec2 texel(1.0f / m_width, 1.0f / m_height);

    // 0. Screen-space reflections (a pass-through when off or without this frame's prepass)
    {
        GpuProfileScope scope(cmd, "SSR");
        PostPushConstants push{};
        push.params0 = glm::vec4(settings.projectionScale, settings.depthLinearize);
        push.params1 = glm::vec4(settings.ssrStrength, settings.ssrMaxDistance, settings.ssrThickness,
                                 settings.prepassValid ? 1.0f : 0.0f);
        push.params2 = glm::vec4(texel, static_cast<float>(settings.frameIndex % 64), 0.0f);
        DrawFullscreen(cmd, m_bloomDownRenderPass, m_ssrFramebuffer, fullSize, m_ssrPipeline, m_ssrSet, push);
    }

    // 1. TAA resolve (a pass-through when TAA is off, so later passes always have one input)
    {
        GpuProfileScope scope(cmd, "TAA");
        PostPushConstants push{};
        push.params1 = glm::vec4(settings.jitterNdc, (settings.taa && m_taaHistoryValid) ? 1.0f : 0.0f, 0.9f);
        push.params2 = glm::vec4(texel, settings.velocityValid ? 1.0f : 0.0f, 0.0f);
        push.matrix = settings.reprojection;
        DrawFullscreen(cmd, m_bloomDownRenderPass, m_taaFramebuffer, fullSize, m_taaPipeline, m_taaSet, push);

        if (settings.taa) {
            CopyResolveToHistory(cmd);
            m_taaHistoryValid = true;
        } else {
            m_taaHistoryValid = false;
        }
    }

    // 2. Depth of field (pass-through when the aperture is 0)
    {
        GpuProfileScope scope(cmd, "Depth of Field");
        PostPushConstants push{};
        push.params0 = glm::vec4(settings.depthLinearize, settings.focusDistance, settings.aperture);
        push.params1 = glm::vec4(settings.maxBlurRadius, texel, 0.0f);
        DrawFullscreen(cmd, m_bloomDownRenderPass, m_dofFramebuffer, fullSize, m_dofPipeline, m_dofSet, push);
    }

    // 3. Motion blur (pass-through when the shutter is 0)
    {
        GpuProfileScope scope(cmd, "Motion Blur");
        PostPushConstants push{};
        push.params0 = glm::vec4(settings.motionBlur, 32.0f, texel);
        push.params1 = glm::vec4(settings.velocityValid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        push.matrix = settings.reprojection;
        DrawFullscreen(cmd, m_bloomDownRenderPass, m_motionBlurFramebuffer, fullSize, m_motionBlurPipeline, m_motionBlurSet, push);
    }

    // 4. Bloom downsample (also feeds auto exposure), exposure, bloom upsample
    const size_t levels = m_bloomViews.size();
    const bool bloomOn = settings.bloomStrength > 0.0001f && levels > 0;
    const bool downsample = (bloomOn || settings.autoExposure || settings.lensFlare > 0.0f) && levels > 0;

    VulkanGpuProfiler* profiler = VulkanGpuProfiler::Active();
    if (profiler) profiler->Begin(cmd, "Bloom & Exposure");
    if (downsample) {
        for (size_t i = 0; i < levels; ++i) {
            VkExtent2D src = (i == 0) ? fullSize : m_bloomExtents[i - 1];
            PostPushConstants push{};
            push.params0 = glm::vec4(1.0f / src.width, 1.0f / src.height, (i == 0) ? 1.0f : 0.0f, 0.0f);
            DrawFullscreen(cmd, m_bloomDownRenderPass, m_bloomFramebuffers[i], m_bloomExtents[i],
                           m_bloomDownPipeline, m_bloomDownSets[i], push);
        }
    }

    if (settings.autoExposure && downsample) {
        // Ease toward the target by a frame-rate independent amount
        const float blend = 1.0f - std::exp(-std::max(settings.deltaTime, 0.0f) * settings.adaptationSpeed);
        m_exposureIndex = 1 - m_exposureIndex;
        PostPushConstants push{};
        push.params0 = glm::vec4(blend, settings.minAutoExposure, settings.maxAutoExposure, m_exposureValid ? 1.0f : 0.0f);
        DrawFullscreen(cmd, m_bloomDownRenderPass, m_exposureFramebuffers[m_exposureIndex], { 1, 1 },
                       m_exposurePipeline, m_exposureSets[m_exposureIndex], push);
        m_exposureValid = true;
    } else {
        m_exposureValid = false;
    }

    if (bloomOn) {
        // Upsample: add each smaller level back onto the next larger one, ending at mip 0
        for (size_t i = levels - 1; i-- > 0;) {
            VkExtent2D src = m_bloomExtents[i + 1];
            PostPushConstants push{};
            push.params0 = glm::vec4(1.0f / src.width, 1.0f / src.height, 0.0f, 1.0f);
            DrawFullscreen(cmd, m_bloomUpRenderPass, m_bloomFramebuffers[i], m_bloomExtents[i],
                           m_bloomUpPipeline, m_bloomUpSets[i], push);
        }
    }

    if (profiler) {
        profiler->End(cmd);
        profiler->Begin(cmd, "Composite & FXAA");
    }
    // 5. Composite. Mip 0 holds the sum of every bloom level (when bloom is on); params0.w averages
    // it back to scene brightness
    PostPushConstants push{};
    push.params0 = glm::vec4(settings.exposure,
                             bloomOn ? settings.bloomStrength : 0.0f,
                             settings.acesToneMapping ? 1.0f : 0.0f,
                             levels > 0 ? 1.0f / static_cast<float>(bloomOn ? levels : 1) : 1.0f);
    push.params1 = glm::vec4(settings.contrast, settings.saturation, settings.temperature, settings.tint);
    push.params2 = glm::vec4(settings.vignette, settings.filmGrain, settings.time, settings.taa ? settings.sharpen : 0.0f);
    push.matrix[0] = glm::vec4(downsample ? settings.lensFlare : 0.0f, settings.autoExposure ? 1.0f : 0.0f, texel);
    push.matrix[1] = glm::vec4(m_lutView ? settings.lutStrength : 0.0f, static_cast<float>(m_lutSize),
                               settings.chromaticAberration,
                               settings.halfUnderwater ? 2.0f * settings.waterlineSide : (settings.underwater ? 1.0f : 0.0f));
    push.matrix[2] = settings.waterline[0];
    push.matrix[3] = settings.waterline[1];
    // With FXAA the composite goes to an intermediate image that FXAA then filters into the display image
    const bool fxaaOn = settings.fxaa && m_fxaaPipeline != VK_NULL_HANDLE;
    DrawFullscreen(cmd, m_compositeRenderPass, fxaaOn ? m_compositeToLdrFramebuffer : m_compositeFramebuffer,
                   fullSize, m_compositePipeline, m_compositeSets[m_exposureIndex], push);

    if (fxaaOn) {
        PostPushConstants fxaaPush{};
        fxaaPush.params1 = glm::vec4(texel, 0.0f, 0.0f);
        DrawFullscreen(cmd, m_compositeRenderPass, m_compositeFramebuffer, fullSize,
                       m_fxaaPipeline, m_fxaaSet, fxaaPush);
    }
    if (profiler) profiler->End(cmd);
}

void VulkanOffscreen::CopyDisplayImageTo(VkCommandBuffer cmd, VkImage dst, VkImageLayout dstLayout) const {
    VkImageMemoryBarrier barriers[2]{};
    for (VkImageMemoryBarrier& b : barriers) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    barriers[0].image = m_colorImage;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].image = dst;
    barriers[1].oldLayout = dstLayout;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcAccessMask = dstLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { m_width, m_height, 1 };
    vkCmdCopyImage(cmd, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    // Both back to sampleable: the display image for the next frame's passes, dst for ImGui
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);
}

void VulkanOffscreen::ReadbackDisplayImage(std::vector<uint8_t>& outRGBA) const {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    const VkDeviceSize size = static_cast<VkDeviceSize>(m_width) * m_height * 4;

    VkBuffer buffer;
    VkDeviceMemory memory;
    VulkanBufferUtils::CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    buffer, memory);

    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_colorImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { m_width, m_height, 1 };
    vkCmdCopyImageToBuffer(cmd, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    ctx.EndSingleTimeCommands(cmd);

    outRGBA.resize(static_cast<size_t>(size));
    void* mapped = nullptr;
    vkMapMemory(device, memory, 0, size, 0, &mapped);
    std::memcpy(outRGBA.data(), mapped, static_cast<size_t>(size));
    vkUnmapMemory(device, memory);

    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

void VulkanOffscreen::ReadbackHdrImage(std::vector<uint16_t>& outRGBA) const {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    const VkDeviceSize size = static_cast<VkDeviceSize>(m_width) * m_height * 8;

    VkBuffer buffer;
    VkDeviceMemory memory;
    VulkanBufferUtils::CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    buffer, memory);

    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_hdrImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { m_width, m_height, 1 };
    vkCmdCopyImageToBuffer(cmd, m_hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    ctx.EndSingleTimeCommands(cmd);

    outRGBA.resize(static_cast<size_t>(size) / 2);
    void* mapped = nullptr;
    vkMapMemory(device, memory, 0, size, 0, &mapped);
    std::memcpy(outRGBA.data(), mapped, static_cast<size_t>(size));
    vkUnmapMemory(device, memory);

    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

void VulkanOffscreen::CopyResolveToHistory(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barriers[2]{};
    for (VkImageMemoryBarrier& b : barriers) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    barriers[0].image = m_taaResolveImage;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].image = m_taaHistoryImage;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { m_width, m_height, 1 };
    vkCmdCopyImage(cmd, m_taaResolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_taaHistoryImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);
}
