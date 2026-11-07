#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>

// GPU resources of the sky:
// - Cloud noise: a tiling 2D coverage map (the shapes of the cloud layer, also used for cloud
//   shadows and reflections) and a tiling 3D detail texture that erodes the volumetric clouds.
//   Both are made once on the CPU at start-up.
// - Atmosphere: per main view, a sky-view look-up table (Hillaire 2020): the sky's radiance in
//   every direction from the camera, ray-marched each frame through a physically based atmosphere
//   (Rayleigh, Mie and ozone) by sky_view_lut.comp. The sky, ambient light and reflections read it
//   when the scene uses the physical atmosphere.
class VulkanSkyResources {
public:
    static constexpr uint32_t kCoverageSize = 512;   // texels across the 2D coverage map
    static constexpr uint32_t kCoveragePeriod = 8;   // coverage noise cells across it (it tiles)
    static constexpr uint32_t kDetailSize = 64;      // texels along each side of the 3D detail
    static constexpr uint32_t kLutWidth = 192;
    static constexpr uint32_t kLutHeight = 108;

    void Init();
    void Cleanup();

    // Records the sky-view LUT for a main view (0 or 1): outside render passes, before the view's
    // sky and scene. cameraHeight in metres above sea level; sunDir toward the sun.
    void RecordSkyViewLut(VkCommandBuffer cmd, int slot, const glm::vec3& sunDir, float cameraHeight);

    VkImageView GetCoverageView() const { return m_coverage.view; }
    VkImageView GetDetailView() const { return m_detail.view; }
    VkSampler GetRepeatSampler() const { return m_repeatSampler; }
    VkImageView GetLutView(int slot) const { return m_luts[slot % 2].view; }
    VkSampler GetLutSampler() const { return m_clampSampler; }

    // Light reaching the ground through the physical atmosphere from a sun at sunDir, for a viewer
    // at cameraHeight (m): the sun's colour and brightness at the surface (relative to space = 1)
    static glm::vec3 SunTransmittance(const glm::vec3& sunDir, float cameraHeight);

private:
    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
    };
    void CreateImage(Image& image, VkImageType type, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage);
    void UploadR8(Image& image, VkExtent3D extent, const uint8_t* texels, size_t size);
    void DestroyImage(Image& image);
    void CreateLutPipeline();

    Image m_coverage;
    Image m_detail;
    Image m_luts[2];
    VkSampler m_repeatSampler{VK_NULL_HANDLE};
    VkSampler m_clampSampler{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_lutSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_lutPool{VK_NULL_HANDLE};
    VkDescriptorSet m_lutSets[2]{};
    VkPipelineLayout m_lutPipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_lutPipeline{VK_NULL_HANDLE};
};
