#include "TerrainNode.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <algorithm>
#include <cmath>

TerrainNode::TerrainNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Terrain;
    type = ShapeType::Custom;
    parentId = pId;
    name = nodeName.empty() ? "Terrain" : nodeName;
    color = glm::vec4(0.48f, 0.62f, 0.38f, 1.0f); // Natural earthy grass tint
    roughness = 0.85f;
    metallic = 0.05f;

    InitGrid();
    GenerateProcedural(TerrainPreset::RollingHills);
    // Grass, high meadow, rock and sand to start with
    layers = {
        { "Grass", "", glm::vec3(0.30f, 0.45f, 0.18f), 3.0f, 0.9f },
        { "Meadow", "", glm::vec3(0.45f, 0.50f, 0.25f), 4.0f, 0.9f },
        { "Rock", "", glm::vec3(0.42f, 0.40f, 0.37f), 6.0f, 0.75f },
        { "Sand", "", glm::vec3(0.72f, 0.64f, 0.46f), 3.0f, 0.95f },
    };
    InitSplat();
    FoliageType grass; grass.name = "Grass"; grass.builtIn = 0; grass.color = glm::vec4(0.45f, 0.62f, 0.25f, 1.0f);
    grass.density = 6.0f; grass.castShadows = false; grass.drawDistance = 60.0f; grass.minScale = 0.7f; grass.maxScale = 1.3f;
    FoliageType flowers; flowers.name = "Flowers"; flowers.builtIn = 1; flowers.density = 1.5f; flowers.castShadows = false;
    flowers.drawDistance = 50.0f;
    FoliageType bush; bush.name = "Bush"; bush.builtIn = 2; bush.color = glm::vec4(0.35f, 0.50f, 0.22f, 1.0f); bush.density = 0.15f;
    bush.minScale = 0.7f; bush.maxScale = 1.4f;
    FoliageType tree; tree.name = "Tree"; tree.builtIn = 3; tree.density = 0.02f; tree.alignToSlope = false; tree.collision = true;
    tree.minScale = 0.8f; tree.maxScale = 1.3f; tree.drawDistance = 400.0f; tree.maxSlope = 30.0f;
    FoliageType pine; pine.name = "Pine"; pine.builtIn = 4; pine.density = 0.02f; pine.alignToSlope = false; pine.collision = true;
    pine.minScale = 0.8f; pine.maxScale = 1.4f; pine.drawDistance = 400.0f; pine.maxSlope = 30.0f;
    FoliageType rock; rock.name = "Rock"; rock.builtIn = 5; rock.color = glm::vec4(0.55f, 0.53f, 0.50f, 1.0f); rock.density = 0.05f;
    rock.wind = false; rock.collision = true; rock.minScale = 0.4f; rock.maxScale = 1.6f; rock.maxSlope = 60.0f; rock.drawDistance = 250.0f;
    foliageTypes = { grass, flowers, bush, tree, pine, rock };
}

std::shared_ptr<Node> TerrainNode::Clone() const {
    auto clone = std::make_shared<TerrainNode>(*this);
    clone->isMeshDirty = true;
    return clone;
}

void TerrainNode::InitGrid() {
    gridResolution = std::clamp(gridResolution, 8u, 512u);
    heightData.assign(gridResolution * gridResolution, 0.0f);
    isMeshDirty = true;
}

void TerrainNode::SetResolution(uint32_t newRes) {
    newRes = std::clamp(newRes, 8u, 512u);
    if (newRes == gridResolution && heightData.size() == newRes * newRes) return;

    std::vector<float> oldData = heightData;
    uint32_t oldRes = gridResolution;

    gridResolution = newRes;
    heightData.assign(newRes * newRes, 0.0f);

    if (oldData.size() == oldRes * oldRes && oldRes >= 2) {
        // Resample old elevation data to new grid resolution
        for (uint32_t j = 0; j < newRes; ++j) {
            float v = (float)j / (float)(newRes - 1);
            float oldFz = v * (float)(oldRes - 1);
            int z0 = std::clamp((int)std::floor(oldFz), 0, (int)oldRes - 2);
            int z1 = z0 + 1;
            float tz = oldFz - (float)z0;

            for (uint32_t i = 0; i < newRes; ++i) {
                float u = (float)i / (float)(newRes - 1);
                float oldFx = u * (float)(oldRes - 1);
                int x0 = std::clamp((int)std::floor(oldFx), 0, (int)oldRes - 2);
                int x1 = x0 + 1;
                float tx = oldFx - (float)x0;

                float h00 = oldData[z0 * oldRes + x0];
                float h10 = oldData[z0 * oldRes + x1];
                float h01 = oldData[z1 * oldRes + x0];
                float h11 = oldData[z1 * oldRes + x1];

                float h0 = glm::mix(h00, h10, tx);
                float h1 = glm::mix(h01, h11, tx);
                heightData[j * newRes + i] = glm::mix(h0, h1, tz);
            }
        }
    }

    isMeshDirty = true;
}

