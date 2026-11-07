#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>
#include "Scene/Scene.h"

namespace Renderer {

struct GPULightEntry {
    uint32_t nodeId{0};
    LightType type{LightType::Point};
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity{2.0f};
    float range{25.0f};
    float innerConeCos{0.9659f}; // cos(15 deg)
    float outerConeCos{0.8660f}; // cos(30 deg)
    bool castShadows{true};
    float shadowBias{0.005f};
    glm::mat4 lightSpaceMatrix{1.0f};
};

// Realtime Shadow Map with 3x3 Percentage-Closer Filtering (PCF)
class ShadowMapBuffer {
public:
    ShadowMapBuffer(uint32_t width = 512, uint32_t height = 512);

    void Clear(float farDepth = 1.0f);
    void WriteDepth(uint32_t x, uint32_t y, float depth01);
    float ReadDepth(uint32_t x, uint32_t y) const;

    // Rasterizes a triangle into the shadow depth map in light clip space
    void RasterizeTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::mat4& lightSpaceMat);

    // Evaluates shadow visibility [0.0 = full shadow, 1.0 = fully lit] using 3x3 PCF
    float SampleShadowPCF(const glm::vec3& worldPos,
                          const glm::vec3& surfaceNormal,
                          const glm::vec3& lightDir,
                          const glm::mat4& lightSpaceMat,
                          float baseBias = 0.005f) const;

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    uint32_t m_width;
    uint32_t m_height;
    std::vector<float> m_depthBuffer;
};

// L2 Spherical Harmonics (9-band) Irradiance Probe for Global Illumination
struct SH9IrradianceProbe {
    glm::vec3 position{0.0f};
    float radius{20.0f};
    glm::vec3 coeffs[9]{ glm::vec3(0.0f) };

    void Clear();
    void ProjectDirectionalLight(const glm::vec3& dirToLight, const glm::vec3& radiance);
    void ProjectAmbientSky(const glm::vec3& skyColor, const glm::vec3& groundColor);
    glm::vec3 EvaluateIrradiance(const glm::vec3& normal) const;
};

class GlobalIlluminationVolume {
public:
    void AddProbe(const SH9IrradianceProbe& probe);
    void ClearProbes() { m_probes.clear(); }
    size_t GetProbeCount() const { return m_probes.size(); }

    // Bakes SH probes from scene lights + sky hemisphere
    void BakeFromScene(const Scene& scene, const glm::vec3& skyColor = glm::vec3(0.35f, 0.45f, 0.65f),
                       const glm::vec3& groundColor = glm::vec3(0.15f, 0.12f, 0.10f));

    // Evaluates spatially interpolated indirect diffuse irradiance at worldPos along normal
    glm::vec3 SampleIndirectIrradiance(const glm::vec3& worldPos, const glm::vec3& normal) const;

private:
    std::vector<SH9IrradianceProbe> m_probes;
};

// Voxel Global Illumination (VXGI) 3D Mipmapped Radiance & Opacity Volume for Voxel Cone Tracing
class VoxelGlobalIllumination {
public:
    void Init(uint32_t baseDim = 32, const glm::vec3& minCorner = glm::vec3(-12.0f), float worldExtent = 24.0f);
    void Clear();

    void InjectVoxel(const glm::vec3& worldPos, const glm::vec3& radiance, float opacity = 1.0f);
    void VoxelizeScene(const Scene& scene);
    void BuildMipChain();

    glm::vec4 SampleMipTrilinear(const glm::vec3& uvw, uint32_t mip) const;
    glm::vec4 TraceCone(const glm::vec3& origin, const glm::vec3& dir, float tanHalfAngle, float maxDist) const;
    glm::vec4 EvaluateDiffuseGI(const glm::vec3& worldPos, const glm::vec3& normal) const;
    glm::vec4 EvaluateSpecularGI(const glm::vec3& worldPos, const glm::vec3& reflectDir,
                                 const glm::vec3& normal, float roughness) const;

    uint32_t GetBaseDim() const { return m_baseDim; }
    uint32_t GetMipCount() const { return static_cast<uint32_t>(m_mips.size()); }
    const glm::vec3& GetMinCorner() const { return m_minCorner; }
    float GetWorldExtent() const { return m_worldExtent; }
    float GetVoxelSize() const { return m_voxelSize; }
    const std::vector<std::vector<glm::vec4>>& GetMips() const { return m_mips; }

private:
    uint32_t m_baseDim{32};
    glm::vec3 m_minCorner{-12.0f};
    float m_worldExtent{24.0f};
    float m_voxelSize{0.75f};
    std::vector<std::vector<glm::vec4>> m_mips;
};

