#include "RenderEffects.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Renderer {

namespace {
constexpr float kPI = 3.14159265358979323846f;
}

// ============================================================================
// ShadowMapBuffer
// ============================================================================
ShadowMapBuffer::ShadowMapBuffer(uint32_t width, uint32_t height)
    : m_width(std::max(1u, width)), m_height(std::max(1u, height)),
      m_depthBuffer(static_cast<size_t>(m_width) * m_height, 1.0f) {}

void ShadowMapBuffer::Clear(float farDepth) {
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), farDepth);
}

void ShadowMapBuffer::WriteDepth(uint32_t x, uint32_t y, float depth01) {
    if (x >= m_width || y >= m_height) return;
    size_t idx = static_cast<size_t>(y) * m_width + x;
    if (depth01 < m_depthBuffer[idx]) {
        m_depthBuffer[idx] = depth01;
    }
}

float ShadowMapBuffer::ReadDepth(uint32_t x, uint32_t y) const {
    x = std::min(x, m_width - 1);
    y = std::min(y, m_height - 1);
    return m_depthBuffer[static_cast<size_t>(y) * m_width + x];
}

void ShadowMapBuffer::RasterizeTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                        const glm::mat4& lightSpaceMat) {
    auto toLightNDC = [&](const glm::vec3& p) -> glm::vec3 {
        glm::vec4 clip = lightSpaceMat * glm::vec4(p, 1.0f);
        float w = (std::abs(clip.w) > 1e-6f) ? clip.w : 1.0f;
        glm::vec3 ndc = glm::vec3(clip) / w;
        return glm::vec3(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f, ndc.z * 0.5f + 0.5f);
    };

    glm::vec3 p0 = toLightNDC(v0);
    glm::vec3 p1 = toLightNDC(v1);
    glm::vec3 p2 = toLightNDC(v2);

    float minX = std::clamp(std::min({p0.x, p1.x, p2.x}), 0.0f, 1.0f);
    float maxX = std::clamp(std::max({p0.x, p1.x, p2.x}), 0.0f, 1.0f);
    float minY = std::clamp(std::min({p0.y, p1.y, p2.y}), 0.0f, 1.0f);
    float maxY = std::clamp(std::max({p0.y, p1.y, p2.y}), 0.0f, 1.0f);

    uint32_t x0 = static_cast<uint32_t>(minX * (m_width - 1));
    uint32_t x1 = static_cast<uint32_t>(maxX * (m_width - 1));
    uint32_t y0 = static_cast<uint32_t>(minY * (m_height - 1));
    uint32_t y1 = static_cast<uint32_t>(maxY * (m_height - 1));

    auto edgeFn = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    };

    glm::vec2 a(p0.x, p0.y), b(p1.x, p1.y), c(p2.x, p2.y);
    float area = edgeFn(a, b, c);
    if (std::abs(area) < 1e-7f) return;

    for (uint32_t y = y0; y <= y1; ++y) {
        for (uint32_t x = x0; x <= x1; ++x) {
            glm::vec2 pt((static_cast<float>(x) + 0.5f) / m_width, (static_cast<float>(y) + 0.5f) / m_height);
            float w0 = edgeFn(b, c, pt) / area;
            float w1 = edgeFn(c, a, pt) / area;
            float w2 = edgeFn(a, b, pt) / area;
            if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                float depth = glm::clamp(w0 * p0.z + w1 * p1.z + w2 * p2.z, 0.0f, 1.0f);
                WriteDepth(x, y, depth);
            }
        }
    }
}

float ShadowMapBuffer::SampleShadowPCF(const glm::vec3& worldPos,
                                       const glm::vec3& surfaceNormal,
                                       const glm::vec3& lightDir,
                                       const glm::mat4& lightSpaceMat,
                                       float baseBias) const {
    glm::vec4 clip = lightSpaceMat * glm::vec4(worldPos, 1.0f);
    if (std::abs(clip.w) < 1e-6f) return 1.0f;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    glm::vec3 projCoords = ndc * 0.5f + 0.5f;

    if (projCoords.x < 0.0f || projCoords.x > 1.0f ||
        projCoords.y < 0.0f || projCoords.y > 1.0f ||
        projCoords.z > 1.0f) {
        return 1.0f;
    }

    float ndotl = std::max(glm::dot(glm::normalize(surfaceNormal), glm::normalize(lightDir)), 0.0f);
    float bias = std::max(baseBias * (1.0f - ndotl), baseBias * 0.25f);
    float currentDepth = projCoords.z;

    float visibility = 0.0f;
    int centerX = static_cast<int>(projCoords.x * (m_width - 1));
    int centerY = static_cast<int>(projCoords.y * (m_height - 1));

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            uint32_t sx = static_cast<uint32_t>(std::clamp(centerX + dx, 0, static_cast<int>(m_width - 1)));
            uint32_t sy = static_cast<uint32_t>(std::clamp(centerY + dy, 0, static_cast<int>(m_height - 1)));
            float closestDepth = ReadDepth(sx, sy);
            visibility += (currentDepth - bias > closestDepth) ? 0.0f : 1.0f;
        }
    }
    return visibility / 9.0f;
}

