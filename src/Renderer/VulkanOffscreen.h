#pragma once

#include "IRenderTarget.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vector>

// Per-frame settings for the post-processing chain
struct PostProcessSettings {
    float exposure{1.0f};
    float bloomStrength{0.35f}; // 0 = bloom off
    bool acesToneMapping{true}; // false = Reinhard
    bool fxaa{true};            // edge anti-aliasing on the final image
    // Temporal anti-aliasing: the frame was rendered with a sub-pixel jitter (jitterNdc) and
    // reprojection maps this frame's unjittered NDC + depth to the previous frame's NDC
    bool taa{false};
    glm::vec2 jitterNdc{0.0f};
    glm::mat4 reprojection{1.0f};
    bool velocityValid{false};  // the prepass wrote per-object motion this frame
    float sharpen{0.25f};       // applied with TAA only

    // Auto exposure (the manual exposure above becomes a compensation multiplier)
    bool autoExposure{false};
    float minAutoExposure{0.05f};
    float maxAutoExposure{8.0f};
    float adaptationSpeed{1.5f};
    float deltaTime{0.016f};

    // Colour grading and lens effects
    float contrast{1.0f};
    float saturation{1.0f};
    float temperature{0.0f};    // -1 cool .. +1 warm
    float tint{0.0f};           // -1 green .. +1 magenta
    float vignette{0.0f};
    float filmGrain{0.0f};
    float lensFlare{0.0f};
    float time{0.0f};

    // Depth of field (aperture 0 = off); depthLinearize = (proj[2][2], proj[3][2])
    float aperture{0.0f};
    float focusDistance{10.0f};
    float maxBlurRadius{12.0f};
    glm::vec2 depthLinearize{-1.0f, -0.2f};

    // Motion blur (0 = off): fraction of a frame the shutter is open
    float motionBlur{0.0f};

    // Screen-space reflections (0 = off). Needs this frame's prepass (prepassValid).
    float ssrStrength{0.0f};
    float ssrMaxDistance{20.0f};  // metres a reflection ray travels
    float ssrThickness{0.3f};     // metres behind a surface still counted as a hit
    bool prepassValid{false};
    glm::vec2 projectionScale{1.0f, -1.0f}; // (proj[0][0], proj[1][1])
    uint32_t frameIndex{0};

    // Colour LUT (strength 0 = off; the LUT itself is set with SetColorLUT) and lens aberration
    float lutStrength{0.0f};
    float chromaticAberration{0.0f};

    // The camera is under an ocean's surface: the image wobbles. Half in the water: only the part
    // below the waterline (8 heights across, NDC y; waterlineSide +1 when +y is under water)
    bool underwater{false};
    bool halfUnderwater{false};
    float waterlineSide{1.0f};
    glm::vec4 waterline[2]{};
};

// Screen-space ambient occlusion parameters
struct SSAOSettings {
    float radius{0.5f};  // world units
    float bias{0.025f};  // world units; stops flat surfaces occluding themselves
    float power{1.5f};   // contrast applied to the raw occlusion
};

// Must match the push constant block in shaders/post_common.glsl
struct PostPushConstants {
    glm::vec4 params0{0.0f};
    glm::vec4 params1{0.0f};
    glm::vec4 params2{0.0f};
    glm::mat4 matrix{1.0f};
};

// A viewport render target. The scene is drawn into a 16-bit HDR image (BeginRenderPass /
// EndRenderPass), then ApplyPostProcess() runs bloom and the final composite (exposure,
// tone mapping, gamma) into the 8-bit display image that ImGui shows.
class VulkanOffscreen : public Graphics::IRenderTarget {
public:
    VulkanOffscreen() = default;
    ~VulkanOffscreen() override = default;

    void Init(uint32_t width, uint32_t height);
    void Cleanup();
    void Resize(uint32_t width, uint32_t height) override;

    // Scene pass into the HDR image, in two halves: BeginRenderPass -> opaque drawing ->
    // NextScenePass (copies the opaque image for refraction) -> transparent drawing -> EndRenderPass.
    // EndRenderPass performs the copy itself if NextScenePass wasn't called.
    void BeginRenderPass(VkCommandBuffer cmd);
    void NextScenePass(VkCommandBuffer cmd);
    void EndRenderPass(VkCommandBuffer cmd);
    // During the second half: copies the scene drawn so far (transparents too) for heat haze,
    // and carries on drawing
    void RefreshSceneColor(VkCommandBuffer cmd);