void TerrainNode::GenerateProcedural(TerrainPreset preset) {
    uint32_t N = gridResolution;
    heightData.assign(N * N, 0.0f);

    switch (preset) {
        case TerrainPreset::FlatPlains:
            std::fill(heightData.begin(), heightData.end(), 0.0f);
            break;

        case TerrainPreset::RollingHills:
            for (uint32_t j = 0; j < N; ++j) {
                for (uint32_t i = 0; i < N; ++i) {
                    float nx = (float)i / (float)N * 6.2831853f * 2.0f;
                    float nz = (float)j / (float)N * 6.2831853f * 2.0f;
                    float h = 0.45f + 0.28f * std::sin(nx * 0.75f + nz * 0.5f)
                                   + 0.15f * std::cos(nx * 1.6f - nz * 1.1f)
                                   + 0.07f * std::sin(nx * 3.2f + nz * 2.8f);
                    heightData[j * N + i] = std::clamp(h, 0.0f, 1.0f);
                }
            }
            break;

        case TerrainPreset::MountainPeaks:
            for (uint32_t j = 0; j < N; ++j) {
                for (uint32_t i = 0; i < N; ++i) {
                    float nx = (float)i / (float)N * 3.14159265f * 2.0f;
                    float nz = (float)j / (float)N * 3.14159265f * 2.0f;
                    float ridge1 = 1.0f - std::abs(std::sin(nx * 1.1f + nz * 0.7f));
                    float ridge2 = 1.0f - std::abs(std::cos(nx * 2.2f - nz * 1.5f));
                    float peak = std::pow(ridge1 * 0.7f + ridge2 * 0.3f, 2.2f);
                    float detail = 0.08f * std::sin(nx * 5.5f) * std::cos(nz * 5.5f);
                    heightData[j * N + i] = std::clamp(peak + detail, 0.0f, 1.0f);
                }
            }
            break;

        case TerrainPreset::CanyonsAndRavines:
            for (uint32_t j = 0; j < N; ++j) {
                for (uint32_t i = 0; i < N; ++i) {
                    float nx = (float)i / (float)N * 6.2831853f;
                    float nz = (float)j / (float)N * 6.2831853f;
                    float plateau = 0.75f + 0.05f * std::sin(nx * 2.0f);
                    float winding = std::sin(nz * 1.2f) * 0.35f;
                    float distToRiver = std::abs(((float)i / (float)N - 0.5f) - winding);
                    float canyonDepth = glm::smoothstep(0.04f, 0.22f, distToRiver);
                    float h = plateau * canyonDepth;
                    heightData[j * N + i] = std::clamp(h, 0.0f, 1.0f);
                }
            }
            break;
    }

    isMeshDirty = true;
}

bool TerrainNode::LoadFromHeightmap(const std::string& path) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 1);
    if (!pixels) {
        std::string alt = "assets/" + path;
        pixels = stbi_load(alt.c_str(), &w, &h, &channels, 1);
    }
    if (!pixels) return false;

    uint32_t N = gridResolution;
    heightData.assign(N * N, 0.0f);
    heightmapPath = path;

    for (uint32_t j = 0; j < N; ++j) {
        int srcY = std::clamp((int)(j * h / N), 0, h - 1);
        for (uint32_t i = 0; i < N; ++i) {
            int srcX = std::clamp((int)(i * w / N), 0, w - 1);
            heightData[j * N + i] = (float)pixels[srcY * w + srcX] / 255.0f;
        }
    }

    stbi_image_free(pixels);
    isMeshDirty = true;
    return true;
}

float TerrainNode::SampleHeightLocal(float localX, float localZ) const {
    uint32_t N = gridResolution;
    if (N < 2 || heightData.size() != N * N) return 0.0f;

    float halfW = terrainSize.x * 0.5f;
    float halfL = terrainSize.y * 0.5f;

    float u = (localX + halfW) / terrainSize.x;
    float v = (localZ + halfL) / terrainSize.y;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0.0f;
    }

    float fx = u * (float)(N - 1);
    float fz = v * (float)(N - 1);
    int x0 = std::clamp((int)std::floor(fx), 0, (int)N - 2);
    int z0 = std::clamp((int)std::floor(fz), 0, (int)N - 2);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float tx = fx - (float)x0;
    float tz = fz - (float)z0;

    float h00 = heightData[z0 * N + x0];
    float h10 = heightData[z0 * N + x1];
    float h01 = heightData[z1 * N + x0];
    float h11 = heightData[z1 * N + x1];

    float h0 = glm::mix(h00, h10, tx);
    float h1 = glm::mix(h01, h11, tx);
    return glm::mix(h0, h1, tz) * heightScale;
}

float TerrainNode::SampleHeight(float worldX, float worldZ, const Scene& scene) const {
    glm::mat4 invWorld = glm::inverse(GetWorldTransformMatrix(scene));
    glm::vec4 localP = invWorld * glm::vec4(worldX, 0.0f, worldZ, 1.0f);
    float localH = SampleHeightLocal(localP.x, localP.z);
    glm::vec4 worldH = GetWorldTransformMatrix(scene) * glm::vec4(localP.x, localH, localP.z, 1.0f);
    return worldH.y;
}

glm::vec3 TerrainNode::SampleNormal(float worldX, float worldZ, const Scene& scene) const {
    const float eps = 0.5f;
    float hCenter = SampleHeight(worldX, worldZ, scene);
    float hRight = SampleHeight(worldX + eps, worldZ, scene);
    float hUp = SampleHeight(worldX, worldZ + eps, scene);

    glm::vec3 vx(eps, hRight - hCenter, 0.0f);
    glm::vec3 vz(0.0f, hUp - hCenter, eps);
    return glm::normalize(glm::cross(vz, vx));
}

