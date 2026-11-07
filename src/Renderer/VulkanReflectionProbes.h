#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

// Storage for reflection probe captures: one HDR 2D-array image with 6 layers (cube faces
// +X, -X, +Y, -Y, +Z, -Z) per probe and a full mip chain, so rough surfaces can sample blurrier
// levels. Faces are rendered elsewhere (a 128x128 VulkanOffscreen) and copied in here.
class VulkanReflectionProbes {
public:
    static constexpr uint32_t kMaxProbes = 4;
    static constexpr uint32_t kFaceSize = 128;
    static constexpr uint32_t kLayers = kMaxProbes * 6;
    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    void Init();
    void Cleanup();

    // Copies mip 0 of an HDR colour image (kFaceSize square, in SHADER_READ_ONLY_OPTIMAL) into
    // one face, leaving both images in SHADER_READ_ONLY_OPTIMAL.
    void CopyFaceFrom(VkCommandBuffer cmd, VkImage source, uint32_t probe, uint32_t face);

    // Rebuilds every mip level from mip 0 (all layers). Call after a probe's faces are captured.
    void GenerateMips(VkCommandBuffer cmd);

    VkImageView GetArrayView() const { return m_arrayView; }
    VkSampler GetSampler() const { return m_sampler; }
    uint32_t GetMipCount() const { return m_mipLevels; }

private:
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_memory{VK_NULL_HANDLE};
    VkImageView m_arrayView{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    uint32_t m_mipLevels{1};
};