    // Opaque scene copy for refraction, and the finished HDR scene (used as a mirror's reflection)
    VkImageView GetSceneColorImageView() const { return m_sceneColorImageView; }
    VkImageView GetHdrImageView() const { return m_hdrImageView; }
    VkImage GetHdrImage() const { return m_hdrImage; }
    // Copies the HDR scene image (RGBA16F, 4 half floats per pixel) to the CPU. Waits for the GPU.
    void ReadbackHdrImage(std::vector<uint16_t>& outRGBA) const;

    // Colour LUT strip used by the composite pass (see VulkanTexture::LoadColorLUT), or
    // VK_NULL_HANDLE for none. Updates descriptor sets: call outside command buffer recording,
    // with the GPU idle.
    void SetColorLUT(VkImageView view, VkSampler sampler, uint32_t size);
    VkSampler GetLinearSampler() const { return m_sampler; }
    VkSampler GetPointSampler() const { return m_pointSampler; }
    // Opaque depth from the prepass (sampled by water; valid when the prepass ran this frame)
    VkImageView GetPrepassDepthImageView() const { return m_prepassDepthImageView; }
    VkImageView GetPrepassNormalImageView() const { return m_normalImageView; } // view-space normals

    // Bloom + composite into the display image. Call after EndRenderPass every frame.
    void ApplyPostProcess(VkCommandBuffer cmd, const PostProcessSettings& settings);

    // SSAO: depth + view-space normal prepass (draw with VulkanPipeline::RenderNormalDepthPrepass),
    // then ComputeSSAO() before the scene pass. proj must be the projection used for the prepass.
    void BeginPrepass(VkCommandBuffer cmd);
    void EndPrepass(VkCommandBuffer cmd);
    void ComputeSSAO(VkCommandBuffer cmd, const glm::mat4& proj, const SSAOSettings& settings);
    VkRenderPass GetPrepassRenderPass() const { return m_prepassRenderPass; }

    // Editor overlays (grid, axes) drawn onto the final display image after post-processing, so
    // they are not lit, shadowed, jittered by TAA or tone mapped. Depth-tested (read only)
    // against the scene. Call after ApplyPostProcess.
    void BeginOverlayPass(VkCommandBuffer cmd);
    void EndOverlayPass(VkCommandBuffer cmd);
    VkRenderPass GetOverlayRenderPass() const { return m_overlayRenderPass; }
    // Blurred AO (1 = open) for the main shader; changes when the target is resized
    VkImageView GetSSAOImageView() const { return m_aoImageView; }
    VkSampler GetSSAOSampler() const { return m_sampler; }

    // Copies the final display image (RGBA8, top row first) to the CPU. Stalls the GPU;
    // meant for screenshots and automated visual tests, not per-frame use.
    void ReadbackDisplayImage(std::vector<uint8_t>& outRGBA) const;
    // Records a copy of the display image into dst (same size and format), which is left
    // sampleable; dstLayout: dst's layout before the copy (UNDEFINED the first time)
    void CopyDisplayImageTo(VkCommandBuffer cmd, VkImage dst, VkImageLayout dstLayout) const;

    // Render pass for scene pipelines (HDR colour + depth)
    VkRenderPass GetRenderPass() const { return m_sceneRenderPass; }
    VkFramebuffer GetFramebuffer() const { return m_sceneFramebuffer; }
    VkExtent2D GetExtent() const { return { m_width, m_height }; }
    void* GetTextureId() const override { return (void*)m_descriptorSet; }

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kDisplayFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kAOFormat = VK_FORMAT_R8_UNORM;

private:
    VkRenderPass CreateScenePassHalf(bool secondHalf);
    void SetFullViewport(VkCommandBuffer cmd);
    void CopyResolveToHistory(VkCommandBuffer cmd);
    void CreatePrepassRenderPass();
    VkRenderPass CreatePostRenderPass(VkFormat format, bool loadExisting);
    void CreateSampler();
    void CreatePostPipelines();
    VkPipeline CreatePostPipeline(VkRenderPass renderPass, const char* fragSpvPath, bool additiveBlend);
    void CreateResources();
    void CreateFramebuffers();
    void CreatePostDescriptorSets();
    void CleanupResources();

    void DrawFullscreen(VkCommandBuffer cmd, VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent,
                        VkPipeline pipeline, VkDescriptorSet descriptorSet, const PostPushConstants& push);

    uint32_t m_width{800};
    uint32_t m_height{600};