// ============================================================================
// Spherical Harmonics Global Illumination
// ============================================================================
void SH9IrradianceProbe::Clear() {
    for (int i = 0; i < 9; ++i) coeffs[i] = glm::vec3(0.0f);
}

void SH9IrradianceProbe::ProjectDirectionalLight(const glm::vec3& dirToLight, const glm::vec3& radiance) {
    glm::vec3 d = glm::normalize(dirToLight);
    // Real L0, L1, L2 spherical harmonic basis constants multiplied by cosine lobe convolution
    coeffs[0] += radiance * 0.282095f * kPI;
    coeffs[1] += radiance * (0.488603f * d.y) * (2.0f * kPI / 3.0f);
    coeffs[2] += radiance * (0.488603f * d.z) * (2.0f * kPI / 3.0f);
    coeffs[3] += radiance * (0.488603f * d.x) * (2.0f * kPI / 3.0f);
    coeffs[4] += radiance * (1.092548f * d.x * d.y) * (kPI / 4.0f);
    coeffs[5] += radiance * (1.092548f * d.y * d.z) * (kPI / 4.0f);
    coeffs[6] += radiance * (0.315392f * (3.0f * d.z * d.z - 1.0f)) * (kPI / 4.0f);
    coeffs[7] += radiance * (1.092548f * d.x * d.z) * (kPI / 4.0f);
    coeffs[8] += radiance * (0.546274f * (d.x * d.x - d.y * d.y)) * (kPI / 4.0f);
}

void SH9IrradianceProbe::ProjectAmbientSky(const glm::vec3& skyColor, const glm::vec3& groundColor) {
    glm::vec3 avg = 0.5f * (skyColor + groundColor);
    glm::vec3 diff = 0.5f * (skyColor - groundColor);
    coeffs[0] += avg / 0.282095f;
    coeffs[1] += diff / 0.488603f;
}

glm::vec3 SH9IrradianceProbe::EvaluateIrradiance(const glm::vec3& normal) const {
    glm::vec3 n = glm::normalize(normal);
    glm::vec3 result =
        coeffs[0] * 0.282095f +
        coeffs[1] * (0.488603f * n.y) +
        coeffs[2] * (0.488603f * n.z) +
        coeffs[3] * (0.488603f * n.x) +
        coeffs[4] * (1.092548f * n.x * n.y) +
        coeffs[5] * (1.092548f * n.y * n.z) +
        coeffs[6] * (0.315392f * (3.0f * n.z * n.z - 1.0f)) +
        coeffs[7] * (1.092548f * n.x * n.z) +
        coeffs[8] * (0.546274f * (n.x * n.x - n.y * n.y));
    return glm::max(result, glm::vec3(0.0f));
}

void GlobalIlluminationVolume::AddProbe(const SH9IrradianceProbe& probe) {
    m_probes.push_back(probe);
}

void GlobalIlluminationVolume::BakeFromScene(const Scene& scene, const glm::vec3& skyColor, const glm::vec3& groundColor) {
    m_probes.clear();
    SH9IrradianceProbe centerProbe;
    centerProbe.position = glm::vec3(0.0f, 2.0f, 0.0f);
    centerProbe.radius = 50.0f;
    centerProbe.ProjectAmbientSky(skyColor, groundColor);

    auto lights = RenderEffects::GatherSceneLights(scene);
    for (const auto& l : lights) {
        glm::vec3 dir = (l.type == LightType::Directional)
            ? glm::normalize(-l.direction)
            : glm::normalize(l.position - centerProbe.position);
        centerProbe.ProjectDirectionalLight(dir, l.color * (l.intensity * 0.15f));
    }
    m_probes.push_back(centerProbe);
}

glm::vec3 GlobalIlluminationVolume::SampleIndirectIrradiance(const glm::vec3& worldPos, const glm::vec3& normal) const {
    if (m_probes.empty()) return glm::vec3(0.0f);

    glm::vec3 totalIrradiance(0.0f);
    float totalWeight = 0.0f;

    for (const auto& probe : m_probes) {
        float dist = glm::length(worldPos - probe.position);
        float w = std::max(0.001f, 1.0f - glm::clamp(dist / std::max(probe.radius, 0.01f), 0.0f, 0.999f));
        totalIrradiance += probe.EvaluateIrradiance(normal) * w;
        totalWeight += w;
    }
    return (totalWeight > 0.0f) ? (totalIrradiance / totalWeight) : glm::vec3(0.0f);
}

