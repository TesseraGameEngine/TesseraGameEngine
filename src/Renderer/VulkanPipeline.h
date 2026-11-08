#pragma once

#include "RenderCommon.h"
#include <vulkan/vulkan.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <future>
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanShadowMap.h"
#include "VulkanReflectionProbes.h"
#include "VulkanSkinning.h"
#include "VulkanVolumeTexture.h"
#include "VulkanLightmapTexture.h"
#include "Scene/Scene.h"
#include "VulkanSkyResources.h"

struct FoliageType;
struct ParticleQuad;
enum class ParticleSheet;

using Graphics::PolygonMode;

// Which part of the scene RenderScene() draws. The scene pass is split around a copy of
// the opaque image (VulkanOffscreen::NextScenePass) so transparent surfaces can refract it.
enum class ScenePhase {
    All,         // sky, opaque, transparent, particles (single pass, no refraction source)
    Opaque,      // sky, opaque surfaces, editor collider wireframes
    Transparent  // sorted transparent surfaces and particles
};

class VulkanPipeline {
public:
    // View slots: each has its own UBO and descriptor sets. Mirror and probe-capture views render
    // for the editor / game camera and share that view's shadow maps and reflection probes (slot % 2).
    static constexpr int kViewEditor = 0;
    static constexpr int kViewPreview = 1;
    static constexpr int kViewEditorMirror = 2;
    static constexpr int kViewPreviewMirror = 3;
    static constexpr int kViewEditorProbe = 4;
    static constexpr int kViewPreviewProbe = 5;
    static constexpr int kViewMaterialPreview = 6; // material preview spheres (editor)
    static constexpr int kViewCount = 7;
    static constexpr int MirrorSlotFor(int viewSlot) { return viewSlot + 2; }
    static constexpr int ProbeSlotFor(int viewSlot) { return viewSlot + 4; }

    // Per-view textures shared by every material set of that view
    static constexpr uint32_t kSSAOBinding = 8;
    static constexpr uint32_t kSceneDepthBinding = 24; // opaque depth from the view's prepass (water)
    // Screen-space fluid (VulkanFluidTargets): sphere surfaces, smoothed surface, thickness
    static constexpr uint32_t kFluidDistanceBinding = 25;
    static constexpr uint32_t kFluidSurfaceBinding = 26;
    static constexpr uint32_t kFluidThicknessBinding = 27;
    static constexpr uint32_t kFluidColorBinding = 32;   // nearest particle's body colour (rgb) and colour depth (a)
    static constexpr uint32_t kFluidFirstSmoothBinding = 33; // the first smoothing pass, read by the second
    static constexpr uint32_t kTerrainSplatBinding = 34;  // terrain: splat map; 35-38: its four layer textures
    static constexpr uint32_t kSceneNormalBinding = 39;   // the view's prepass normals (decals)
    static constexpr uint32_t kSceneColorBinding = 9;  // opaque scene copy for refraction
    static constexpr uint32_t kMirrorBinding = 10;     // planar reflection
    static constexpr uint32_t kEnvironmentBinding = 12; // HDRI environment (same for every view)

    VulkanPipeline() = default;
    ~VulkanPipeline() = default;

    // prepassRenderPass: the viewports' depth + normal prepass (VulkanOffscreen::GetPrepassRenderPass)
    void Init(VkRenderPass renderPass, VkRenderPass prepassRenderPass,
              const std::string& vertShaderPath, const std::string& fragShaderPath);
    void Cleanup();

    void UpdateUniformBuffer(const UniformBufferObject& ubo);
    void UpdateUniformBufferPreview(const UniformBufferObject& ubo);
    void UpdateUniformBufferSlot(int viewSlot, const UniformBufferObject& ubo);

    // viewSlot: kViewEditor / kViewPreview / mirror slots (true/false still select preview/editor)
    void RenderScene(VkCommandBuffer cmd, const Scene& scene,
                     const std::map<ShapeType, GPUMesh>& meshes,
                     const std::map<std::string, GPUMesh>& customMeshes,
                     PolygonMode mode = PolygonMode::Solid,
                     bool renderGizmoNodes = true,
                     int viewSlot = kViewEditor,
                     const glm::vec3& cameraPos = glm::vec3(0.0f),
                     ScenePhase phase = ScenePhase::All);
    void RenderGrid(VkCommandBuffer cmd, const GPUMesh& gridMesh, const GPUMesh& axesMesh, bool isPreview = false);
    // Just the sky background (procedural or HDRI) for viewSlot's uploaded UBO; nothing when it's off
    void RenderSky(VkCommandBuffer cmd, int viewSlot);

