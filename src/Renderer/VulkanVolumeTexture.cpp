#include "VulkanVolumeTexture.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include <glm/gtc/packing.hpp>
#include <cstring>
#include <stdexcept>

void VulkanVolumeTexture::Init() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(VulkanContext::Get().GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GI volume sampler!");
    }
    const std::vector<glm::vec4> black(1, glm::vec4(0.0f));
    Upload(glm::uvec3(1), black, black, black);
}

void VulkanVolumeTexture::Destroy() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (int c = 0; c < 3; ++c) {
        if (m_views[c]) vkDestroyImageView(device, m_views[c], nullptr);
        if (m_images[c]) vkDestroyImage(device, m_images[c], nullptr);
        if (m_memory[c]) vkFreeMemory(device, m_memory[c], nullptr);
        m_views[c] = VK_NULL_HANDLE;
        m_images[c] = VK_NULL_HANDLE;
        m_memory[c] = VK_NULL_HANDLE;
    }
}

void VulkanVolumeTexture::Cleanup() {
    Destroy();
    if (m_sampler) {
        vkDestroySampler(VulkanContext::Get().GetDevice(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

void VulkanVolumeTexture::Upload(const glm::uvec3& dims, const std::vector<glm::vec4>& red,
                                 const std::vector<glm::vec4>& green, const std::vector<glm::vec4>& blue) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    vkDeviceWaitIdle(device);
    Destroy();

    const size_t count = static_cast<size_t>(dims.x) * dims.y * dims.z;
    const VkDeviceSize bytes = count * 4 * sizeof(uint16_t);
    const std::vector<glm::vec4>* channels[3] = { &red, &green, &blue };
    for (int c = 0; c < 3; ++c) {
        // Half floats: filterable on every GPU
        std::vector<uint16_t> halves(count * 4);
        for (size_t i = 0; i < count; ++i)
            for (int k = 0; k < 4; ++k) halves[i * 4 + k] = glm::packHalf1x16((*channels[c])[i][k]);

        VkBuffer staging;
        VkDeviceMemory stagingMemory;
        VulkanBufferUtils::CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        staging, stagingMemory);
        void* mapped = nullptr;
        vkMapMemory(device, stagingMemory, 0, bytes, 0, &mapped);
        std::memcpy(mapped, halves.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent = { dims.x, dims.y, dims.z };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        if (vkCreateImage(device, &imageInfo, nullptr, &m_images[c]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GI volume image!");
        }
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, m_images[c], &requirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = ctx.FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory[c]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate GI volume memory!");
        }
        vkBindImageMemory(device, m_images[c], m_memory[c], 0);

        VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_images[c];
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { dims.x, dims.y, dims.z };
        vkCmdCopyBufferToImage(cmd, staging, m_images[c], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        ctx.EndSingleTimeCommands(cmd);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[c];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_views[c]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GI volume view!");
        }
    }
}


void VulkanVXGIVolume::Init() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(kMipLevels - 1);
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VXGI volume sampler!");
    }

    m_texelsPerFrame = 0;
    for (uint32_t m = 0; m < kMipLevels; ++m) {
        const uint32_t d = std::max(1u, kBaseDim >> m);
        m_texelsPerFrame += static_cast<VkDeviceSize>(d) * d * d;
    }
    const VkDeviceSize totalStagingBytes = m_texelsPerFrame * kFramesInFlight * 4 * sizeof(uint16_t);
    VulkanBufferUtils::CreateBuffer(totalStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    m_stagingBuffer, m_stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, m_stagingMemory, 0, totalStagingBytes, 0, &mapped);
    m_stagingMapped = static_cast<uint16_t*>(mapped);
    std::memset(m_stagingMapped, 0, static_cast<size_t>(totalStagingBytes));

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent = { kBaseDim, kBaseDim, kBaseDim };
    imageInfo.mipLevels = kMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VXGI volume image!");
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, m_image, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = ctx.FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate VXGI volume memory!");
    }
    vkBindImageMemory(device, m_image, m_memory, 0);

    // Initialize all mip levels to transparent black and transition to SHADER_READ_ONLY_OPTIMAL
    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    std::vector<std::vector<glm::vec4>> zeroMips(kMipLevels);
    for (uint32_t m = 0; m < kMipLevels; ++m) {
        const uint32_t d = std::max(1u, kBaseDim >> m);
        zeroMips[m].assign(static_cast<size_t>(d) * d * d, glm::vec4(0.0f));
    }
    RecordUpload(cmd, 0, zeroMips);
    ctx.EndSingleTimeCommands(cmd);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, kMipLevels, 0, 1 };
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VXGI volume view!");
    }
}

void VulkanVXGIVolume::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        if (m_stagingMapped) {
            vkUnmapMemory(device, m_stagingMemory);
            m_stagingMapped = nullptr;
        }
        vkDestroyBuffer(device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

void VulkanVXGIVolume::RecordUpload(VkCommandBuffer cmd, uint32_t frameIndex,
                                    const std::vector<std::vector<glm::vec4>>& mips) {
    if (!m_stagingMapped || m_image == VK_NULL_HANDLE || mips.size() < kMipLevels) return;
    const uint32_t slot = frameIndex % kFramesInFlight;
    const VkDeviceSize slotTexelOffset = m_texelsPerFrame * slot;
    uint16_t* dst = m_stagingMapped + slotTexelOffset * 4;

    VkBufferImageCopy regions[kMipLevels]{};
    VkDeviceSize cursorTexels = slotTexelOffset;
    for (uint32_t m = 0; m < kMipLevels; ++m) {
        const uint32_t d = std::max(1u, kBaseDim >> m);
        const size_t count = static_cast<size_t>(d) * d * d;
        const auto& srcMip = mips[m];
        const size_t n = std::min(count, srcMip.size());
        for (size_t i = 0; i < n; ++i) {
            const glm::vec4& v = srcMip[i];
            dst[0] = glm::packHalf1x16(v.x);
            dst[1] = glm::packHalf1x16(v.y);
            dst[2] = glm::packHalf1x16(v.z);
            dst[3] = glm::packHalf1x16(v.w);
            dst += 4;
        }
        for (size_t i = n; i < count; ++i) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            dst += 4;
        }

        regions[m].bufferOffset = cursorTexels * 4 * sizeof(uint16_t);
        regions[m].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
        regions[m].imageExtent = { d, d, d };
        cursorTexels += count;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, kMipLevels, 0, 1 };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kMipLevels, regions);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}