// ============================================================================
// RenderEffects: Lights, Shadows, PBR, Parallax, Post-Processing
// ============================================================================
std::vector<GPULightEntry> RenderEffects::GatherSceneLights(const Scene& scene) {
    std::vector<GPULightEntry> entries;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::Light) continue;

        GPULightEntry entry;
        entry.nodeId = node->id;
        entry.type = node->lightType;

        glm::mat4 worldMat = node->GetWorldTransformMatrix(scene);
        entry.position = glm::vec3(worldMat[3]);
        // Forward axis (-Z in local space) transformed to world space
        glm::vec3 forward = -glm::vec3(worldMat[2]);
        entry.direction = (glm::length(forward) > 1e-5f) ? glm::normalize(forward) : glm::vec3(0.0f, -1.0f, 0.0f);

        entry.color = node->lightColor;
        entry.intensity = node->lightIntensity;
        entry.range = node->lightRange;

        float innerRad = glm::radians(std::clamp(node->spotInnerConeAngle, 1.0f, 89.0f));
        float outerRad = glm::radians(std::clamp(std::max(node->spotOuterConeAngle, node->spotInnerConeAngle + 0.5f), 1.5f, 89.5f));
        entry.innerConeCos = std::cos(innerRad);
        entry.outerConeCos = std::cos(outerRad);
        entry.castShadows = node->castShadows;
        entry.shadowBias = node->shadowBias;
        entry.lightSpaceMatrix = ComputeLightSpaceMatrix(entry);

        entries.push_back(entry);
    }
    return entries;
}

