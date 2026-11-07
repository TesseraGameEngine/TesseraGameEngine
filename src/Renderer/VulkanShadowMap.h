#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

// Must match the push constant block in shaders/shadow.vert / shadow.frag
struct ShadowPushConstants {
    glm::mat4 lightModelViewProj;   // light view-projection * model
    glm::vec4 color{1.0f};          // material colour (alpha used for cutout)
    float alphaCutoff{0.0f};        // > 0: texels with alpha below this cast no shadow
    int textureFlags{0};            // TEX_ALBEDO bit: sample the albedo map's alpha
    float instanced{0.0f};          // 1: lightModelViewProj is the light alone; models come from the instance buffer
    float _pad{0.0f};               // 1: sways with the wind (foliage)
};

// Layered depth texture rendered from the shadow-casting light (one layer per cascade),
// sampled by the main shader with hardware depth comparison (sampler2DArrayShadow).
class VulkanShadowMap {
public:
    static constexpr uint32_t kMaxCascades = 4;

    VulkanShadowMap() = default;
    ~VulkanShadowMap() = default;

    void Init(uint32_t resolution, uint32_t layers = kMaxCascades);
    void Cleanup();

    // Renders into one layer (cascade)
    void BeginRenderPass(VkCommandBuffer cmd, uint32_t layer);
    void EndRenderPass(VkCommandBuffer cmd);

    VkRenderPass GetRenderPass() const { return m_renderPass; }
    VkImageView GetImageView() const { return m_arrayView; } // all layers, for sampling
    VkSampler GetSampler() const { return m_sampler; }
    VkSampler GetPlainSampler() const { return m_plainSampler; } // reads the depth itself (no compare)
    uint32_t GetResolution() const { return m_resolution; }
    uint32_t GetLayerCount() const { return m_layers; }

    // Layout the depth image is left in between frames, for descriptor writes
    static constexpr VkImageLayout kSampledLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

private:
    void CreateImage();
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateSampler();

    uint32_t m_resolution{2048};
    uint32_t m_layers{kMaxCascades};
    VkFormat m_format{VK_FORMAT_D32_SFLOAT};

    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_imageMemory{VK_NULL_HANDLE};
    VkImageView m_arrayView{VK_NULL_HANDLE};
    std::vector<VkImageView> m_layerViews;     // one per layer, as render targets
    std::vector<VkFramebuffer> m_framebuffers; // one per layer
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkSampler m_plainSampler{VK_NULL_HANDLE};
    VkRenderPass m_renderPass{VK_NULL_HANDLE};
};
