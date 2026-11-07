#include "VulkanFluidTargets.h"
#include "VulkanContext.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {

void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS) throw std::runtime_error("Failed to create fluid image!");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = ctx.FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate fluid image!");
    vkBindImageMemory(device, image, memory, 0);
}

VkImageView CreateView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = { aspect, 0, 1, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(VulkanContext::Get().GetDevice(), &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid image view!");
    }
    return view;
}

// Valid to sample (as "no fluid") before anything has been drawn into it
void ToShaderRead(VkImage image) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    ctx.EndSingleTimeCommands(cmd);
}

VkAttachmentDescription ColorAttachment(VkFormat format) {
    VkAttachmentDescription a{};
    a.format = format;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return a;
}

VkRenderPass CreatePass(const VkAttachmentDescription* attachments, uint32_t count, uint32_t colorCount, bool depth) {
    std::array<VkAttachmentReference, 2> colorRefs{};
    for (uint32_t i = 0; i < colorCount; ++i) colorRefs[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ colorCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorCount;
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = depth ? &depthRef : nullptr;
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
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(VulkanContext::Get().GetDevice(), &info, nullptr, &pass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid render pass!");
    }
    return pass;
}

} // namespace

void VulkanFluidTargets::Init(uint32_t width, uint32_t height) {
    m_width = std::max(width, 1u);
    m_height = std::max(height, 1u);
    CreateRenderPasses();
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(VulkanContext::Get().GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid sampler!");
    }
    CreateTargets();
}

void VulkanFluidTargets::CreateRenderPasses() {
    const VkAttachmentDescription particle[3] = {
        ColorAttachment(kDistanceFormat),
        ColorAttachment(kColorFormat),
        [] {
            VkAttachmentDescription depth = ColorAttachment(VulkanContext::Get().FindDepthFormat());
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            return depth;
        }()
    };
    m_particlePass = CreatePass(particle, 3, 2, true);
    const VkAttachmentDescription thickness[1] = { ColorAttachment(kThicknessFormat) };
    m_thicknessPass = CreatePass(thickness, 1, 1, false);
    const VkAttachmentDescription smooth[1] = { ColorAttachment(kSmoothFormat) };
    m_smoothPass = CreatePass(smooth, 1, 1, false);
}

void VulkanFluidTargets::CreateTargets() {
    const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    CreateImage(m_width, m_height, kDistanceFormat, color, m_distance, m_distanceMemory);
    CreateImage(m_width, m_height, kThicknessFormat, color, m_thickness, m_thicknessMemory);
    CreateImage(m_width, m_height, kSmoothFormat, color, m_smooth, m_smoothMemory);
    CreateImage(m_width, m_height, kSmoothFormat, color, m_smoothA, m_smoothAMemory);
    CreateImage(m_width, m_height, kColorFormat, color, m_color, m_colorMemory);
    const VkFormat depthFormat = VulkanContext::Get().FindDepthFormat();
    CreateImage(m_width, m_height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depth, m_depthMemory);
    m_distanceView = CreateView(m_distance, kDistanceFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    m_thicknessView = CreateView(m_thickness, kThicknessFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    m_smoothView = CreateView(m_smooth, kSmoothFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    m_smoothAView = CreateView(m_smoothA, kSmoothFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    m_colorView = CreateView(m_color, kColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    m_depthView = CreateView(m_depth, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    for (VkImage image : { m_distance, m_thickness, m_smooth, m_smoothA, m_color }) ToShaderRead(image);

    VkDevice device = VulkanContext::Get().GetDevice();
    const VkImageView particleViews[3] = { m_distanceView, m_colorView, m_depthView };
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = m_particlePass;
    info.attachmentCount = 3;
    info.pAttachments = particleViews;
    info.width = m_width;
    info.height = m_height;
    info.layers = 1;
    if (vkCreateFramebuffer(device, &info, nullptr, &m_particleFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid framebuffer!");
    }
    info.renderPass = m_smoothPass;
    info.attachmentCount = 1;
    info.pAttachments = &m_smoothView;
    if (vkCreateFramebuffer(device, &info, nullptr, &m_smoothFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid framebuffer!");
    }
    info.pAttachments = &m_smoothAView;
    if (vkCreateFramebuffer(device, &info, nullptr, &m_smoothAFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid framebuffer!");
    }
    info.renderPass = m_thicknessPass;
    info.pAttachments = &m_thicknessView;
    if (vkCreateFramebuffer(device, &info, nullptr, &m_thicknessFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid framebuffer!");
    }
}

void VulkanFluidTargets::DestroyTargets() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (VkFramebuffer* fb : { &m_particleFramebuffer, &m_smoothFramebuffer, &m_smoothAFramebuffer, &m_thicknessFramebuffer }) {
        if (*fb) vkDestroyFramebuffer(device, *fb, nullptr);
        *fb = VK_NULL_HANDLE;
    }
    for (VkImageView* view : { &m_distanceView, &m_thicknessView, &m_smoothView, &m_smoothAView, &m_colorView, &m_depthView }) {
        if (*view) vkDestroyImageView(device, *view, nullptr);
        *view = VK_NULL_HANDLE;
    }
    for (VkImage* image : { &m_distance, &m_thickness, &m_smooth, &m_smoothA, &m_color, &m_depth }) {
        if (*image) vkDestroyImage(device, *image, nullptr);
        *image = VK_NULL_HANDLE;
    }
    for (VkDeviceMemory* memory : { &m_distanceMemory, &m_thicknessMemory, &m_smoothMemory, &m_smoothAMemory, &m_colorMemory, &m_depthMemory }) {
        if (*memory) vkFreeMemory(device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
    }
}

void VulkanFluidTargets::Cleanup() {
    DestroyTargets();
    VkDevice device = VulkanContext::Get().GetDevice();
    for (VkRenderPass* pass : { &m_particlePass, &m_smoothPass, &m_thicknessPass }) {
        if (*pass) vkDestroyRenderPass(device, *pass, nullptr);
        *pass = VK_NULL_HANDLE;
    }
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    m_sampler = VK_NULL_HANDLE;
}

void VulkanFluidTargets::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == m_width && height == m_height)) return;
    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
    m_width = width;
    m_height = height;
    DestroyTargets();
    CreateTargets();
}

void VulkanFluidTargets::Begin(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer framebuffer, uint32_t colorCount, bool depth) {
    std::array<VkClearValue, 3> clears{};
    for (uint32_t c = 0; c < colorCount; ++c) clears[c].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}; // no fluid
    if (depth) clears[colorCount].depthStencil = { 1.0f, 0 };
    const uint32_t clearCount = colorCount + (depth ? 1u : 0u);
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = pass;
    info.framebuffer = framebuffer;
    info.renderArea.extent = { m_width, m_height };
    info.clearValueCount = clearCount;
    info.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, { m_width, m_height } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanFluidTargets::BeginParticles(VkCommandBuffer cmd) { Begin(cmd, m_particlePass, m_particleFramebuffer, 2, true); }
void VulkanFluidTargets::BeginThickness(VkCommandBuffer cmd) { Begin(cmd, m_thicknessPass, m_thicknessFramebuffer, 1, false); }
void VulkanFluidTargets::BeginSmooth(VkCommandBuffer cmd, int pass) {
    Begin(cmd, m_smoothPass, pass == 0 ? m_smoothAFramebuffer : m_smoothFramebuffer, 1, false);
}
void VulkanFluidTargets::End(VkCommandBuffer cmd) { vkCmdEndRenderPass(cmd); }