glm::mat4 RenderEffects::ComputeLightSpaceMatrix(const GPULightEntry& light,
                                                 const glm::vec3& sceneCenter,
                                                 float sceneRadius) {
    glm::vec3 dir = (glm::length(light.direction) > 1e-5f) ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 up = (std::abs(dir.y) > 0.98f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    if (light.type == LightType::Directional) {
        glm::vec3 lightPos = sceneCenter - dir * sceneRadius;
        glm::mat4 view = glm::lookAt(lightPos, sceneCenter, up);
        glm::mat4 proj = glm::ortho(-sceneRadius, sceneRadius, -sceneRadius, sceneRadius, 0.1f, sceneRadius * 2.5f);
        return proj * view;
    } else {
        glm::mat4 view = glm::lookAt(light.position, light.position + dir, up);
        float fov = (light.type == LightType::Spot)
            ? std::acos(std::clamp(light.outerConeCos, -1.0f, 1.0f)) * 2.0f
            : glm::radians(90.0f);
        glm::mat4 proj = glm::perspective(fov, 1.0f, 0.1f, std::max(light.range, 1.0f));
        return proj * view;
    }
}

float RenderEffects::EvaluateLightAttenuation(const GPULightEntry& light, const glm::vec3& worldPos, glm::vec3& outLightDir) {
    if (light.type == LightType::Directional) {
        outLightDir = glm::normalize(-light.direction);
        return 1.0f;
    }

    glm::vec3 toLight = light.position - worldPos;
    float dist = glm::length(toLight);
    if (dist <= 1e-5f) {
        outLightDir = glm::vec3(0.0f, 1.0f, 0.0f);
        return 1.0f;
    }
    outLightDir = toLight / dist;

    if (dist >= light.range) return 0.0f;
    float normDist = dist / std::max(light.range, 0.01f);
    float rangeWindow = std::pow(std::max(1.0f - std::pow(normDist, 4.0f), 0.0f), 2.0f);
    float distanceAtten = rangeWindow / (dist * dist + 1.0f);

    if (light.type == LightType::Spot) {
        float theta = glm::dot(outLightDir, glm::normalize(-light.direction));
        float epsilon = std::max(light.innerConeCos - light.outerConeCos, 1e-4f);
        float spotFactor = glm::clamp((theta - light.outerConeCos) / epsilon, 0.0f, 1.0f);
        spotFactor = spotFactor * spotFactor * (3.0f - 2.0f * spotFactor); // smoothstep
        return distanceAtten * spotFactor;
    }

    return distanceAtten;
}

glm::vec3 RenderEffects::EvaluateMultiLightPBR(const std::vector<GPULightEntry>& lights,
                                               const glm::vec3& worldPos,
                                               const glm::vec3& normal,
                                               const glm::vec3& viewDir,
                                               const glm::vec3& albedo,
                                               float metallic,
                                               float roughness,
                                               float ao,
                                               const glm::vec3& emissive,
                                               const GlobalIlluminationVolume* giVolume,
                                               const ShadowMapBuffer* primaryShadowMap) {
    glm::vec3 N = glm::normalize(normal);
    glm::vec3 V = glm::normalize(viewDir);
    glm::vec3 F0 = glm::mix(glm::vec3(0.04f), albedo, metallic);

    glm::vec3 Lo(0.0f);
    for (size_t i = 0; i < lights.size(); ++i) {
        const auto& light = lights[i];
        glm::vec3 L(0.0f);
        float atten = EvaluateLightAttenuation(light, worldPos, L);
        if (atten <= 0.0f) continue;

        float shadowVis = 1.0f;
        if (i == 0 && primaryShadowMap && light.castShadows) {
            shadowVis = primaryShadowMap->SampleShadowPCF(worldPos, N, L, light.lightSpaceMatrix, light.shadowBias);
        }
        if (shadowVis <= 0.0f) continue;

        glm::vec3 H = glm::normalize(V + L);
        glm::vec3 radiance = light.color * light.intensity * atten * shadowVis;

        // Cook-Torrance BRDF
        float NdotL = std::max(glm::dot(N, L), 0.0f);
        float NdotV = std::max(glm::dot(N, V), 0.001f);
        float NdotH = std::max(glm::dot(N, H), 0.0f);
        float HdotV = std::max(glm::dot(H, V), 0.0f);

        float a = std::max(roughness * roughness, 0.002f);
        float a2 = a * a;
        float denom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
        float D = a2 / (kPI * denom * denom + 1e-6f);

        float k = ((roughness + 1.0f) * (roughness + 1.0f)) / 8.0f;
        float G_V = NdotV / (NdotV * (1.0f - k) + k);
        float G_L = NdotL / (NdotL * (1.0f - k) + k);
        float G = G_V * G_L;

        glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(1.0f - HdotV, 5.0f);

        glm::vec3 specular = (D * G * F) / (4.0f * NdotV * NdotL + 0.0001f);
        glm::vec3 kS = F;
        glm::vec3 kD = (glm::vec3(1.0f) - kS) * (1.0f - metallic);

        Lo += (kD * albedo / kPI + specular) * radiance * NdotL;
    }

    glm::vec3 ambientIrradiance = giVolume
        ? giVolume->SampleIndirectIrradiance(worldPos, N)
        : glm::vec3(0.08f);

    glm::vec3 ambient = ambientIrradiance * albedo * glm::clamp(ao, 0.0f, 1.0f);
    return ambient + Lo + emissive;
}

glm::vec2 RenderEffects::ComputeParallaxOcclusionUV(const glm::vec2& uv,
                                                    const glm::vec3& viewDirTangent,
                                                    float heightScale,
                                                    const std::function<float(const glm::vec2&)>& heightSampler,
                                                    int numLayers) {
    if (!heightSampler || heightScale <= 0.0f || numLayers < 2) return uv;

    glm::vec3 V = glm::normalize(viewDirTangent);
    float layerDepth = 1.0f / static_cast<float>(numLayers);
    float currentLayerDepth = 0.0f;
    glm::vec2 P = glm::vec2(V.x, V.y) / std::max(V.z, 0.15f) * heightScale;
    glm::vec2 deltaTexCoords = P / static_cast<float>(numLayers);

    glm::vec2 currentTexCoords = uv;
    float currentDepthMapValue = 1.0f - heightSampler(currentTexCoords);

    int steps = 0;
    while (currentLayerDepth < currentDepthMapValue && steps < numLayers) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = 1.0f - heightSampler(currentTexCoords);
        currentLayerDepth += layerDepth;
        ++steps;
    }

    glm::vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0f - heightSampler(prevTexCoords)) - currentLayerDepth + layerDepth;
    float denom = afterDepth - beforeDepth;
    float weight = (std::abs(denom) > 1e-6f) ? glm::clamp(afterDepth / denom, 0.0f, 1.0f) : 0.5f;

    return prevTexCoords * weight + currentTexCoords * (1.0f - weight);
}