void TerrainNode::ApplyBrushLocal(float localX, float localZ, float dt) {
    uint32_t N = gridResolution;
    if (N < 2 || heightData.size() != N * N) return;

    float halfW = terrainSize.x * 0.5f;
    float halfL = terrainSize.y * 0.5f;

    float rad = std::max(0.5f, brushRadius);
    float radSq = rad * rad;

    for (uint32_t j = 0; j < N; ++j) {
        float vz = -halfL + (float)j * (terrainSize.y / (float)(N - 1));
        float dz = vz - localZ;
        if (std::abs(dz) > rad) continue;

        for (uint32_t i = 0; i < N; ++i) {
            float vx = -halfW + (float)i * (terrainSize.x / (float)(N - 1));
            float dx = vx - localX;
            float distSq = dx * dx + dz * dz;
            if (distSq > radSq) continue;

            float dist = std::sqrt(distSq);
            float falloff = std::cos((dist / rad) * 1.5707963f);
            float factor = falloff * brushStrength * dt;

            uint32_t idx = j * N + i;
            float curH = heightData[idx];

            switch (brushAction) {
                case TerrainBrushAction::Raise:
                    heightData[idx] = std::clamp(curH + factor * (1.0f / std::max(1.0f, heightScale)), 0.0f, 1.0f);
                    break;

                case TerrainBrushAction::Lower:
                    heightData[idx] = std::clamp(curH - factor * (1.0f / std::max(1.0f, heightScale)), 0.0f, 1.0f);
                    break;

                case TerrainBrushAction::Smooth: {
                    float hL = (i > 0) ? heightData[j * N + (i - 1)] : curH;
                    float hR = (i < N - 1) ? heightData[j * N + (i + 1)] : curH;
                    float hD = (j > 0) ? heightData[(j - 1) * N + i] : curH;
                    float hU = (j < N - 1) ? heightData[(j + 1) * N + i] : curH;
                    float avg = (hL + hR + hD + hU) * 0.25f;
                    heightData[idx] = glm::mix(curH, avg, std::clamp(factor * 3.0f, 0.0f, 1.0f));
                    break;
                }

                case TerrainBrushAction::Flatten: {
                    heightData[idx] = glm::mix(curH, flattenTargetHeight, std::clamp(factor * 3.0f, 0.0f, 1.0f));
                    break;
                }
            }
        }
    }

    isMeshDirty = true;
}

void TerrainNode::ApplyBrush(float centerWorldX, float centerWorldZ, float dt, const Scene& scene) {
    glm::mat4 invWorld = glm::inverse(GetWorldTransformMatrix(scene));
    glm::vec4 localP = invWorld * glm::vec4(centerWorldX, 0.0f, centerWorldZ, 1.0f);
    ApplyToolLocal(localP.x, localP.z, dt);
}

// ---------------- Paint layers ----------------

void TerrainNode::InitSplat() {
    splatResolution = std::clamp(splatResolution, 16u, 2048u);
    splatData.assign(static_cast<size_t>(splatResolution) * splatResolution * 4, 0);
    for (size_t i = 0; i < splatData.size(); i += 4) splatData[i] = 255; // all the first layer
    ++splatVersion;
}

glm::vec4 TerrainNode::SampleSplatLocal(float localX, float localZ) const {
    const uint32_t N = splatResolution;
    if (N < 2 || splatData.size() != static_cast<size_t>(N) * N * 4) return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    const float u = std::clamp((localX + terrainSize.x * 0.5f) / terrainSize.x, 0.0f, 1.0f) * (N - 1);
    const float v = std::clamp((localZ + terrainSize.y * 0.5f) / terrainSize.y, 0.0f, 1.0f) * (N - 1);
    const uint32_t x = static_cast<uint32_t>(u + 0.5f), z = static_cast<uint32_t>(v + 0.5f);
    const uint8_t* p = &splatData[(static_cast<size_t>(z) * N + x) * 4];
    glm::vec4 w(p[0], p[1], p[2], p[3]);
    const float sum = w.x + w.y + w.z + w.w;
    return sum > 0.0f ? w / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
}

void TerrainNode::PaintLayerLocal(float localX, float localZ, int layer, float dt) {
    const uint32_t N = splatResolution;
    if (splatData.size() != static_cast<size_t>(N) * N * 4) InitSplat();
    layer = std::clamp(layer, 0, kMaxLayers - 1);
    const float rad = std::max(0.25f, brushRadius);
    const float texelX = terrainSize.x / (N - 1), texelZ = terrainSize.y / (N - 1);
    const int cx = static_cast<int>((localX + terrainSize.x * 0.5f) / texelX + 0.5f);
    const int cz = static_cast<int>((localZ + terrainSize.y * 0.5f) / texelZ + 0.5f);
    const int rx = static_cast<int>(rad / texelX) + 1, rz = static_cast<int>(rad / texelZ) + 1;
    bool changed = false;
    for (int z = std::max(cz - rz, 0); z <= std::min(cz + rz, static_cast<int>(N) - 1); ++z) {
        for (int x = std::max(cx - rx, 0); x <= std::min(cx + rx, static_cast<int>(N) - 1); ++x) {
            const float dx = x * texelX - terrainSize.x * 0.5f - localX;
            const float dz = z * texelZ - terrainSize.y * 0.5f - localZ;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > rad) continue;
            // Soft-edged brush: the target layer gains weight, the others give it up in proportion
            const float amount = std::clamp(std::cos(dist / rad * 1.5707963f) * brushStrength * dt, 0.0f, 1.0f);
            uint8_t* p = &splatData[(static_cast<size_t>(z) * N + x) * 4];
            glm::vec4 w = glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
            const float sum = std::max(w.x + w.y + w.z + w.w, 1e-4f);
            w /= sum;
            glm::vec4 target(0.0f);
            target[layer] = 1.0f;
            w = glm::mix(w, target, amount);
            for (int c = 0; c < 4; ++c) {
                const uint8_t value = static_cast<uint8_t>(std::clamp(w[c], 0.0f, 1.0f) * 255.0f + 0.5f);
                if (value != p[c]) { p[c] = value; changed = true; }
            }
        }
    }
    if (changed) ++splatVersion;
}