struct PostProcessSettings {
    // Bloom & HDR Tonemapping
    bool enableBloom{true};
    float bloomThreshold{1.0f};
    float bloomIntensity{0.35f};
    float exposure{1.0f};

    // Screen Space Ambient Occlusion (SSAO)
    bool enableSSAO{true};
    int ssaoKernelSize{16};
    float ssaoRadius{0.5f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.0f};

    // Screen Space Reflections (SSR)
    bool enableSSR{true};
    int ssrMaxSteps{32};
    float ssrStepSize{0.15f};
    float ssrThickness{0.1f};

    // Exponential Height Fog & Volumetric Lighting
    bool enableFog{true};
    glm::vec3 fogColor{0.55f, 0.65f, 0.78f};
    float fogDensity{0.02f};
    float fogHeightFalloff{0.15f};
    float fogBaseHeight{0.0f};

    bool enableVolumetricLighting{true};
    int volumetricSamples{16};
    float volumetricScattering{0.6f}; // Mie anisotropy g in [-1, 1]
    float volumetricIntensity{0.4f};
};

class RenderEffects {
public:
    // 1. Unlimited Lights Gather & Evaluation
    static std::vector<GPULightEntry> GatherSceneLights(const Scene& scene);
    static glm::mat4 ComputeLightSpaceMatrix(const GPULightEntry& light,
                                             const glm::vec3& sceneCenter = glm::vec3(0.0f),
                                             float sceneRadius = 25.0f);

    static float EvaluateLightAttenuation(const GPULightEntry& light, const glm::vec3& worldPos, glm::vec3& outLightDir);

    static glm::vec3 EvaluateMultiLightPBR(const std::vector<GPULightEntry>& lights,
                                           const glm::vec3& worldPos,
                                           const glm::vec3& normal,
                                           const glm::vec3& viewDir,
                                           const glm::vec3& albedo,
                                           float metallic,
                                           float roughness,
                                           float ao = 1.0f,
                                           const glm::vec3& emissive = glm::vec3(0.0f),
                                           const GlobalIlluminationVolume* giVolume = nullptr,
                                           const ShadowMapBuffer* primaryShadowMap = nullptr);

    // 2. Parallax Occlusion Mapping (Height / Displacement Map)
    static glm::vec2 ComputeParallaxOcclusionUV(const glm::vec2& uv,
                                                const glm::vec3& viewDirTangent,
                                                float heightScale,
                                                const std::function<float(const glm::vec2&)>& heightSampler,
                                                int numLayers = 16);

    // 3. Bloom & ACES HDR Tonemapping
    static glm::vec3 ExtractBrightPass(const glm::vec3& hdrColor, float threshold = 1.0f);
    static glm::vec3 ACESTonemap(const glm::vec3& hdrColor, float exposure = 1.0f);
    static glm::vec3 ApplyBloomAndTonemap(const glm::vec3& hdrColor,
                                          const glm::vec3& bloomContribution,
                                          const PostProcessSettings& settings);

    // 4. Screen Space Ambient Occlusion (SSAO)
    static std::vector<glm::vec3> GenerateSSAOKernel(int kernelSize = 16);
    static float EvaluateSSAO(const glm::vec3& viewPos,
                              const glm::vec3& viewNormal,
                              const std::vector<glm::vec3>& kernel,
                              float radius,
                              float bias,
                              const std::function<float(const glm::vec3&)>& sampleSceneViewZ);

    // 5. Screen Space Reflections (SSR)
    struct SSRHitResult {
        bool hit{false};
        glm::vec3 hitViewPos{0.0f};
        glm::vec2 hitUV{0.0f};
        float confidence{0.0f};
    };
    static SSRHitResult TraceSSR(const glm::vec3& viewPos,
                                 const glm::vec3& viewNormal,
                                 const PostProcessSettings& settings,
                                 const std::function<float(const glm::vec3&)>& sampleSceneViewZ);

    // 6. Exponential Height Fog & Volumetric Light Scattering
    static glm::vec3 ApplyExponentialHeightFog(const glm::vec3& sceneColor,
                                               const glm::vec3& cameraPos,
                                               const glm::vec3& worldPos,
                                               const PostProcessSettings& settings);

    static glm::vec3 ComputeVolumetricLightScattering(const glm::vec3& cameraPos,
                                                      const glm::vec3& worldPos,
                                                      const GPULightEntry& light,
                                                      const PostProcessSettings& settings,
                                                      const ShadowMapBuffer* shadowMap = nullptr);
};

} // namespace Renderer