    // Ocean spray droplets (OceanWaves::Spray), drawn as soft sprites with the particles
    struct SpraySprite {
        glm::vec3 position;
        float size;
        glm::vec4 color;
        glm::vec3 stretch{0.0f}; // world-space streak along the motion (zero = round)
        float frame{-1.0f};      // >= 0: an animated splash at that point of its life (0..1)
        float seed{0.0f};        // varies each splash's shape
    };
    void SetSprayParticles(std::vector<SpraySprite> sprites) { m_spraySprites = std::move(sprites); }

    // Particle Emitters in GPU mode (VulkanParticleSim): each one's quads, by node id, set each frame
    struct GpuParticleDraw {
        VkBuffer quads;
        uint32_t count;   // all quads
        uint32_t sprites; // the first ones: one per particle (mesh particles' instances)
    };
    void SetGpuParticles(std::map<uint32_t, GpuParticleDraw> draws) { m_gpuParticleDraws = std::move(draws); }
    // Heat haze (Distortion particles), main views only: after the rest of the scene, once the view's
    // offscreen has copied it (VulkanOffscreen::RefreshSceneColor), in the transparent pass
    bool HasDistortionParticles(const Scene& scene) const;
    void DrawDistortionParticles(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const glm::vec3& cameraPos);
    // Whether a main view's prepass ran this frame (soft particles fade against its depth)
    void SetViewHasSceneDepth(int viewSlot, bool hasDepth) { if (viewSlot >= 0 && viewSlot < 2) m_viewHasSceneDepth[viewSlot] = hasDepth; }

    // Pour streams (Liquids::Simulation): refractive water segments drawn with the transparents
    struct LiquidStreamSegment {
        glm::vec3 start;
        glm::vec3 end;
        float radius;
        glm::vec4 color;
    };
    void SetLiquidStreams(std::vector<LiquidStreamSegment> segments) { m_liquidStreams = std::move(segments); }

    // Real fluid (VulkanFluidSim), drawn by screen-space fluid rendering: each frame set the
    // bodies (their GPU particle buffers, drawn as they are), then per main view, after its prepass:
    // VulkanFluidTargets::BeginParticles -> DrawFluidParticles -> End, the same for
    // BeginThickness, then BeginSmooth(0) -> DrawFluidSmooth(0) -> End and the same for pass 1.
    // RenderScene shades the surface, then the foam and spray, in the transparent phase.
    struct FluidBody {
        VkBuffer particles;  // vec4 per particle: xyz = position, w = radius (< 0 spray, 0 unused slot)
        VkBuffer velocities; // vec4 per particle: xyz = velocity, w = foam
        uint32_t count;
        glm::vec4 color;
        float colorDepth;
        float radius;
    };
    void SetFluidBodies(std::vector<FluidBody> bodies);
    // A texture made from pixels (an editor-painted map), named by a key starting with '@' that
    // material sets can use like a path. Created on first use, updated in place after.
    void SetRuntimeTexture(const std::string& key, const unsigned char* rgba, uint32_t width, uint32_t height);
    bool HasFluids() const { return !m_fluidBodies.empty(); }
    void CreateFluidPipelines(VkRenderPass particlePass, VkRenderPass thicknessPass, VkRenderPass smoothPass);
    // thickness: the thickness pass instead of the distance pass
    void DrawFluidParticles(VkCommandBuffer cmd, int viewSlot, bool thickness);
    // pass 0: the particle distances -> first smoothing; pass 1: a wider smoothing of that
    void DrawFluidSmooth(VkCommandBuffer cmd, int viewSlot, int pass);

    // Material texture (block-compressed per VulkanTexture's quality setting); normalMap: BC5
    std::shared_ptr<VulkanTexture> GetOrLoadTexture(const std::string& path, bool normalMap = false);

    // Points a view's material descriptor sets at one of its per-view textures (kSSAOBinding,
    // kSceneColorBinding or kMirrorBinding). Call after Init and whenever the image is recreated;
    // must not be called while that view's frame is in flight.
    void SetViewTexture(int viewSlot, uint32_t binding, VkImageView view, VkSampler sampler);
    void SetViewportSSAOTexture(bool isPreview, VkImageView view, VkSampler sampler) {
        SetViewTexture(isPreview ? kViewPreview : kViewEditor, kSSAOBinding, view, sampler);
    }

    // Uses an equirectangular .hdr image as the sky and environment lighting, or goes back to the
    // procedural sky when path is empty. Reloads only when the path changes. Waits for the GPU to
    // be idle before swapping, so call it outside command buffer recording. Returns false if the
    // file couldn't be loaded.
    bool SetEnvironmentHDRI(const std::string& path);
    bool HasEnvironmentHDRI() const { return m_environmentTexture != nullptr; }