void TerrainNode::AutoPaint() {
    const uint32_t N = splatResolution;
    if (splatData.size() != static_cast<size_t>(N) * N * 4) InitSplat();
    float lowest = 1e9f, highest = -1e9f;
    for (float h : heightData) { lowest = std::min(lowest, h); highest = std::max(highest, h); }
    const float range = std::max(highest - lowest, 1e-4f);
    for (uint32_t z = 0; z < N; ++z) {
        for (uint32_t x = 0; x < N; ++x) {
            const float lx = x * terrainSize.x / (N - 1) - terrainSize.x * 0.5f;
            const float lz = z * terrainSize.y / (N - 1) - terrainSize.y * 0.5f;
            const float e = 0.5f;
            const float hc = SampleHeightLocal(lx, lz);
            const glm::vec3 n = glm::normalize(glm::vec3(SampleHeightLocal(lx - e, lz) - SampleHeightLocal(lx + e, lz), 2.0f * e,
                                                         SampleHeightLocal(lx, lz - e) - SampleHeightLocal(lx, lz + e)));
            const float slope = glm::degrees(std::acos(std::clamp(n.y, 0.0f, 1.0f)));
            const float height = (hc / std::max(heightScale, 1e-4f) - lowest) / range;
            // Smooth bands so the layers blend at their edges
            auto band = [](float x, float a, float b) { return std::clamp((x - a) / std::max(b - a, 1e-4f), 0.0f, 1.0f); };
            const float rock = band(slope, 28.0f, 40.0f);
            const float high = band(height, 0.55f, 0.75f) * (1.0f - rock);
            const float low = band(height, 0.12f, 0.02f) * (1.0f - rock) * (1.0f - high);
            const float base = std::max(1.0f - rock - high - low, 0.0f);
            uint8_t* p = &splatData[(static_cast<size_t>(z) * N + x) * 4];
            const float w[4] = { base, high, rock, low };
            const float sum = std::max(w[0] + w[1] + w[2] + w[3], 1e-4f);
            for (int c = 0; c < 4; ++c) p[c] = static_cast<uint8_t>(w[c] / sum * 255.0f + 0.5f);
        }
    }
    ++splatVersion;
}

// ---------------- Foliage ----------------

