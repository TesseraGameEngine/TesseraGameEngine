#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

// Baked GI irradiance volume: three RGBA16F 3D textures (one per colour channel) holding L1
// spherical-harmonic coefficients per probe: (L0, L1x, L1y, L1z). Sampled with trilinear
// filtering by env_common.glsl. Starts as a 1x1x1 black volume so it can always be bound.
class VulkanVolumeTexture {
public:
    void Init();
    void Cleanup();

    // Replaces the contents; each vector has dims.x * dims.y * dims.z entries (x fastest).
    // Waits for the GPU (call outside command buffer recording).
    void Upload(const glm::uvec3& dims, const std::vector<glm::vec4>& red,
                const std::vector<glm::vec4>& green, const std::vector<glm::vec4>& blue);

    VkImageView GetView(int channel) const { return m_views[channel]; }
    VkSampler GetSampler() const { return m_sampler; }

private:
    void Destroy();
    VkImage m_images[3]{};
    VkDeviceMemory m_memory[3]{};
    VkImageView m_views[3]{};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

// Real-time Voxel Global Illumination (VXGI) 3D radiance + opacity volume.
// Stores premultiplied radiance (rgb) and voxel opacity (a) in a mipmapped 64^3 RGBA16F 3D texture
// (6 mip levels: 64, 32, 16, 8, 4, 2) sampled by Voxel Cone Tracing in env_common.glsl (binding 40).
class VulkanVXGIVolume {
public:
    static constexpr uint32_t kBaseDim = 64;
    static constexpr uint32_t kMipLevels = 6; // 64 -> 32 -> 16 -> 8 -> 4 -> 2
    static constexpr uint32_t kFramesInFlight = 2;

    void Init();
    void Cleanup();

    // Copies a full 6-level mip pyramid into the 3D volume on the given command buffer (outside a render pass).
    void RecordUpload(VkCommandBuffer cmd, uint32_t frameIndex, const std::vector<std::vector<glm::vec4>>& mips);

    VkImageView GetView() const { return m_view; }
    VkSampler GetSampler() const { return m_sampler; }

private:
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_memory{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkBuffer m_stagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_stagingMemory{VK_NULL_HANDLE};
    uint16_t* m_stagingMapped{nullptr};
    VkDeviceSize m_texelsPerFrame{0};
};