    // Scene pass: HDR colour + depth
    VkRenderPass m_sceneRenderPass{VK_NULL_HANDLE};     // first half (clears)
    VkRenderPass m_sceneLoadRenderPass{VK_NULL_HANDLE}; // second half (loads colour + depth)
    VkFramebuffer m_sceneFramebuffer{VK_NULL_HANDLE};
    bool m_inOpaqueHalf{false};
    void CopySceneColorAndResume(VkCommandBuffer cmd);
    VkImage m_sceneColorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_sceneColorImageMemory{VK_NULL_HANDLE};
    VkImageView m_sceneColorImageView{VK_NULL_HANDLE};
    VkImage m_hdrImage{VK_NULL_HANDLE};
    VkDeviceMemory m_hdrImageMemory{VK_NULL_HANDLE};
    VkImageView m_hdrImageView{VK_NULL_HANDLE};
    VkImage m_depthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_depthImageMemory{VK_NULL_HANDLE};
    VkImageView m_depthImageView{VK_NULL_HANDLE};

    // Bloom chain: one HDR image, mip 0 at half resolution, one view + framebuffer per mip
    static constexpr uint32_t kMaxBloomLevels = 6;
    VkImage m_bloomImage{VK_NULL_HANDLE};
    VkDeviceMemory m_bloomImageMemory{VK_NULL_HANDLE};
    std::vector<VkImageView> m_bloomViews;
    std::vector<VkFramebuffer> m_bloomFramebuffers;
    std::vector<VkExtent2D> m_bloomExtents;

    // SSAO: prepass targets (normals + sampleable depth), raw and blurred AO
    VkRenderPass m_prepassRenderPass{VK_NULL_HANDLE};
    VkFramebuffer m_prepassFramebuffer{VK_NULL_HANDLE};
    VkImage m_normalImage{VK_NULL_HANDLE};
    VkDeviceMemory m_normalImageMemory{VK_NULL_HANDLE};
    VkImageView m_normalImageView{VK_NULL_HANDLE};
    VkImage m_prepassDepthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_prepassDepthImageMemory{VK_NULL_HANDLE};
    VkImageView m_prepassDepthImageView{VK_NULL_HANDLE};
    VkImage m_aoRawImage{VK_NULL_HANDLE};
    VkDeviceMemory m_aoRawImageMemory{VK_NULL_HANDLE};
    VkImageView m_aoRawImageView{VK_NULL_HANDLE};
    VkImage m_aoImage{VK_NULL_HANDLE};
    VkDeviceMemory m_aoImageMemory{VK_NULL_HANDLE};
    VkImageView m_aoImageView{VK_NULL_HANDLE};
    VkFramebuffer m_aoRawFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_aoFramebuffer{VK_NULL_HANDLE};
    VkRenderPass m_aoRenderPass{VK_NULL_HANDLE};
    VkPipeline m_ssaoPipeline{VK_NULL_HANDLE};
    VkPipeline m_ssaoBlurPipeline{VK_NULL_HANDLE};
    VkDescriptorSet m_ssaoSet{VK_NULL_HANDLE};     // depth, normals
    VkDescriptorSet m_ssaoBlurSet{VK_NULL_HANDLE}; // raw AO, depth
    VkSampler m_pointSampler{VK_NULL_HANDLE};      // depth can't be linearly filtered on every GPU

    // Display image (tone-mapped, shown by ImGui)
    VkImage m_colorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_colorImageMemory{VK_NULL_HANDLE};
    VkImageView m_colorImageView{VK_NULL_HANDLE};
    VkFramebuffer m_compositeFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_overlayFramebuffer{VK_NULL_HANDLE};

    // FXAA: the composite writes here first, then FXAA writes the display image
    VkImage m_ldrImage{VK_NULL_HANDLE};
    VkDeviceMemory m_ldrImageMemory{VK_NULL_HANDLE};
    VkImageView m_ldrImageView{VK_NULL_HANDLE};
    VkFramebuffer m_compositeToLdrFramebuffer{VK_NULL_HANDLE};
    VkPipeline m_fxaaPipeline{VK_NULL_HANDLE};
    VkDescriptorSet m_fxaaSet{VK_NULL_HANDLE};