namespace {
uint32_t NextRandom(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
float Random01(uint32_t& seed) {
    return static_cast<float>(NextRandom(seed) >> 8) / 16777216.0f;
}
}

void TerrainNode::PaintFoliageLocal(float localX, float localZ, float dt, bool erase) {
    if (foliageTypes.empty()) return;
    const int typeIndex = std::clamp(activeFoliage, 0, static_cast<int>(foliageTypes.size()) - 1);
    const FoliageType& type = foliageTypes[typeIndex];
    const float rad = std::max(0.25f, brushRadius);
    if (erase) {
        const size_t before = foliage.size();
        foliage.erase(std::remove_if(foliage.begin(), foliage.end(), [&](const FoliageInstance& f) {
            if (!eraseAllFoliage && static_cast<int>(f.type) != typeIndex) return false;
            const glm::vec2 d = f.local - glm::vec2(localX, localZ);
            if (glm::dot(d, d) > rad * rad) return false;
            // Thin out gradually toward the edge of the brush
            return Random01(foliageSeed) < std::clamp(brushStrength * dt * 4.0f, 0.0f, 1.0f);
        }), foliage.end());
        if (foliage.size() != before) NotifyFoliageChanged();
        return;
    }
    // Fill toward the type's density: new instances only where the area is below it, and never
    // closer than half the average spacing to one already there
    const float area = 3.14159265f * rad * rad;
    int existing = 0;
    for (const FoliageInstance& f : foliage) {
        if (static_cast<int>(f.type) != typeIndex) continue;
        const glm::vec2 d = f.local - glm::vec2(localX, localZ);
        if (glm::dot(d, d) <= rad * rad) ++existing;
    }
    const float wanted = type.density * area;
    const int attempts = static_cast<int>(std::min(std::max(wanted - existing, 0.0f), wanted * brushStrength * dt * 2.0f + 1.0f));
    const float minSpacing = 0.5f / std::sqrt(std::max(type.density, 1e-3f));
    bool added = false;
    for (int a = 0; a < attempts; ++a) {
        const float angle = Random01(foliageSeed) * 6.2831853f;
        const float r = std::sqrt(Random01(foliageSeed)) * rad;
        const glm::vec2 p(localX + std::cos(angle) * r, localZ + std::sin(angle) * r);
        if (std::abs(p.x) > terrainSize.x * 0.5f || std::abs(p.y) > terrainSize.y * 0.5f) continue;
        const float e = 0.5f;
        const glm::vec3 n = glm::normalize(glm::vec3(SampleHeightLocal(p.x - e, p.y) - SampleHeightLocal(p.x + e, p.y), 2.0f * e,
                                                     SampleHeightLocal(p.x, p.y - e) - SampleHeightLocal(p.x, p.y + e)));
        if (glm::degrees(std::acos(std::clamp(n.y, 0.0f, 1.0f))) > type.maxSlope) continue;
        bool crowded = false;
        for (const FoliageInstance& f : foliage) {
            if (static_cast<int>(f.type) != typeIndex) continue;
            const glm::vec2 d = f.local - p;
            if (glm::dot(d, d) < minSpacing * minSpacing) { crowded = true; break; }
        }
        if (crowded) continue;
        FoliageInstance f;
        f.local = p;
        f.rotation = Random01(foliageSeed) * 6.2831853f;
        f.scale = glm::mix(type.minScale, type.maxScale, Random01(foliageSeed));
        f.type = static_cast<uint32_t>(typeIndex);
        foliage.push_back(f);
        added = true;
    }
    if (added) NotifyFoliageChanged();
}

void TerrainNode::RebuildFoliageCache(const Scene& scene) const {
    const glm::mat4 world = GetWorldTransformMatrix(scene);
    const uint64_t stamp = m_foliageVersion * 1000003ull + m_heightVersion * 7919ull + foliage.size() + foliageTypes.size() * 131ull;
    if (stamp == m_foliageStamp && world == m_foliageWorld && m_foliageMatrices.size() == foliage.size()) return;
    m_foliageStamp = stamp;
    m_foliageWorld = world;
    m_foliageMatrices.resize(foliage.size());
    m_foliageCells.clear();
    constexpr float kCell = 16.0f;
    const int cellsX = std::max(1, static_cast<int>(std::ceil(terrainSize.x / kCell)));
    const int cellsZ = std::max(1, static_cast<int>(std::ceil(terrainSize.y / kCell)));
    std::vector<std::vector<uint32_t>> grid(static_cast<size_t>(cellsX) * cellsZ);
    const float scaleY = glm::length(glm::vec3(world[1]));
    for (size_t i = 0; i < foliage.size(); ++i) {
        const FoliageInstance& f = foliage[i];
        const FoliageType* type = f.type < foliageTypes.size() ? &foliageTypes[f.type] : nullptr;
        const float h = SampleHeightLocal(f.local.x, f.local.y);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (type && type->alignToSlope) {
            const float e = 0.5f;
            const glm::vec3 n = glm::normalize(glm::vec3(SampleHeightLocal(f.local.x - e, f.local.y) - SampleHeightLocal(f.local.x + e, f.local.y), 2.0f * e,
                                                         SampleHeightLocal(f.local.x, f.local.y - e) - SampleHeightLocal(f.local.x, f.local.y + e)));
            up = glm::normalize(glm::mix(up, n, 0.7f));
        }
        // Sunk a little so the base never floats on a slope
        const glm::vec3 base(f.local.x, h - 0.05f * f.scale, f.local.y);
        glm::vec3 side = glm::normalize(glm::cross(up, glm::vec3(std::sin(f.rotation), 0.0f, std::cos(f.rotation))) + glm::vec3(1e-5f, 0.0f, 0.0f));
        glm::vec3 forward = glm::cross(side, up);
        glm::mat4 local(glm::vec4(side * f.scale, 0.0f), glm::vec4(up * f.scale, 0.0f), glm::vec4(forward * f.scale, 0.0f), glm::vec4(base, 1.0f));
        m_foliageMatrices[i] = world * local;
        const int gx = std::clamp(static_cast<int>((f.local.x + terrainSize.x * 0.5f) / kCell), 0, cellsX - 1);
        const int gz = std::clamp(static_cast<int>((f.local.y + terrainSize.y * 0.5f) / kCell), 0, cellsZ - 1);
        grid[static_cast<size_t>(gz) * cellsX + gx].push_back(static_cast<uint32_t>(i));
    }
    for (auto& cell : grid) {
        if (cell.empty()) continue;
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (uint32_t i : cell) {
            const glm::vec3 p(m_foliageMatrices[i][3]);
            const float size = glm::length(glm::vec3(m_foliageMatrices[i][1])) * 12.0f + 1.0f; // tall trees reach up
            lo = glm::min(lo, p - glm::vec3(size * 0.5f, 0.0f, size * 0.5f));
            hi = glm::max(hi, p + glm::vec3(size * 0.5f, size * scaleY, size * 0.5f));
        }
        FoliageCell c;
        c.center = (lo + hi) * 0.5f;
        c.radius = glm::length(hi - lo) * 0.5f;
        c.instances = std::move(cell);
        m_foliageCells.push_back(std::move(c));
    }
}

void TerrainNode::CollectFoliage(const Scene& scene, const glm::vec3& cameraPos, const std::function<bool(const glm::vec3&, float)>& visible,
                                 std::vector<std::vector<glm::mat4>>& outPerType, bool shadowCastersOnly) const {
    outPerType.assign(foliageTypes.size(), {});
    if (foliage.empty() || foliageTypes.empty()) return;
    RebuildFoliageCache(scene);
    float farthest = 0.0f;
    for (const FoliageType& t : foliageTypes) farthest = std::max(farthest, t.drawDistance);
    for (const FoliageCell& cell : m_foliageCells) {
        if (glm::distance(cell.center, cameraPos) - cell.radius > farthest) continue;
        if (visible && !visible(cell.center, cell.radius)) continue;
        for (uint32_t i : cell.instances) {
            const uint32_t t = foliage[i].type;
            if (t >= foliageTypes.size()) continue;
            const FoliageType& type = foliageTypes[t];
            if (shadowCastersOnly && !type.castShadows) continue;
            const glm::vec3 p(m_foliageMatrices[i][3]);
            const glm::vec3 d = p - cameraPos;
            if (glm::dot(d, d) > type.drawDistance * type.drawDistance) continue;
            outPerType[t].push_back(m_foliageMatrices[i]);
        }
    }
}

std::vector<std::pair<uint32_t, glm::mat4>> TerrainNode::CollidingFoliage(const Scene& scene) const {
    std::vector<std::pair<uint32_t, glm::mat4>> out;
    RebuildFoliageCache(scene);
    for (size_t i = 0; i < foliage.size(); ++i) {
        const uint32_t t = foliage[i].type;
        if (t < foliageTypes.size() && foliageTypes[t].collision) out.emplace_back(t, m_foliageMatrices[i]);
    }
    return out;
}

bool TerrainNode::Raycast(const glm::vec3& origin, const glm::vec3& direction, const Scene& scene, glm::vec3& outHit) const {
    const glm::mat4 world = GetWorldTransformMatrix(scene);
    const glm::mat4 inv = glm::inverse(world);
    const glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
    const glm::vec3 d = glm::vec3(inv * glm::vec4(direction, 0.0f));
    if (glm::length(d) < 1e-8f) return false;
    // Clip the ray to the terrain's box, then march it in steps of half a grid cell and refine
    const glm::vec3 lo(-terrainSize.x * 0.5f, -1.0f, -terrainSize.y * 0.5f);
    const glm::vec3 hi(terrainSize.x * 0.5f, heightScale + 1.0f, terrainSize.y * 0.5f);
    float t0 = 0.0f, t1 = 1e9f;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-9f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return false;
            continue;
        }
        float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    if (t0 > t1) return false;
    const float cell = std::min(terrainSize.x, terrainSize.y) / std::max(gridResolution - 1u, 1u);
    const float step = cell * 0.5f / glm::length(d);
    auto above = [&](float t) {
        const glm::vec3 p = o + d * t;
        return p.y - SampleHeightLocal(p.x, p.z);
    };
    float prevT = t0, prevA = above(t0);
    if (prevA < 0.0f) { outHit = glm::vec3(world * glm::vec4(o + d * t0, 1.0f)); return true; }
    for (float t = t0 + step; t <= t1 + step; t += step) {
        const float tc = std::min(t, t1);
        const float a = above(tc);
        if (a <= 0.0f) {
            float ta = prevT, tb = tc;
            for (int i = 0; i < 20; ++i) {
                const float tm = 0.5f * (ta + tb);
                if (above(tm) > 0.0f) ta = tm; else tb = tm;
            }
            outHit = glm::vec3(world * glm::vec4(o + d * (0.5f * (ta + tb)), 1.0f));
            return true;
        }
        prevT = tc;
        prevA = a;
        if (tc >= t1) break;
    }
    return false;
}

