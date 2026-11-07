#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// Baked lightmap atlas: one 2D B10G11R11 float texture holding every lightmapped object's
// indirect light (see LightmapBaker). Starts as a 1x1 black texture so it can always be bound.
class VulkanLightmapTexture {
public:
    void Init();
    void Cleanup();

    // Replaces the contents with width * height texels packed as B10G11R11 (glm::packF2x11_1x10).
    // Waits for the GPU (call outside command buffer recording).
    void Upload(uint32_t width, uint32_t height, const std::vector<uint32_t>& texels);

    VkImageView GetView() const { return m_view; }
    VkSampler GetSampler() const { return m_sampler; }

private:
    void Destroy();
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_memory{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};