    // Baked GI irradiance volume (see VulkanVolumeTexture), shown in every view whose UBO
    // enables it. Waits for the GPU: call outside command buffer recording.
    void SetBakedGIVolume(const glm::uvec3& dims, const std::vector<glm::vec4>& red,
                          const std::vector<glm::vec4>& green, const std::vector<glm::vec4>& blue);
    void RecordVXGIUpload(VkCommandBuffer cmd, const std::vector<std::vector<glm::vec4>>& mips) {
        m_vxgiVolume.RecordUpload(cmd, m_frameIndex, mips);
    }

    // Baked lightmaps (LightmapBaker): the shared atlas, and each lightmapped node's own copy of
    // its mesh with lightmap UVs (the pipeline owns and frees it). A node draws with its copy, and
    // takes its indirect light from the atlas, while it still uses the mesh the copy was made from
    // (LightmapSourceKey). These wait for the GPU: call outside command buffer recording.
    void SetLightmapAtlas(uint32_t width, uint32_t height, const std::vector<uint32_t>& texels);
    void SetLightmapMesh(uint32_t nodeId, const std::string& sourceKey, const GPUMesh& mesh);
    void ClearLightmaps();
    bool HasLightmaps() const { return !m_lightmapMeshes.empty(); }
    // Identifies the mesh a node draws: "shape:<n>" for primitives, the model key otherwise
    static std::string LightmapSourceKey(const Node& node);

    // Opaque pipeline for a node's shader graph material (Node::shaderGraphPath), or
    // VK_NULL_HANDLE if it can't be built (then the node uses its standard material). The graph
    // is compiled into a variant of shader.frag with glslc the first time it's used (or after the
    // file changes) and cached in cache/shadergraphs/ by content, so later runs and exported
    // games can load the SPIR-V without glslc.
    VkPipeline GetShaderGraphPipeline(const std::string& graphPath);

    // Grid and axes pipeline for VulkanOffscreen's overlay pass (call once, after Init)
    void CreateOverlayPipeline(VkRenderPass overlayRenderPass);

    // GPU skinning: animated characters are drawn from their skinned buffers (set once at startup)
    void SetSkinning(VulkanSkinning* skinning) { m_skinning = skinning; }

    // Reflection probe storage for a main view (kViewEditor / kViewPreview)
    VulkanReflectionProbes& GetReflectionProbes(int viewSlot) { return m_reflectionProbes[viewSlot % 2]; }

    // Builds the reflection camera for the nearest visible mirror in viewSlot's view (whose
    // UBO must already be uploaded). Returns false when no mirror needs rendering.
    bool PrepareMirrorView(const Scene& scene, int viewSlot, UniformBufferObject& outMirrorUbo) const;

    // Depth + view-space normal + motion prepass of the opaque scene (SSAO, TAA, motion blur). Call
    // inside the viewport's prepass render pass, after UpdateUniformBuffer*() for that viewport.
    // Per-object motion uses each node's model matrix from the previous prepass in this view.
    void RenderNormalDepthPrepass(VkCommandBuffer cmd, const Scene& scene,
                                  const std::map<ShapeType, GPUMesh>& meshes,
                                  const std::map<std::string, GPUMesh>& customMeshes,
                                  int viewSlot);

    // Renders the shadow map for one viewport and fills ubo.shadowMatrix / ubo.shadowParams.
    // Call before UpdateUniformBuffer*() and outside any render pass.
    // The first shadow-casting directional light wins; otherwise the first shadow-casting spot light.
    void RenderShadowPass(VkCommandBuffer cmd, const Scene& scene,
                          const std::map<ShapeType, GPUMesh>& meshes,
                          const std::map<std::string, GPUMesh>& customMeshes,
                          int viewSlot, float shadowDistance,
                          UniformBufferObject& ubo);