void TerrainNode::ApplyToolLocal(float localX, float localZ, float dt) {
    switch (tool) {
        case TerrainTool::Sculpt: ApplyBrushLocal(localX, localZ, dt); break;
        case TerrainTool::Paint: PaintLayerLocal(localX, localZ, paintLayer, dt); break;
        case TerrainTool::Foliage: PaintFoliageLocal(localX, localZ, dt, false); break;
        case TerrainTool::EraseFoliage: PaintFoliageLocal(localX, localZ, dt, true); break;
    }
}


MeshData TerrainNode::GenerateMeshData() const {
    MeshData mesh;
    uint32_t N = gridResolution;
    if (heightData.size() != N * N) {
        return mesh;
    }

    mesh.vertices.resize(N * N);
    mesh.indices.reserve((N - 1) * (N - 1) * 6);

    float halfW = terrainSize.x * 0.5f;
    float halfL = terrainSize.y * 0.5f;
    float stepX = terrainSize.x / (float)(N - 1);
    float stepZ = terrainSize.y / (float)(N - 1);

    for (uint32_t j = 0; j < N; ++j) {
        float v = (float)j / (float)(N - 1);
        float z = -halfL + (float)j * stepZ;

        for (uint32_t i = 0; i < N; ++i) {
            float u = (float)i / (float)(N - 1);
            float x = -halfW + (float)i * stepX;
            float normH = heightData[j * N + i];
            float y = normH * heightScale;

            float hL = (i > 0) ? heightData[j * N + (i - 1)] : normH;
            float hR = (i < N - 1) ? heightData[j * N + (i + 1)] : normH;
            float hD = (j > 0) ? heightData[(j - 1) * N + i] : normH;
            float hU = (j < N - 1) ? heightData[(j + 1) * N + i] : normH;

            float dX = (hR - hL) * heightScale;
            float dZ = (hU - hD) * heightScale;
            glm::vec3 n = glm::normalize(glm::vec3(-dX / (stepX * 2.0f), 1.0f, -dZ / (stepZ * 2.0f)));

            Vertex vert{};
            vert.pos = glm::vec3(x, y, z);
            vert.normal = n;
            vert.texCoord = glm::vec2(u * uvTiling, v * uvTiling);

            mesh.vertices[j * N + i] = vert;
        }
    }

    for (uint32_t j = 0; j < N - 1; ++j) {
        for (uint32_t i = 0; i < N - 1; ++i) {
            uint32_t topLeft = j * N + i;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (j + 1) * N + i;
            uint32_t bottomRight = bottomLeft + 1;

            mesh.indices.push_back(topLeft);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(topRight);

            mesh.indices.push_back(topRight);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(bottomRight);
        }
    }

    return mesh;
}

void TerrainNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Terrain Landscape Generator & Sculptor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "1-Click Procedural Landscapes:");
        float halfBtnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Rolling Hills", ImVec2(halfBtnW, 26))) {
            GenerateProcedural(TerrainPreset::RollingHills);
        }
        ImGui::SameLine();
        if (ImGui::Button("Mountain Peaks", ImVec2(halfBtnW, 26))) {
            GenerateProcedural(TerrainPreset::MountainPeaks);
        }
        if (ImGui::Button("Canyons & Ravines", ImVec2(halfBtnW, 26))) {
            GenerateProcedural(TerrainPreset::CanyonsAndRavines);
        }
        ImGui::SameLine();
        if (ImGui::Button("Flatten Plains", ImVec2(halfBtnW, 26))) {
            GenerateProcedural(TerrainPreset::FlatPlains);
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Terrain Dimensions:");

        const char* resOptions[] = { "32 x 32", "64 x 64", "128 x 128", "256 x 256" };
        int curResIdx = (gridResolution <= 32) ? 0 : (gridResolution <= 64) ? 1 : (gridResolution <= 128) ? 2 : 3;
        ImGui::Text("Grid Resolution:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##GridRes", &curResIdx, resOptions, IM_ARRAYSIZE(resOptions))) {
            uint32_t targetRes = (curResIdx == 0) ? 32 : (curResIdx == 1) ? 64 : (curResIdx == 2) ? 128 : 256;
            SetResolution(targetRes);
        }

        ImGui::Text("Size (X, Z) in meters:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat2("##TerrainSize", glm::value_ptr(terrainSize), 1.0f, 10.0f, 5000.0f, "%.1f m")) {
            isMeshDirty = true;
        }

        ImGui::Text("Max Elevation:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##Elevation", &heightScale, 0.5f, 1.0f, 500.0f, "%.1f m")) {
            isMeshDirty = true;
        }

        ImGui::Text("UV Tiling:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##UVTiling", &uvTiling, 0.5f, 1.0f, 128.0f, "%.1f")) {
            isMeshDirty = true;
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Heightmap Image Import:");
        char hMapBuf[260];
        strncpy(hMapBuf, heightmapPath.c_str(), sizeof(hMapBuf) - 1);
        hMapBuf[sizeof(hMapBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 68.0f);
        if (ImGui::InputText("##HMapPath", hMapBuf, sizeof(hMapBuf))) {
            heightmapPath = hMapBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Load", ImVec2(60.0f, 0))) {
            if (!heightmapPath.empty()) {
                LoadFromHeightmap(heightmapPath);
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Brush (drag in the viewport):");
        ImGui::Checkbox("Brush Active", &sculptingEnabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("While ticked, dragging with the left mouse button over the terrain\nsculpts, paints or places foliage, with the tool chosen below.");
        const char* tools[] = { "Sculpt", "Paint Layers", "Place Foliage", "Erase Foliage" };
        int curTool = static_cast<int>(tool);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##TerrainTool", &curTool, tools, IM_ARRAYSIZE(tools))) tool = static_cast<TerrainTool>(curTool);

        ImGui::Text("Brush Radius:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##BrushRadius", &brushRadius, 0.5f, 50.0f, "%.1f m");
        ImGui::Text("Brush Strength:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##BrushStrength", &brushStrength, 0.1f, 10.0f, "%.1f");

        if (tool == TerrainTool::Sculpt) {
            const char* brushActions[] = { "Raise (Hills)", "Lower (Ravines)", "Smooth", "Flatten (Plateaus)" };
            int curAction = static_cast<int>(brushAction);
            ImGui::Text("Sculpt:");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##BrushAction", &curAction, brushActions, IM_ARRAYSIZE(brushActions))) {
                brushAction = static_cast<TerrainBrushAction>(curAction);
            }
            if (brushAction == TerrainBrushAction::Flatten) {
                ImGui::Text("Flatten Target Height:");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##FlattenHeight", &flattenTargetHeight, 0.0f, 1.0f, "%.2f");
            }
            if (ImGui::Button("Smooth Entire Terrain")) {
                uint32_t N = gridResolution;
                for (uint32_t step = 0; step < 2; ++step) {
                    auto temp = heightData;
                    for (uint32_t j = 1; j < N - 1; ++j) {
                        for (uint32_t i = 1; i < N - 1; ++i) {
                            float avg = (temp[j * N + (i - 1)] + temp[j * N + (i + 1)] +
                                         temp[(j - 1) * N + i] + temp[(j + 1) * N + i]) * 0.25f;
                            heightData[j * N + i] = glm::mix(heightData[j * N + i], avg, 0.5f);
                        }
                    }
                }
                isMeshDirty = true;
            }
        }

        if (tool == TerrainTool::Paint) {
            ImGui::Text("Layer to paint:");
            for (int l = 0; l < static_cast<int>(layers.size()) && l < kMaxLayers; ++l) {
                if (l > 0) ImGui::SameLine();
                std::string label = layers[l].name.empty() ? ("Layer " + std::to_string(l + 1)) : layers[l].name;
                if (ImGui::RadioButton((label + "##paintlayer" + std::to_string(l)).c_str(), paintLayer == l)) paintLayer = l;
            }
            if (ImGui::Button("Auto Paint from Slope & Height")) AutoPaint();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer 1 on gentle ground, 2 high up, 3 on steep slopes, 4 along the lowest ground.");
            ImGui::SameLine();
            if (ImGui::Button("Clear Paint")) InitSplat();
        }

        if (tool == TerrainTool::Foliage || tool == TerrainTool::EraseFoliage) {
            ImGui::Text("Foliage type:");
            for (int f = 0; f < static_cast<int>(foliageTypes.size()); ++f) {
                if (f > 0 && f % 3 != 0) ImGui::SameLine();
                if (ImGui::RadioButton((foliageTypes[f].name + "##foliagetype" + std::to_string(f)).c_str(), activeFoliage == f)) activeFoliage = f;
            }
            if (tool == TerrainTool::EraseFoliage) ImGui::Checkbox("Erase all types", &eraseAllFoliage);
            ImGui::Text("%zu placed", foliage.size());
            ImGui::SameLine();
            if (ImGui::Button("Clear All Foliage")) { foliage.clear(); NotifyFoliageChanged(); }
        }
    }

    if (ImGui::CollapsingHeader("Terrain Paint Layers")) {
        for (int l = 0; l < static_cast<int>(layers.size()) && l < kMaxLayers; ++l) {
            TerrainLayer& layer = layers[l];
            ImGui::PushID(l);
            char nameBuf[64];
            strncpy(nameBuf, layer.name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##LayerName", nameBuf, sizeof(nameBuf))) layer.name = nameBuf;
            ImGui::SameLine();
            ImGui::ColorEdit3("##LayerTint", glm::value_ptr(layer.tint), ImGuiColorEditFlags_NoInputs);
            char texBuf[260];
            strncpy(texBuf, layer.texturePath.c_str(), sizeof(texBuf) - 1);
            texBuf[sizeof(texBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##LayerTexture", "texture (optional: plain colour without)", texBuf, sizeof(texBuf))) layer.texturePath = texBuf;
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_TEXTURE_PATH")) {
                    layer.texturePath = std::string(static_cast<const char*>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Tiling (m)", &layer.tiling, 0.25f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::SliderFloat("Rough", &layer.roughness, 0.02f, 1.0f, "%.2f");
            ImGui::Separator();
            ImGui::PopID();
        }
        if (layers.size() < static_cast<size_t>(kMaxLayers) && ImGui::Button("Add Layer")) {
            layers.push_back({ "Layer " + std::to_string(layers.size() + 1), "", glm::vec3(0.5f), 4.0f, 0.85f });
        }
    }

    if (ImGui::CollapsingHeader("Terrain Foliage Types")) {
        const char* meshes[] = { "Grass", "Flowers", "Bush", "Broadleaf Tree", "Pine", "Rock" };
        for (int f = 0; f < static_cast<int>(foliageTypes.size()); ++f) {
            FoliageType& type = foliageTypes[f];
            ImGui::PushID(1000 + f);
            if (ImGui::TreeNode((type.name + "##type").c_str())) {
                char nameBuf[64];
                strncpy(nameBuf, type.name.c_str(), sizeof(nameBuf) - 1);
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) type.name = nameBuf;
                int meshChoice = type.builtIn < 0 ? 6 : type.builtIn;
                const char* meshesWithModel[] = { "Grass", "Flowers", "Bush", "Broadleaf Tree", "Pine", "Rock", "Model..." };
                if (ImGui::Combo("Mesh", &meshChoice, meshesWithModel, IM_ARRAYSIZE(meshesWithModel))) {
                    type.builtIn = meshChoice == 6 ? -1 : meshChoice;
                    NotifyFoliageChanged();
                }
                if (type.builtIn < 0) {
                    char modelBuf[260];
                    strncpy(modelBuf, type.modelMesh.c_str(), sizeof(modelBuf) - 1);
                    modelBuf[sizeof(modelBuf) - 1] = '\0';
                    if (ImGui::InputTextWithHint("Model Mesh", "imported model's mesh name", modelBuf, sizeof(modelBuf))) type.modelMesh = modelBuf;
                }
                ImGui::ColorEdit3("Colour", glm::value_ptr(type.color));
                if (ImGui::DragFloatRange2("Scale", &type.minScale, &type.maxScale, 0.01f, 0.05f, 20.0f, "%.2f")) NotifyFoliageChanged();
                ImGui::SliderFloat("Density (per m\xC2\xB2)", &type.density, 0.005f, 20.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Max Slope", &type.maxSlope, 0.0f, 90.0f, "%.0f deg");
                if (ImGui::Checkbox("Lean with Slope", &type.alignToSlope)) NotifyFoliageChanged();
                ImGui::SameLine();
                ImGui::Checkbox("Sway in Wind", &type.wind);
                ImGui::Checkbox("Cast Shadows", &type.castShadows);
                ImGui::SameLine();
                ImGui::Checkbox("Solid (Play)", &type.collision);
                ImGui::SliderFloat("Draw Distance", &type.drawDistance, 10.0f, 1000.0f, "%.0f m");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        (void)meshes;
        if (ImGui::Button("Add Foliage Type")) {
            FoliageType t;
            t.name = "Type " + std::to_string(foliageTypes.size() + 1);
            foliageTypes.push_back(t);
        }
    }
}