    // TAA: resolve (this frame) and history (last frame's resolve), both HDR
    VkImage m_taaResolveImage{VK_NULL_HANDLE};
    VkDeviceMemory m_taaResolveImageMemory{VK_NULL_HANDLE};
    VkImageView m_taaResolveImageView{VK_NULL_HANDLE};
    VkImage m_taaHistoryImage{VK_NULL_HANDLE};
    VkDeviceMemory m_taaHistoryImageMemory{VK_NULL_HANDLE};
    VkImageView m_taaHistoryImageView{VK_NULL_HANDLE};
    VkFramebuffer m_taaFramebuffer{VK_NULL_HANDLE};
    VkPipeline m_taaPipeline{VK_NULL_HANDLE};
    VkDescriptorSet m_taaSet{VK_NULL_HANDLE}; // current HDR, history, scene depth
    bool m_taaHistoryValid{false};

    // Prepass motion (TAA, motion blur)
    VkImage m_velocityImage{VK_NULL_HANDLE};
    VkDeviceMemory m_velocityImageMemory{VK_NULL_HANDLE};
    VkImageView m_velocityImageView{VK_NULL_HANDLE};

    // Screen-space reflections (scene + reflections, read by TAA)
    VkImage m_ssrImage{VK_NULL_HANDLE};
    VkDeviceMemory m_ssrImageMemory{VK_NULL_HANDLE};
    VkImageView m_ssrImageView{VK_NULL_HANDLE};
    VkFramebuffer m_ssrFramebuffer{VK_NULL_HANDLE};
    VkPipeline m_ssrPipeline{VK_NULL_HANDLE};
    VkDescriptorSet m_ssrSet{VK_NULL_HANDLE};

    // Colour LUT (composite binding 3); null view = none bound
    VkImageView m_lutView{VK_NULL_HANDLE};
    VkSampler m_lutSampler{VK_NULL_HANDLE};
    uint32_t m_lutSize{0};

    // Depth of field and motion blur
    VkImage m_dofImage{VK_NULL_HANDLE};
    VkDeviceMemory m_dofImageMemory{VK_NULL_HANDLE};
    VkImageView m_dofImageView{VK_NULL_HANDLE};
    VkImage m_motionBlurImage{VK_NULL_HANDLE};
    VkDeviceMemory m_motionBlurImageMemory{VK_NULL_HANDLE};
    VkImageView m_motionBlurImageView{VK_NULL_HANDLE};
    VkFramebuffer m_dofFramebuffer{VK_NULL_HANDLE};
    VkFramebuffer m_motionBlurFramebuffer{VK_NULL_HANDLE};
    VkPipeline m_dofPipeline{VK_NULL_HANDLE};
    VkPipeline m_motionBlurPipeline{VK_NULL_HANDLE};
    VkDescriptorSet m_dofSet{VK_NULL_HANDLE};
    VkDescriptorSet m_motionBlurSet{VK_NULL_HANDLE};

    // Auto exposure: two 1x1 images, written alternately (each reads the other as last frame's)
    VkImage m_exposureImages[2]{};
    VkDeviceMemory m_exposureImageMemory[2]{};
    VkImageView m_exposureImageViews[2]{};
    VkFramebuffer m_exposureFramebuffers[2]{};
    VkDescriptorSet m_exposureSets[2]{};
    VkDescriptorSet m_compositeSets[2]{}; // [i] reads exposure image i
    VkPipeline m_exposurePipeline{VK_NULL_HANDLE};
    int m_exposureIndex{0};
    bool m_exposureValid{false};

    // Post-processing passes and pipelines (created once; independent of size)
    VkRenderPass m_bloomDownRenderPass{VK_NULL_HANDLE}; // overwrites the target mip
    VkRenderPass m_bloomUpRenderPass{VK_NULL_HANDLE};   // keeps the target mip and adds to it
    VkRenderPass m_compositeRenderPass{VK_NULL_HANDLE};
    VkRenderPass m_overlayRenderPass{VK_NULL_HANDLE};
    void CreateOverlayRenderPass();
    VkDescriptorSetLayout m_postSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_postPipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_bloomDownPipeline{VK_NULL_HANDLE};
    VkPipeline m_bloomUpPipeline{VK_NULL_HANDLE};
    VkPipeline m_compositePipeline{VK_NULL_HANDLE};

    // Descriptor sets for the current size (rebuilt on resize)
    VkDescriptorPool m_postDescriptorPool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> m_bloomDownSets; // [i] samples the scene (i = 0) or bloom mip i-1
    std::vector<VkDescriptorSet> m_bloomUpSets;   // [i] samples bloom mip i+1
    static constexpr uint32_t kPostBindings = 4;

    // Linear clamp sampler & ImGui descriptor set for the display image
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
};