    VkPipeline GetPipeline() const { return m_graphicsPipeline; }
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }

    // Selects which per-frame UBO region the following updates and draws use. Call once per frame,
    // before any UpdateUniformBuffer*(), with the frame-in-flight index (0 .. 1).
    void BeginFrame(uint32_t frameInFlight);

    // A main view's lights (kViewEditor / kViewPreview; mirror and probe views use their parent's).
    // Call before that view's RenderShadowPass and UBO uploads. Each UBO upload then also uploads
    // the lights and rebuilds the view's clustered light lists for that UBO's camera.
    void SetViewLights(int viewSlot, const std::vector<LightData>& lights);

    // Hardware ray tracing. The layout of descriptor set 1 (VulkanRayTracing) must be given
    // before Init so the scene pipeline layout includes it; ray-traced variants of the forward
    // pipelines (frag_rt.spv) are then created. Each frame, SetViewRayTracing gives a view the
    // set to trace against (VK_NULL_HANDLE = draw that view with the rasterised fallback).
    void SetRayTracingSetLayout(VkDescriptorSetLayout layout) { m_rtSetLayout = layout; }
    void SetViewRayTracing(int viewSlot, VkDescriptorSet set);
    bool HasRayTracingPipelines() const { return m_graphicsPipelineRT != VK_NULL_HANDLE; }

    // Occlusion culling for the main views (kViewEditor / kViewPreview). Each frame the bounding
    // box of every visible opaque object is tested against the depth buffer with a hardware
    // query; objects whose box was completely hidden in the results from two frames ago are
    // skipped (their boxes are still tested, so they come back when uncovered). Results are
    // ignored while the camera moves fast. Call BeginOcclusionFrame once per frame, outside any
    // render pass, after BeginFrame.
    void SetOcclusionCulling(int viewSlot, bool enabled) { m_occlusion[viewSlot % 2].enabled = enabled; }
    void BeginOcclusionFrame(VkCommandBuffer cmd);
    struct OcclusionStats {
        uint32_t tested{0}; // boxes in the latest results
        uint32_t hidden{0}; // of those, completely hidden
    };
    OcclusionStats GetOcclusionStats(int viewSlot) const { return m_occlusion[viewSlot % 2].stats; }

    // Must match InstanceData in instancing_common.glsl
    struct InstanceData {
        glm::mat4 model;
        glm::mat4 prevModel;
    };