glm::vec3 RenderEffects::ExtractBrightPass(const glm::vec3& hdrColor, float threshold) {
    float luminance = glm::dot(hdrColor, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    if (luminance <= threshold) return glm::vec3(0.0f);
    float softFactor = (luminance - threshold) / std::max(luminance, 1e-4f);
    return hdrColor * softFactor;
}

glm::vec3 RenderEffects::ACESTonemap(const glm::vec3& hdrColor, float exposure) {
    glm::vec3 x = hdrColor * std::max(exposure, 0.001f);
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    glm::vec3 mapped = glm::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
    return mapped;
}

glm::vec3 RenderEffects::ApplyBloomAndTonemap(const glm::vec3& hdrColor,
                                              const glm::vec3& bloomContribution,
                                              const PostProcessSettings& settings) {
    glm::vec3 combined = hdrColor;
    if (settings.enableBloom) {
        combined += bloomContribution * settings.bloomIntensity;
    }
    return ACESTonemap(combined, settings.exposure);
}

std::vector<glm::vec3> RenderEffects::GenerateSSAOKernel(int kernelSize) {
    kernelSize = std::clamp(kernelSize, 1, 64);
    std::vector<glm::vec3> kernel;
    kernel.reserve(kernelSize);

    uint32_t seed = 42u;
    auto nextFloat = [&seed]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    };

    for (int i = 0; i < kernelSize; ++i) {
        glm::vec3 sample(
            nextFloat() * 2.0f - 1.0f,
            nextFloat() * 2.0f - 1.0f,
            nextFloat() // Hemisphere z in [0, 1]
        );
        if (glm::length(sample) > 1e-5f) {
            sample = glm::normalize(sample);
        }
        sample *= nextFloat();
        float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;
        kernel.push_back(sample);
    }
    return kernel;
}

