#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

// Render targets for screen-space fluid rendering in one view (VulkanPipeline::DrawFluid*):
// 1. Particle pass: every fluid particle drawn as a sphere into the nearest-surface distance
//    (RG32F: distance, 0 = no fluid, and the particle's radius) and its body's colour (RGBA16F:
//    colour, colour depth), with its own depth buffer.
// 2. Thickness pass: the same spheres summed into the thickness (R16F, additive).
// 3. Smooth passes: the distance image blurred (edge-preserving) into a continuous surface, then
//    again more widely, which irons out the rows particles form against walls.
// The scene's transparent pass then shades that surface (fluid_composite.frag).
class VulkanFluidTargets {
public:
    static constexpr VkFormat kDistanceFormat = VK_FORMAT_R32G32_SFLOAT;
    static constexpr VkFormat kSmoothFormat = VK_FORMAT_R32_SFLOAT;
    static constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kThicknessFormat = VK_FORMAT_R16_SFLOAT;

    void Init(uint32_t width, uint32_t height);
    void Cleanup();
    void Resize(uint32_t width, uint32_t height);

    // Clears and begins the particle pass (nearest distance) / the thickness pass
    void BeginParticles(VkCommandBuffer cmd);
    void BeginThickness(VkCommandBuffer cmd);
    // Begins smoothing pass 0 (distance -> first smoothing) or 1 (-> the final surface)
    void BeginSmooth(VkCommandBuffer cmd, int pass);
    void End(VkCommandBuffer cmd);

    VkRenderPass GetParticleRenderPass() const { return m_particlePass; }
    VkRenderPass GetThicknessRenderPass() const { return m_thicknessPass; }
    VkRenderPass GetSmoothRenderPass() const { return m_smoothPass; }
    VkImageView GetDistanceView() const { return m_distanceView; }
    VkImageView GetSmoothDistanceView() const { return m_smoothView; }
    VkImageView GetFirstSmoothView() const { return m_smoothAView; }
    VkImageView GetColorView() const { return m_colorView; }
    VkImageView GetThicknessView() const { return m_thicknessView; }
    VkSampler GetSampler() const { return m_sampler; }

private:
    void CreateRenderPasses();
    void CreateTargets();
    void DestroyTargets();
    void Begin(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer framebuffer, uint32_t colorCount, bool depth);

    uint32_t m_width{1};
    uint32_t m_height{1};
    VkRenderPass m_particlePass{VK_NULL_HANDLE};
    VkRenderPass m_smoothPass{VK_NULL_HANDLE};
    VkRenderPass m_thicknessPass{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkImage m_distance{VK_NULL_HANDLE}, m_thickness{VK_NULL_HANDLE}, m_depth{VK_NULL_HANDLE}, m_smooth{VK_NULL_HANDLE};
    VkImage m_smoothA{VK_NULL_HANDLE}, m_color{VK_NULL_HANDLE};
    VkDeviceMemory m_distanceMemory{VK_NULL_HANDLE}, m_thicknessMemory{VK_NULL_HANDLE}, m_depthMemory{VK_NULL_HANDLE}, m_smoothMemory{VK_NULL_HANDLE};
    VkDeviceMemory m_smoothAMemory{VK_NULL_HANDLE}, m_colorMemory{VK_NULL_HANDLE};
    VkImageView m_distanceView{VK_NULL_HANDLE}, m_thicknessView{VK_NULL_HANDLE}, m_depthView{VK_NULL_HANDLE}, m_smoothView{VK_NULL_HANDLE};
    VkImageView m_smoothAView{VK_NULL_HANDLE}, m_colorView{VK_NULL_HANDLE};
    VkFramebuffer m_particleFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_smoothFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_smoothAFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_thicknessFramebuffer{VK_NULL_HANDLE};
};