private:
    void CreateDescriptorSetLayout();
    void CreateUniformBuffer();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreateGraphicsPipeline(VkRenderPass renderPass, const std::string& vertShaderPath, const std::string& fragShaderPath);

    // Binding 0 = UBO, bindings 1-6 = Albedo, Normal, Metallic, Roughness, AO, Emissive,
    // binding 7 = shadow map (sampler2DShadow), bindings 8-10 = per-view textures
    static constexpr uint32_t kMaterialBindingCount = 42;
    static constexpr uint32_t kHeightMapBinding = 41;    // height / displacement map for Parallax Occlusion Mapping (POM)
    static constexpr uint32_t kVXGIVolumeBinding = 40;   // real-time VXGI 3D radiance + opacity volume (mipmapped)
    static constexpr uint32_t kShelterBinding = 28;      // weather shelter map (top-down depth, vertex + fragment)
    static constexpr uint32_t kSkyViewLutBinding = 29;   // physical atmosphere sky-view LUT (per main view)
    static constexpr uint32_t kCloudDetailBinding = 30;  // volumetric cloud 3D detail noise
    static constexpr uint32_t kCloudCoverageBinding = 31; // cloud coverage map
    static constexpr uint32_t kLightmapBinding = 23;     // baked lightmap atlas
    static constexpr uint32_t kGIVolumeBinding = 20;     // 20-22: baked GI volume (R, G, B SH textures)
    static constexpr uint32_t kMaterialExtBinding = 17;  // advanced materials (storage buffer, all frames)
    static constexpr uint32_t kDetailAlbedoBinding = 18;
    static constexpr uint32_t kDetailNormalBinding = 19;
    // Storage buffers (dynamic offsets select this frame's / this view's region)
    static constexpr uint32_t kInstanceBinding = 14;     // instance transforms (vertex stage)
    static constexpr uint32_t kLightClusterBinding = 15; // clustered light lists
    static constexpr uint32_t kLightBinding = 16;        // lights
    static bool IsStorageBinding(uint32_t b) { return b >= kInstanceBinding && b <= kLightBinding; } // dynamic offsets
    static constexpr uint32_t kMaxMaterialExts = 4096; // distinct advanced materials per frame
    // Must match MaterialExt in material_ext.glsl
    struct MaterialExtData {
        glm::vec4 params0{0.0f};
        glm::vec4 subsurface{0.0f};
        glm::vec4 params1{0.0f};
        glm::vec4 params2{0.0f};
        glm::vec4 liquidPlane{0.0f};
        glm::vec4 liquidTop{0.0f};
        glm::vec4 liquidBoundsMin{0.0f}; // xyz = world box min, w = 1 for a cylinder (0 = box)
        glm::vec4 liquidBoundsMax{0.0f}; // xyz = world box max, w = colour depth (m)
        glm::vec4 liquidRipple{0.0f};    // xyz = where a stream lands, w = ripple strength
        glm::vec4 terrainParams{0.0f};   // terrain: x = UV tiling (splat UV = mesh UV / x), yz = size (m), w = layers with textures (bits)
        glm::vec4 terrainTiling{0.0f};   // terrain: metres per repeat of each layer
        glm::vec4 terrainTint[4]{};      // terrain: rgb = each layer's tint, a = its roughness
    };
    // Entry for a node's advanced material this frame, as stored in materialFlags (index + 1 in
    // bits 8 and up), or 0 for a standard material. Identical materials share an entry.
    int MaterialExtFlags(const Node& node);
    // Binding of each material texture slot (albedo, normal, metallic, roughness, AO, emissive, detail albedo, detail normal)
    static constexpr uint32_t kMaterialTextureBindings[] = { 1, 2, 3, 4, 5, 6, kDetailAlbedoBinding, kDetailNormalBinding,
                                                             kTerrainSplatBinding, kTerrainSplatBinding + 1, kTerrainSplatBinding + 2,
                                                             kTerrainSplatBinding + 3, kTerrainSplatBinding + 4, kHeightMapBinding };

    // Clustered lighting; must match lights_common.glsl
    static constexpr uint32_t kClusterTilesX = 16;
    static constexpr uint32_t kClusterTilesY = 9;
    static constexpr uint32_t kClusterSlices = 24;
    static constexpr uint32_t kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;
    static constexpr uint32_t kMaxDirectionalLights = 32;
    static constexpr uint32_t kMaxLightIndices = 65536;  // cluster list entries per view
    static constexpr float kClusterNear = 0.1f;          // depth range split into slices (log scale)
    static constexpr float kClusterFar = 1000.0f;
    static constexpr uint32_t kMaxInstancesPerFrame = 131072; // foliage needs plenty
    static constexpr uint32_t kMaterialTextureCount = 14;
    static constexpr uint32_t kShadowMapBinding = 7;
    static constexpr uint32_t kLocalShadowBinding = 11; // point / spot light shadow layers
    static constexpr uint32_t kProbeBinding = 13;       // reflection probes of the parent view
    // Per-view texture bindings, in the order of m_viewTextureViews' first index
    static constexpr uint32_t kViewTextureBindings[] = { kSSAOBinding, kSceneColorBinding, kMirrorBinding, kEnvironmentBinding,
                                                         kGIVolumeBinding, kGIVolumeBinding + 1, kGIVolumeBinding + 2,
                                                         kLightmapBinding, kSceneDepthBinding,
                                                         kFluidDistanceBinding, kFluidSurfaceBinding, kFluidThicknessBinding,
                                                         kFluidColorBinding, kFluidFirstSmoothBinding, kSceneNormalBinding,
                                                         kVXGIVolumeBinding };
    static constexpr uint32_t kViewTextureCount = 16;
    static int ViewTextureIndex(uint32_t binding) {
        for (uint32_t t = 0; t < kViewTextureCount; ++t) if (kViewTextureBindings[t] == binding) return static_cast<int>(t);
        return -1;
    }

    VkDescriptorSet GetOrCreateDescriptorSet(
        int uboIndex,
        const std::string& albedoPath,
        const std::string& normalPath = "",
        const std::string& metallicPath = "",
        const std::string& roughnessPath = "",
        const std::string& aoPath = "",
        const std::string& emissivePath = "",
        const std::string& detailAlbedoPath = "",
        const std::string& detailNormalPath = "",
        const std::string* terrainTextures = nullptr, // splat key, then the four layer textures
        const std::string& heightPath = ""
    );
    void WriteMaterialDescriptorSet(VkDescriptorSet set, int uboIndex,
                                    const std::shared_ptr<VulkanTexture> (&textures)[kMaterialTextureCount]);

    // True when the node's world-space bounds are entirely outside the view frustum
    bool IsOutsideFrustum(const Node& node, const Scene& scene, const glm::mat4& viewProj) const;

    // GPU mesh a node draws with (LOD-aware for custom meshes); nullptr when it has none
    const GPUMesh* ResolveNodeMesh(const Node& node,
                                   const std::map<ShapeType, GPUMesh>& meshes,
                                   const std::map<std::string, GPUMesh>& customMeshes,
                                   float distToCam) const;

    void CreateShadowPipeline(const std::string& shadowVertShaderPath);
    // Fullscreen procedural sky drawn first in each viewport; uses the main pipeline layout
    void CreateSkyPipeline(VkRenderPass renderPass, const std::string& skyVertShaderPath, const std::string& skyFragShaderPath);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);

    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipelineWireframe{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipelineLines{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipelineParticlesAlpha{VK_NULL_HANDLE};
    // Sorted transparent surfaces: depth test on, depth write off, premultiplied alpha
    VkPipeline m_graphicsPipelineTransparent{VK_NULL_HANDLE};
    // Ray-traced variants (same state, frag_rt.spv) and set 1
    VkPipeline m_graphicsPipelineRT{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipelineTransparentRT{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_rtSetLayout{VK_NULL_HANDLE};       // owned by VulkanRayTracing
    VkDescriptorSetLayout m_emptySetLayout{VK_NULL_HANDLE};    // set 1 placeholder without ray tracing
    VkDescriptorSet m_viewRTSets[kViewCount]{};
    int m_activeRTView{-1};                                     // view RenderScene is drawing with RT
    // Binds a forward-shaded pipeline for the view being rendered: its ray-traced variant (and
    // set 1) when that view traces rays this frame
    void BindForwardPipeline(VkCommandBuffer cmd, VkPipeline pipeline) const;
    UniformBufferObject m_lastUBO[kViewCount]{};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    // Indexed by view slot
    VkDescriptorSet m_descriptorSets[kViewCount]{};
    // Each view's UBO holds kFramesInFlight regions; frame N writes and binds region N % kFramesInFlight
    static constexpr uint32_t kFramesInFlight = 2;
    VkDeviceSize m_uboStride{0};
    uint32_t m_frameIndex{0};
    uint32_t CurrentUboOffset() const { return static_cast<uint32_t>(m_uboStride * m_frameIndex); }
    void BindMaterialSet(VkCommandBuffer cmd, VkPipelineLayout layout, VkDescriptorSet set) const;

    // Per-frame storage: instance transforms (one region per frame), and lights + cluster lists
    // (one region per frame per view slot)
    void CreateFrameStorage();
    void DestroyFrameStorage();
    // Copies instances into this frame's region; returns the first instance index, or UINT32_MAX when full
    uint32_t WriteInstances(const InstanceData* data, uint32_t count);
    void BuildLightClusters(int viewSlot, const UniformBufferObject& ubo);
    struct StorageRegion {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        char* mapped{nullptr};
        VkDeviceSize regionSize{0};
    };
    StorageRegion m_instanceStorage;
    StorageRegion m_lightStorage;
    StorageRegion m_clusterStorage;
    StorageRegion m_materialExtStorage; // one region per frame (entries indexed across regions)
    std::unordered_map<std::string, uint32_t> m_materialExtIndices; // this frame's entries
    uint32_t m_instanceCursor{0};
    std::vector<LightData> m_viewLights[3]; // editor, game (with their mirrors and probes), material previews
    static int LightSetFor(int viewSlot) { return viewSlot == kViewMaterialPreview ? 2 : viewSlot % 2; }
    std::vector<uint32_t> m_clusterScratch;
    std::unordered_map<VkDescriptorSet, int> m_setSlots; // view slot each material set was written for

    // Occlusion culling (see SetOcclusionCulling)
    static constexpr uint32_t kMaxOcclusionQueries = 8192; // per view per frame
    struct OcclusionView {
        bool enabled{false};
        VkQueryPool pools[kFramesInFlight]{};
        std::vector<const Node*> issued[kFramesInFlight]; // query i tested issued[frame][i]
        glm::vec3 issuedCameraPos[kFramesInFlight]{};
        glm::vec3 issuedCameraForward[kFramesInFlight]{};
        std::unordered_set<const Node*> hidden;           // from the latest results
        glm::vec3 resultCameraPos{0.0f};
        glm::vec3 resultCameraForward{0.0f, 0.0f, -1.0f};
        OcclusionStats stats;
    };
    OcclusionView m_occlusion[2];
    VkPipeline m_occlusionPipeline{VK_NULL_HANDLE};
    VkPipeline m_overlayPipeline{VK_NULL_HANDLE}; // editor grid / axes (overlay pass)

    // The solid scene pipeline's fixed-function state, kept so shader graph pipelines can be
    // built later with only the fragment shader swapped
    struct ScenePipelineTemplate {
        VkVertexInputBindingDescription binding{};
        std::vector<VkVertexInputAttributeDescription> attributes;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        VkPipelineViewportStateCreateInfo viewportState{};
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        VkPipelineMultisampleStateCreateInfo multisampling{};
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        VkPipelineColorBlendAttachmentState blendAttachment{};
        VkRenderPass renderPass{VK_NULL_HANDLE};
        std::string vertShaderPath;
    };
    ScenePipelineTemplate m_sceneTemplate;
    struct ShaderGraphPipeline {
        VkPipeline pipeline{VK_NULL_HANDLE};
        long long fileStamp{0};
        bool failed{false};
    };
    std::map<std::string, ShaderGraphPipeline> m_shaderGraphPipelines;
    VkPipeline BuildScenePipeline(const std::vector<char>& fragmentSpirv);
    void CreateOcclusionResources();
    void DestroyOcclusionResources();
    // Local-space box used for culling (rest pose box, padded for animated meshes)
    void GetLocalBounds(const Node& node, glm::vec3& outMin, glm::vec3& outMax) const;
    // Whether this view can use its occlusion results now (enabled, camera close to where they were taken)
    bool OcclusionResultsUsable(int viewSlot) const;
    void IssueOcclusionQueries(VkCommandBuffer cmd, int viewSlot, const Scene& scene,
                               const std::vector<std::shared_ptr<Node>>& nodes,
                               const std::map<ShapeType, GPUMesh>& meshes, const glm::mat4& viewProj);
    VkBuffer m_uniformBuffers[kViewCount]{};
    VkDeviceMemory m_uniformBuffersMemory[kViewCount]{};
    void* m_uniformBuffersMapped[kViewCount]{};

    // Textures & Texture descriptor sets
    std::shared_ptr<VulkanTexture> m_defaultTexture;
    std::shared_ptr<VulkanTexture> m_defaultNormalTexture;
    std::map<std::string, std::shared_ptr<VulkanTexture>> m_textures;
    std::map<std::string, std::shared_ptr<VulkanTexture>> m_runtimeTextures; // '@' keys (SetRuntimeTexture)
    std::map<std::string, VkDescriptorSet> m_textureDescriptorSets;

    // Shadow mapping: index 0 = Editor Viewport, index 1 = Camera Preview.
    // Mirror views use their parent view's map (slot % 2).
    VulkanShadowMap m_shadowMaps[2];
    // Point / spot light shadows: up to kMaxLocalShadowLights lights sharing kLocalShadowLayers
    // layers (a spot light takes 1, a point light 6). Index as m_shadowMaps.
    VulkanShadowMap m_localShadowMaps[2];
    // Weather shelter: the topmost surface around the camera, seen from straight above (1 layer).
    // Keeps rain, wetness and snow out from under roofs and gives splashes and snow depth their
    // ground height. Index as m_shadowMaps; other views reuse their parent view's map.
    VulkanShadowMap m_shelterMaps[2];
    VulkanSkyResources m_sky;
    glm::mat4 m_shelterMatrix[2]{glm::mat4(1.0f), glm::mat4(1.0f)};
    glm::vec4 m_shelterParams[2]{};
    static constexpr uint32_t kShelterResolution = 1024;
    static constexpr float kShelterSize = 64.0f;   // metres across
    static constexpr float kShelterRange = 400.0f; // metres from the top of the map to its bottom
    VulkanReflectionProbes m_reflectionProbes[2];
    VulkanSkinning* m_skinning{nullptr};
    VulkanVolumeTexture m_giVolume;
    VulkanVXGIVolume m_vxgiVolume;
    VulkanLightmapTexture m_lightmapAtlas;
    // Ocean: its own pipeline (depth-writing, unblended, in the transparent pass) and a radial
    // grid around the camera that ocean.vert moves with the waves
    VkPipeline m_oceanPipeline{VK_NULL_HANDLE};
    std::vector<SpraySprite> m_spraySprites;
    std::vector<LiquidStreamSegment> m_liquidStreams;
    std::vector<FluidBody> m_fluidBodies;
    VkPipeline m_fluidDepthPipeline{VK_NULL_HANDLE};
    VkPipeline m_fluidThicknessPipeline{VK_NULL_HANDLE};
    VkPipeline m_fluidSmoothPipeline{VK_NULL_HANDLE};
    VkPipeline m_fluidCompositePipeline{VK_NULL_HANDLE};
    void DrawFluidComposite(VkCommandBuffer cmd, int viewSlot);
    // Rain and snow around the camera (precip.vert/.frag), in the transparent phase
    VkPipeline m_precipPipeline{VK_NULL_HANDLE};
    void DrawPrecipitation(VkCommandBuffer cmd, int viewSlot, const SceneEnvironment& env);
    // instanceStreams: per-particle vec4 vertex streams (0 none, 1 positions, 2 positions and velocities);
    // 3: mesh vertices (position only), for decal boxes; 4: particle quads (ParticleQuad per instance);
    // 5: mesh vertices (position, normal) with particle quads as instances
    VkPipeline m_fluidFoamPipeline{VK_NULL_HANDLE};
    VkPipeline m_fluidSmoothWidePipeline{VK_NULL_HANDLE}; // the second smoothing pass
    // Projected decals (decal.vert/.frag): paint (alpha), stain (multiply) and glow (additive)
    VkPipeline m_decalPipelines[3]{};
    // Particle quads (particle.vert/.frag): additive, alpha (premultiplied) and heat haze
    VkPipeline m_particlePipelines[3]{};
    VkPipeline m_particleMeshPipeline{VK_NULL_HANDLE}; // spheres and cubes (particle_mesh.vert/.frag)
    void DrawParticles(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const glm::vec3& cameraPos,
                       const std::map<ShapeType, GPUMesh>& meshes, bool distortionOnly = false);
    // The built-in sprite sheet's runtime texture key (generated on first use)
    std::string ParticleSheetTexture(ParticleSheet sheet);
    // CPU particle quads go into a host-visible buffer per frame in flight, grown as needed
    struct ParticleUpload {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        void* mapped{nullptr};
        VkDeviceSize capacity{0};
    };
    ParticleUpload m_particleUpload[kFramesInFlight];
    VkDeviceSize m_particleUploadCursor{0};
    std::vector<std::pair<ParticleUpload, uint64_t>> m_retiredParticleUploads; // with the frame they were replaced in
    uint64_t m_frameCounter{0};
    VkDeviceSize UploadParticleQuads(const std::vector<ParticleQuad>& quads, VkBuffer& buffer);
    void ReleaseParticleUploads(bool all);
    std::map<uint32_t, GpuParticleDraw> m_gpuParticleDraws;
    std::map<int, std::future<std::vector<uint8_t>>> m_particleSheetJobs; // built-in sheets being drawn
    bool m_viewHasSceneDepth[2]{};
    void DrawDecals(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const std::map<ShapeType, GPUMesh>& meshes);
    void DrawFluidFoam(VkCommandBuffer cmd, int viewSlot);
    VkPipeline BuildFluidPipeline(const std::string& vertSpv, const std::string& fragSpv, VkRenderPass renderPass, uint32_t instanceStreams,
                                  uint32_t colorCount, const VkPipelineColorBlendAttachmentState* blends, bool depthTest, bool depthWrite,
                                  VkCompareOp depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL);
    // Terrain foliage visible from a view (or a shadow cascade): matrices grouped by mesh
    struct FoliageDraw {
        const GPUMesh* mesh;
        const FoliageType* type;
        bool builtIn;
        std::vector<glm::mat4> matrices;
    };
    void GatherFoliage(const Scene& scene, const std::map<std::string, GPUMesh>& customMeshes, const glm::vec3& cameraPos,
                       const glm::mat4& viewProj, bool shadowCastersOnly, std::vector<FoliageDraw>& out);
    // Volumetric clouds over the finished opaque scene (cloud_volume.frag), main views only
    VkPipeline m_cloudVolumePipeline{VK_NULL_HANDLE};
    void DrawVolumetricClouds(VkCommandBuffer cmd, int viewSlot);
    void DrawLiquidStreams(VkCommandBuffer cmd, int viewSlot, const std::map<ShapeType, GPUMesh>& meshes);
    GPUMesh m_oceanGrid;
    void CreateOceanGrid();
    void DrawOcean(VkCommandBuffer cmd, const Scene& scene, const Node& node, int viewSlot);
    struct LightmapMesh {
        std::string sourceKey;
        GPUMesh mesh;
    };
    std::map<uint32_t, LightmapMesh> m_lightmapMeshes; // by node id
    // The node's lightmapped mesh, or nullptr when it has none (or its mesh changed since the bake)
    const GPUMesh* FindLightmapMesh(const Node& node) const;

    // HDRI environment (nullptr = procedural sky)
    std::shared_ptr<VulkanTexture> m_environmentTexture;
    std::string m_environmentPath;

    // Previous prepass model matrix per (view slot, node id), for per-object motion vectors
    std::map<std::pair<int, uint32_t>, glm::mat4> m_prevModelMatrices;
    static constexpr uint32_t kLocalShadowResolution = 1024;
    static constexpr uint32_t kLocalShadowLayers = 12;
    static constexpr int kMaxLocalShadowLights = 4;
    VkPipelineLayout m_shadowPipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_shadowPipeline{VK_NULL_HANDLE};       // depth only
    VkPipeline m_shadowCutoutPipeline{VK_NULL_HANDLE}; // alpha-cutout casters
    VkPipeline m_skyPipeline{VK_NULL_HANDLE};

    // SSAO prepass
    VkRenderPass m_prepassRenderPass{VK_NULL_HANDLE};
    VkPipeline m_prepassPipeline{VK_NULL_HANDLE};

    // Per-view textures ([binding - kSSAOBinding][view slot]); white until a view provides its own
    std::shared_ptr<VulkanTexture> m_defaultViewTexture;
    VkImageView m_viewTextureViews[kViewTextureCount][kViewCount]{};
    VkSampler m_viewTextureSamplers[kViewTextureCount][kViewCount]{};
    static constexpr uint32_t kShadowMapResolution = 2048;
};