float RenderEffects::EvaluateSSAO(const glm::vec3& viewPos,
                                  const glm::vec3& viewNormal,
                                  const std::vector<glm::vec3>& kernel,
                                  float radius,
                                  float bias,
                                  const std::function<float(const glm::vec3&)>& sampleSceneViewZ) {
    if (kernel.empty() || !sampleSceneViewZ) return 1.0f;

    glm::vec3 N = glm::normalize(viewNormal);
    glm::vec3 up = (std::abs(N.z) < 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 tangent = glm::normalize(up - N * glm::dot(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);
    glm::mat3 TBN(tangent, bitangent, N);

    float occlusion = 0.0f;
    for (const auto& k : kernel) {
        glm::vec3 samplePos = viewPos + (TBN * k) * radius;
        float sceneZ = sampleSceneViewZ(samplePos);
        float rangeCheck = glm::smoothstep(0.0f, 1.0f, radius / (std::abs(viewPos.z - sceneZ) + 1e-4f));
        if (sceneZ >= samplePos.z + bias) {
            occlusion += rangeCheck;
        }
    }
    float ao = 1.0f - (occlusion / static_cast<float>(kernel.size()));
    return glm::clamp(ao, 0.0f, 1.0f);
}

RenderEffects::SSRHitResult RenderEffects::TraceSSR(const glm::vec3& viewPos,
                                                    const glm::vec3& viewNormal,
                                                    const PostProcessSettings& settings,
                                                    const std::function<float(const glm::vec3&)>& sampleSceneViewZ) {
    SSRHitResult result;
    if (!settings.enableSSR || !sampleSceneViewZ) return result;

    glm::vec3 V = glm::normalize(viewPos);
    glm::vec3 N = glm::normalize(viewNormal);
    glm::vec3 reflectDir = glm::normalize(glm::reflect(V, N));

    glm::vec3 currentPos = viewPos;
    for (int step = 1; step <= settings.ssrMaxSteps; ++step) {
        currentPos += reflectDir * settings.ssrStepSize;
        float sceneZ = sampleSceneViewZ(currentPos);
        float depthDiff = sceneZ - currentPos.z;
        if (depthDiff >= 0.0f && depthDiff <= settings.ssrThickness) {
            result.hit = true;
            result.hitViewPos = currentPos;
            result.hitUV = glm::vec2(currentPos.x * 0.5f + 0.5f, currentPos.y * 0.5f + 0.5f);
            result.confidence = 1.0f - (static_cast<float>(step) / static_cast<float>(settings.ssrMaxSteps));
            return result;
        }
    }
    return result;
}

glm::vec3 RenderEffects::ApplyExponentialHeightFog(const glm::vec3& sceneColor,
                                                   const glm::vec3& cameraPos,
                                                   const glm::vec3& worldPos,
                                                   const PostProcessSettings& settings) {
    if (!settings.enableFog) return sceneColor;

    glm::vec3 ray = worldPos - cameraPos;
    float dist = glm::length(ray);
    float heightDelta = std::max(worldPos.y - settings.fogBaseHeight, 0.0f);
    float heightFactor = std::exp(-heightDelta * settings.fogHeightFalloff);
    float opticalDepth = dist * settings.fogDensity * heightFactor;
    float fogFactor = glm::clamp(1.0f - std::exp(-opticalDepth), 0.0f, 1.0f);

    return glm::mix(sceneColor, settings.fogColor, fogFactor);
}

glm::vec3 RenderEffects::ComputeVolumetricLightScattering(const glm::vec3& cameraPos,
                                                          const glm::vec3& worldPos,
                                                          const GPULightEntry& light,
                                                          const PostProcessSettings& settings,
                                                          const ShadowMapBuffer* shadowMap) {
    if (!settings.enableVolumetricLighting || settings.volumetricSamples <= 0) {
        return glm::vec3(0.0f);
    }

    glm::vec3 ray = worldPos - cameraPos;
    float rayLen = glm::length(ray);
    if (rayLen <= 1e-4f) return glm::vec3(0.0f);
    glm::vec3 rayDir = ray / rayLen;
    glm::vec3 stepVec = ray / static_cast<float>(settings.volumetricSamples);

    float g = std::clamp(settings.volumetricScattering, -0.95f, 0.95f);
    float g2 = g * g;

    glm::vec3 accumScattering(0.0f);
    glm::vec3 samplePos = cameraPos + stepVec * 0.5f;

    for (int i = 0; i < settings.volumetricSamples; ++i) {
        glm::vec3 L(0.0f);
        float atten = EvaluateLightAttenuation(light, samplePos, L);
        if (atten > 0.0f) {
            float shadowVis = 1.0f;
            if (shadowMap && light.castShadows) {
                shadowVis = shadowMap->SampleShadowPCF(samplePos, L, L, light.lightSpaceMatrix, light.shadowBias);
            }
            if (shadowVis > 0.0f) {
                // Henyey-Greenstein phase function
                float cosTheta = glm::dot(rayDir, -L);
                float denom = 1.0f + g2 - 2.0f * g * cosTheta;
                float phase = (1.0f - g2) / (4.0f * kPI * std::pow(std::max(denom, 1e-4f), 1.5f));
                accumScattering += light.color * light.intensity * atten * shadowVis * phase;
            }
        }
        samplePos += stepVec;
    }

    return (accumScattering / static_cast<float>(settings.volumetricSamples)) * settings.volumetricIntensity;
}

// ============================================================================
// VoxelGlobalIllumination (VXGI Reference Implementation)
// ============================================================================
void VoxelGlobalIllumination::Init(uint32_t baseDim, const glm::vec3& minCorner, float worldExtent) {
    m_baseDim = std::max(4u, baseDim);
    m_minCorner = minCorner;
    m_worldExtent = std::max(1.0f, worldExtent);
    m_voxelSize = m_worldExtent / static_cast<float>(m_baseDim);

    m_mips.clear();
    uint32_t d = m_baseDim;
    while (d >= 1u) {
        m_mips.emplace_back(static_cast<size_t>(d) * d * d, glm::vec4(0.0f));
        if (d == 1u) break;
        d = std::max(1u, d >> 1);
    }
}

void VoxelGlobalIllumination::Clear() {
    for (auto& mip : m_mips) {
        std::fill(mip.begin(), mip.end(), glm::vec4(0.0f));
    }
}

void VoxelGlobalIllumination::InjectVoxel(const glm::vec3& worldPos, const glm::vec3& radiance, float opacity) {
    if (m_mips.empty()) Init();
    const glm::vec3 uvw = (worldPos - m_minCorner) / m_worldExtent;
    if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f || uvw.x >= 1.0f || uvw.y >= 1.0f || uvw.z >= 1.0f) return;
    const uint32_t ix = std::min(static_cast<uint32_t>(uvw.x * m_baseDim), m_baseDim - 1);
    const uint32_t iy = std::min(static_cast<uint32_t>(uvw.y * m_baseDim), m_baseDim - 1);
    const uint32_t iz = std::min(static_cast<uint32_t>(uvw.z * m_baseDim), m_baseDim - 1);
    const float a = std::clamp(opacity, 0.0f, 1.0f);
    m_mips[0][(static_cast<size_t>(iz) * m_baseDim + iy) * m_baseDim + ix] = glm::vec4(radiance * a, a);
}

void VoxelGlobalIllumination::VoxelizeScene(const Scene& scene) {
    if (m_mips.empty()) Init();
    Clear();
    const auto lights = RenderEffects::GatherSceneLights(scene);
    const auto& env = scene.GetEnvironment();
    const glm::vec3 skyAmb = env.enableGI ? (env.giSkyColor * std::max(env.giIntensity, 0.0f) * 0.22f) : glm::vec3(0.04f);

    for (const auto& node : scene.GetNodes()) {
        if (!node || node->IsUINode() || (node->nodeType != NodeType::Mesh && node->nodeType != NodeType::Character)) continue;
        const glm::vec3 center = node->GetWorldPosition(scene);
        const glm::vec3 halfExt = glm::max(glm::abs(node->scale) * 0.5f, glm::vec3(m_voxelSize * 0.5f));
        const glm::vec3 albedo = glm::vec3(node->color) * (1.0f - 0.8f * std::clamp(node->metallic, 0.0f, 1.0f));
        const glm::vec3 emissive = node->emissiveColor * node->emissiveIntensity;

        const glm::vec3 lo = glm::max(center - halfExt, m_minCorner);
        const glm::vec3 hi = glm::min(center + halfExt, m_minCorner + glm::vec3(m_worldExtent));
        for (float z = lo.z; z <= hi.z; z += m_voxelSize * 0.75f) {
            for (float y = lo.y; y <= hi.y; y += m_voxelSize * 0.75f) {
                for (float x = lo.x; x <= hi.x; x += m_voxelSize * 0.75f) {
                    const glm::vec3 wp(x, y, z);
                    const glm::vec3 n = glm::length(wp - center) > 1e-4f ? glm::normalize(wp - center) : glm::vec3(0.0f, 1.0f, 0.0f);
                    glm::vec3 direct(0.0f);
                    for (const auto& l : lights) {
                        glm::vec3 L(0.0f);
                        const float atten = RenderEffects::EvaluateLightAttenuation(l, wp, L);
                        if (atten > 0.0f) {
                            direct += l.color * l.intensity * atten * (std::max(glm::dot(n, L), 0.0f) * 0.75f + 0.25f);
                        }
                    }
                    InjectVoxel(wp, albedo * (direct + skyAmb) + emissive, 1.0f);
                }
            }
        }
    }
    BuildMipChain();
}

void VoxelGlobalIllumination::BuildMipChain() {
    for (size_t m = 1; m < m_mips.size(); ++m) {
        const uint32_t srcDim = std::max(1u, m_baseDim >> (m - 1));
        const uint32_t dstDim = std::max(1u, m_baseDim >> m);
        const auto& src = m_mips[m - 1];
        auto& dst = m_mips[m];
        for (uint32_t z = 0; z < dstDim; ++z) {
            for (uint32_t y = 0; y < dstDim; ++y) {
                for (uint32_t x = 0; x < dstDim; ++x) {
                    glm::vec3 sumRgb(0.0f);
                    float sumAlpha = 0.0f;
                    for (uint32_t dz = 0; dz < 2; ++dz) {
                        for (uint32_t dy = 0; dy < 2; ++dy) {
                            for (uint32_t dx = 0; dx < 2; ++dx) {
                                const uint32_t sx = std::min(2 * x + dx, srcDim - 1);
                                const uint32_t sy = std::min(2 * y + dy, srcDim - 1);
                                const uint32_t sz = std::min(2 * z + dz, srcDim - 1);
                                const glm::vec4& c = src[(static_cast<size_t>(sz) * srcDim + sy) * srcDim + sx];
                                sumRgb += glm::vec3(c);
                                sumAlpha += c.a;
                            }
                        }
                    }
                    const float parentAlpha = std::clamp(sumAlpha * 0.25f, 0.0f, 1.0f);
                    const glm::vec3 avgRgb = sumAlpha > 1e-4f ? (sumRgb / sumAlpha) : glm::vec3(0.0f);
                    dst[(static_cast<size_t>(z) * dstDim + y) * dstDim + x] = glm::vec4(avgRgb * parentAlpha, parentAlpha);
                }
            }
        }
    }
}

glm::vec4 VoxelGlobalIllumination::SampleMipTrilinear(const glm::vec3& uvw, uint32_t mip) const {
    if (m_mips.empty()) return glm::vec4(0.0f);
    const uint32_t m = std::min(mip, static_cast<uint32_t>(m_mips.size() - 1));
    const uint32_t d = std::max(1u, m_baseDim >> m);
    const auto& data = m_mips[m];
    const glm::vec3 coord = glm::clamp(uvw, 0.0f, 1.0f) * static_cast<float>(d) - 0.5f;
    const glm::ivec3 i0 = glm::clamp(glm::ivec3(glm::floor(coord)), glm::ivec3(0), glm::ivec3(static_cast<int>(d) - 1));
    const glm::ivec3 i1 = glm::clamp(i0 + 1, glm::ivec3(0), glm::ivec3(static_cast<int>(d) - 1));
    const glm::vec3 f = glm::clamp(coord - glm::floor(coord), 0.0f, 1.0f);

    auto fetch = [&](int x, int y, int z) {
        return data[(static_cast<size_t>(z) * d + y) * d + x];
    };
    const glm::vec4 c00 = glm::mix(fetch(i0.x, i0.y, i0.z), fetch(i1.x, i0.y, i0.z), f.x);
    const glm::vec4 c10 = glm::mix(fetch(i0.x, i1.y, i0.z), fetch(i1.x, i1.y, i0.z), f.x);
    const glm::vec4 c01 = glm::mix(fetch(i0.x, i0.y, i1.z), fetch(i1.x, i0.y, i1.z), f.x);
    const glm::vec4 c11 = glm::mix(fetch(i0.x, i1.y, i1.z), fetch(i1.x, i1.y, i1.z), f.x);
    const glm::vec4 c0 = glm::mix(c00, c10, f.y);
    const glm::vec4 c1 = glm::mix(c01, c11, f.y);
    return glm::mix(c0, c1, f.z);
}

glm::vec4 VoxelGlobalIllumination::TraceCone(const glm::vec3& origin, const glm::vec3& dir,
                                             float tanHalfAngle, float maxDist) const {
    if (m_mips.empty()) return glm::vec4(0.0f);
    const float maxMip = static_cast<float>(m_mips.size() - 1);
    const glm::vec3 d = glm::normalize(dir);
    glm::vec4 accum(0.0f);
    float t = m_voxelSize * 1.35f;
    for (int step = 0; step < 16 && t < maxDist && accum.a < 0.97f; ++step) {
        const glm::vec3 p = origin + d * t;
        const glm::vec3 uvw = (p - m_minCorner) / m_worldExtent;
        if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f || uvw.x > 1.0f || uvw.y > 1.0f || uvw.z > 1.0f) break;
        const float diameter = std::max(2.0f * tanHalfAngle * t, m_voxelSize);
        const float lod = std::clamp(std::log2(diameter / m_voxelSize), 0.0f, maxMip);
        const uint32_t m0 = static_cast<uint32_t>(std::floor(lod));
        const uint32_t m1 = std::min(m0 + 1, static_cast<uint32_t>(m_mips.size() - 1));
        const glm::vec4 vox = glm::mix(SampleMipTrilinear(uvw, m0), SampleMipTrilinear(uvw, m1), lod - std::floor(lod));
        const float w = 1.0f - accum.a;
        accum += w * vox;
        t += std::max(diameter * 0.65f, m_voxelSize * 0.75f);
    }
    return accum;
}

glm::vec4 VoxelGlobalIllumination::EvaluateDiffuseGI(const glm::vec3& worldPos, const glm::vec3& normal) const {
    const glm::vec3 N = glm::normalize(normal);
    const glm::vec3 origin = worldPos + N * (m_voxelSize * 1.4f);
    const float maxDist = m_worldExtent * 0.65f;
    const glm::vec3 up = std::abs(N.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 T = glm::normalize(glm::cross(up, N));
    const glm::vec3 B = glm::cross(N, T);

    constexpr float kAperture = 0.57735f;
    const glm::vec4 c0 = TraceCone(origin, N, kAperture, maxDist);
    const glm::vec4 c1 = TraceCone(origin, glm::normalize(0.5f * N + 0.866025f * T), kAperture, maxDist);
    const glm::vec4 c2 = TraceCone(origin, glm::normalize(0.5f * N - 0.866025f * T), kAperture, maxDist);
    const glm::vec4 c3 = TraceCone(origin, glm::normalize(0.5f * N + 0.866025f * B), kAperture, maxDist);
    const glm::vec4 c4 = TraceCone(origin, glm::normalize(0.5f * N - 0.866025f * B), kAperture, maxDist);
    const glm::vec4 sum = 0.28f * c0 + 0.18f * (c1 + c2 + c3 + c4);
    const float voxelAO = std::clamp(1.0f - sum.a * 0.75f, 0.18f, 1.0f);
    return glm::vec4( glm::vec3(sum), voxelAO );
}

glm::vec4 VoxelGlobalIllumination::EvaluateSpecularGI(const glm::vec3& worldPos, const glm::vec3& reflectDir,
                                                      const glm::vec3& normal, float roughness) const {
    const glm::vec3 N = glm::normalize(normal);
    const glm::vec3 origin = worldPos + N * (m_voxelSize * 1.4f);
    const float tanHalfAngle = glm::mix(0.04f, 0.55f, roughness * roughness);
    const glm::vec4 spec = TraceCone(origin, reflectDir, tanHalfAngle, m_worldExtent * 0.8f);
    const glm::vec3 rgb = spec.a > 1e-3f ? (glm::vec3(spec) / std::max(spec.a, 0.25f)) : glm::vec3(0.0f);
    return glm::vec4(rgb, std::clamp(spec.a, 0.0f, 1.0f));
}

} // namespace Renderer

