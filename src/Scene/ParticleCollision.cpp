#include "ParticleCollision.h"
#include "Scene/Scene.h"
#include "Scene/OceanWaves.h"
#include "Scene/Nodes/3D/TerrainNode.h"
#include <algorithm>
#include <cmath>

namespace {

// A node's mesh as a transform of the unit box (models: their bounds; planes: a thin slab under them)
bool SolidFrame(const Node& node, const Scene& scene, glm::mat4& frame) {
    frame = node.GetWorldTransformMatrix(scene);
    if (node.type == ShapeType::Custom) {
        const glm::vec3 size = glm::max(node.boundsMax - node.boundsMin, glm::vec3(1e-4f));
        frame = frame * glm::mat4(glm::vec4(size.x, 0, 0, 0), glm::vec4(0, size.y, 0, 0), glm::vec4(0, 0, size.z, 0),
                                  glm::vec4((node.boundsMax + node.boundsMin) * 0.5f, 1));
    } else if (node.type == ShapeType::Plane) {
        frame = frame * glm::mat4(glm::vec4(2.0f, 0, 0, 0), glm::vec4(0, 0.02f, 0, 0), glm::vec4(0, 0, 2.0f, 0), glm::vec4(0, -0.01f, 0, 1));
    } else if (node.type == ShapeType::Quad) {
        return false; // flat cards (signs, sprites): not solid
    }
    return std::abs(glm::determinant(glm::mat3(frame))) > 1e-12f;
}

void FrameBounds(const glm::mat4& frame, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f);
        const glm::vec3 p = glm::vec3(frame * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

// World-space box of a node's mesh
void NodeBounds(const Node& node, const Scene& scene, glm::vec3& lo, glm::vec3& hi) {
    glm::vec3 localMin(-0.5f), localMax(0.5f);
    if (node.type == ShapeType::Custom) {
        localMin = node.boundsMin;
        localMax = node.boundsMax;
    }
    const glm::mat4 world = node.GetWorldTransformMatrix(scene);
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? localMax.x : localMin.x, (c & 2) ? localMax.y : localMin.y, (c & 4) ? localMax.z : localMin.z);
        const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

bool Overlaps(const glm::vec3& aLo, const glm::vec3& aHi, const glm::vec3& bLo, const glm::vec3& bHi) {
    return !(glm::any(glm::lessThan(aHi, bLo)) || glm::any(glm::greaterThan(aLo, bHi)));
}

} // namespace

void ParticleColliders::Clear() {
    m_solids.clear();
    m_terrains.clear();
    m_oceans.clear();
    m_groundPlane = false;
    m_scene = nullptr;
}

void ParticleColliders::Build(const Scene& scene, const glm::vec3& regionMin, const glm::vec3& regionMax, const Settings& settings) {
    Clear();
    m_scene = &scene;
    m_regionMin = regionMin;
    m_regionMax = regionMax;
    m_groundPlane = settings.groundPlane;
    m_groundHeight = settings.groundHeight;
    m_time = OceanWaves::Time();
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->id == settings.excludeNodeId || node->IsUINode()) continue;
        if (node->nodeType == NodeType::Terrain) {
            if (!settings.terrain || m_terrains.size() >= kMaxTerrains) continue;
            const auto* terrain = dynamic_cast<const TerrainNode*>(node.get());
            if (!terrain || terrain->gridResolution < 2 || terrain->heightData.size() != terrain->gridResolution * terrain->gridResolution) continue;
            const glm::mat4 world = terrain->GetWorldTransformMatrix(scene);
            const glm::mat4 box = world * glm::mat4(glm::vec4(terrain->terrainSize.x, 0, 0, 0), glm::vec4(0, terrain->heightScale + 1.0f, 0, 0),
                                                    glm::vec4(0, 0, terrain->terrainSize.y, 0), glm::vec4(0, terrain->heightScale * 0.5f, 0, 1));
            glm::vec3 lo, hi;
            FrameBounds(box, lo, hi);
            lo.y = -1e30f; // anything below the land is under it
            if (!Overlaps(lo, hi, regionMin, regionMax)) continue;
            Terrain t;
            t.toWorld = world;
            t.toLocal = glm::inverse(world);
            t.heights = &terrain->heightData;
            t.resolution = terrain->gridResolution;
            t.size = terrain->terrainSize;
            t.heightScale = terrain->heightScale;
            t.nodeId = terrain->id;
            t.version = terrain->HeightVersion() * 1000003ull + reinterpret_cast<uintptr_t>(terrain->heightData.data());
            m_terrains.push_back(t);
            continue;
        }
        if (node->nodeType != NodeType::Mesh || node->isFluidVolume) continue;
        if (node->isOcean) {
            if (!settings.water) continue;
            const glm::vec3 center = node->GetWorldPosition(scene);
            const glm::vec2 half = glm::max(node->oceanSize, glm::vec2(1.0f)) * 0.5f;
            if (center.x + half.x < regionMin.x || center.x - half.x > regionMax.x || center.z + half.y < regionMin.z || center.z - half.y > regionMax.z) continue;
            m_oceans.push_back({ node, center.x - half.x, center.z - half.y, center.x + half.x, center.z + half.y });
            continue;
        }
        if (node->isLiquid) {
            // The liquid in a container, up to its fill level
            if (!settings.water || m_solids.size() >= kMaxSolids) continue;
            glm::vec3 lo, hi;
            NodeBounds(*node, scene, lo, hi);
            hi.y = lo.y + (hi.y - lo.y) * std::clamp(node->liquidFill, 0.0f, 1.0f);
            if (hi.y - lo.y < 1e-3f || !Overlaps(lo, hi, regionMin, regionMax)) continue;
            const glm::vec3 size = hi - lo;
            const glm::mat4 frame(glm::vec4(size.x, 0, 0, 0), glm::vec4(0, size.y, 0, 0), glm::vec4(0, 0, size.z, 0), glm::vec4((lo + hi) * 0.5f, 1));
            m_solids.push_back({ glm::inverse(frame), frame, false, true });
            continue;
        }
        if (!settings.solids || m_solids.size() >= kMaxSolids) continue;
        glm::mat4 frame;
        if (!SolidFrame(*node, scene, frame)) continue;
        glm::vec3 lo, hi;
        FrameBounds(frame, lo, hi);
        if (!Overlaps(lo, hi, regionMin, regionMax)) continue;
        m_solids.push_back({ glm::inverse(frame), frame, node->type == ShapeType::Sphere, false });
    }
}

float ParticleColliders::TerrainHeightLocal(const Terrain& terrain, float x, float z, bool& inside) const {
    const uint32_t n = terrain.resolution;
    const float u = (x + terrain.size.x * 0.5f) / terrain.size.x;
    const float v = (z + terrain.size.y * 0.5f) / terrain.size.y;
    inside = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
    if (!inside) return 0.0f;
    const float fx = u * static_cast<float>(n - 1);
    const float fz = v * static_cast<float>(n - 1);
    const int x0 = std::clamp(static_cast<int>(fx), 0, static_cast<int>(n) - 2);
    const int z0 = std::clamp(static_cast<int>(fz), 0, static_cast<int>(n) - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const std::vector<float>& h = *terrain.heights;
    const float h0 = glm::mix(h[z0 * n + x0], h[z0 * n + x0 + 1], tx);
    const float h1 = glm::mix(h[(z0 + 1) * n + x0], h[(z0 + 1) * n + x0 + 1], tx);
    return glm::mix(h0, h1, tz) * terrain.heightScale;
}

float ParticleColliders::OceanHeight(float x, float z, bool& inside) const {
    for (const Ocean& ocean : m_oceans) {
        if (x < ocean.minX || x > ocean.maxX || z < ocean.minZ || z > ocean.maxZ) continue;
        inside = true;
        return OceanWaves::SampleSurface(*ocean.node, *m_scene, x, z, m_time).height;
    }
    inside = false;
    return 0.0f;
}

bool ParticleColliders::Sweep(const glm::vec3& from, const glm::vec3& to, float radius, Hit& hit) const {
    bool found = false;
    hit.t = 2.0f;
    const glm::vec3 delta = to - from;
    auto consider = [&](float t, const glm::vec3& normal, bool water) {
        if (t < hit.t) {
            hit.t = t;
            hit.normal = normal;
            hit.water = water;
            found = true;
        }
    };

    if (m_groundPlane) {
        const float level = m_groundHeight + radius;
        if (to.y < level) {
            const float t = from.y > level ? (from.y - level) / std::max(from.y - to.y, 1e-6f) : 0.0f;
            consider(t, glm::vec3(0.0f, 1.0f, 0.0f), false);
        }
    }

    for (const Solid& solid : m_solids) {
        const glm::vec3 a = glm::vec3(solid.toLocal * glm::vec4(from, 1.0f));
        const glm::vec3 b = glm::vec3(solid.toLocal * glm::vec4(to, 1.0f));
        // The particle's radius in the shape's (scaled) space, per axis
        const glm::vec3 scale(glm::length(glm::vec3(solid.toWorld[0])), glm::length(glm::vec3(solid.toWorld[1])), glm::length(glm::vec3(solid.toWorld[2])));
        const glm::vec3 pad = solid.water ? glm::vec3(0.0f) : glm::vec3(radius) / glm::max(scale, glm::vec3(1e-4f));
        const glm::vec3 d = b - a;
        glm::vec3 normalLocal(0.0f);
        float t = -1.0f;
        if (solid.sphere) {
            const float r = 0.5f + (pad.x + pad.y + pad.z) / 3.0f;
            const float c = glm::dot(a, a) - r * r;
            if (c <= 0.0f) {
                // Starts inside: out along the shortest way
                t = 0.0f;
                normalLocal = glm::length(a) > 1e-5f ? glm::normalize(a) : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                const float qa = glm::dot(d, d);
                const float qb = glm::dot(a, d);
                const float disc = qb * qb - qa * c;
                if (qa > 1e-12f && disc >= 0.0f) {
                    const float root = (-qb - std::sqrt(disc)) / qa;
                    if (root >= 0.0f && root <= 1.0f) {
                        t = root;
                        normalLocal = glm::normalize(a + d * root);
                    }
                }
            }
        } else {
            const glm::vec3 half = glm::vec3(0.5f) + pad;
            const glm::vec3 depth = half - glm::abs(a);
            if (depth.x > 0.0f && depth.y > 0.0f && depth.z > 0.0f) {
                t = 0.0f;
                const int axis = solid.water ? 1 : ((depth.x < depth.y && depth.x < depth.z) ? 0 : (depth.y < depth.z ? 1 : 2));
                normalLocal[axis] = a[axis] < 0.0f && !solid.water ? -1.0f : 1.0f;
            } else {
                // Slab test: where the path enters the box
                float tEnter = 0.0f, tExit = 1.0f;
                int enterAxis = -1;
                bool miss = false;
                for (int i = 0; i < 3 && !miss; ++i) {
                    if (std::abs(d[i]) < 1e-9f) {
                        if (std::abs(a[i]) > half[i]) miss = true;
                        continue;
                    }
                    float t0 = (-half[i] - a[i]) / d[i];
                    float t1 = (half[i] - a[i]) / d[i];
                    if (t0 > t1) std::swap(t0, t1);
                    if (t0 > tEnter) { tEnter = t0; enterAxis = i; }
                    tExit = std::min(tExit, t1);
                    if (tEnter > tExit) miss = true;
                }
                if (!miss && enterAxis >= 0) {
                    t = tEnter;
                    normalLocal[enterAxis] = d[enterAxis] > 0.0f ? -1.0f : 1.0f;
                }
            }
        }
        if (t >= 0.0f) {
            const glm::vec3 n = glm::normalize(glm::mat3(glm::transpose(solid.toLocal)) * normalLocal);
            consider(t, n, solid.water);
        }
    }

    for (const Terrain& terrain : m_terrains) {
        const glm::vec3 a = glm::vec3(terrain.toLocal * glm::vec4(from, 1.0f));
        const glm::vec3 b = glm::vec3(terrain.toLocal * glm::vec4(to, 1.0f));
        const float localRadius = radius / std::max(glm::length(glm::vec3(terrain.toWorld[1])), 1e-4f);
        bool insideB = false;
        const float hb = TerrainHeightLocal(terrain, b.x, b.z, insideB);
        if (!insideB || b.y >= hb + localRadius) continue;
        bool insideA = false;
        const float ha = TerrainHeightLocal(terrain, a.x, a.z, insideA);
        float t = 0.0f;
        if (insideA && a.y >= ha + localRadius) {
            // Bisect for where the path goes under the ground
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 10; ++i) {
                const float mid = (lo + hi) * 0.5f;
                const glm::vec3 p = a + (b - a) * mid;
                bool inside = false;
                const float h = TerrainHeightLocal(terrain, p.x, p.z, inside);
                if (inside && p.y < h + localRadius) hi = mid;
                else lo = mid;
            }
            t = lo;
        }
        const glm::vec3 p = a + (b - a) * t;
        const float e = std::max(terrain.size.x, terrain.size.y) / static_cast<float>(terrain.resolution - 1);
        bool in = false;
        const float hx0 = TerrainHeightLocal(terrain, p.x - e, p.z, in);
        const float hx1 = TerrainHeightLocal(terrain, p.x + e, p.z, in);
        const float hz0 = TerrainHeightLocal(terrain, p.x, p.z - e, in);
        const float hz1 = TerrainHeightLocal(terrain, p.x, p.z + e, in);
        const glm::vec3 normalLocal = glm::normalize(glm::vec3(hx0 - hx1, 2.0f * e, hz0 - hz1));
        consider(t, glm::normalize(glm::mat3(glm::transpose(terrain.toLocal)) * normalLocal), false);
    }

    if (!m_oceans.empty()) {
        bool insideB = false;
        const float hb = OceanHeight(to.x, to.z, insideB);
        if (insideB && to.y < hb) {
            bool insideA = false;
            const float ha = OceanHeight(from.x, from.z, insideA);
            float t = 0.0f;
            if (insideA && from.y >= ha) t = (from.y - ha) / std::max((from.y - ha) - (to.y - hb), 1e-6f);
            consider(t, glm::vec3(0.0f, 1.0f, 0.0f), true);
        }
    }

    if (!found) return false;
    hit.t = std::clamp(hit.t, 0.0f, 1.0f);
    hit.position = from + delta * hit.t;
    if (!hit.water) {
        // Out onto the surface (a little off it), whichever side the shape's test put it
        for (const Solid& solid : m_solids) {
            if (solid.water) continue;
            glm::vec3 local = glm::vec3(solid.toLocal * glm::vec4(hit.position, 1.0f));
            const glm::vec3 scale(glm::length(glm::vec3(solid.toWorld[0])), glm::length(glm::vec3(solid.toWorld[1])), glm::length(glm::vec3(solid.toWorld[2])));
            const glm::vec3 half = glm::vec3(0.5f) + glm::vec3(radius) / glm::max(scale, glm::vec3(1e-4f));
            if (solid.sphere) {
                const float r = (half.x + half.y + half.z) / 3.0f;
                const float len = glm::length(local);
                if (len < r && len > 1e-6f) local *= (r + 1e-3f) / len;
                else continue;
            } else {
                const glm::vec3 depth = half - glm::abs(local);
                if (depth.x <= 0.0f || depth.y <= 0.0f || depth.z <= 0.0f) continue;
                const int axis = (depth.x < depth.y && depth.x < depth.z) ? 0 : (depth.y < depth.z ? 1 : 2);
                local[axis] = (local[axis] < 0.0f ? -1.0f : 1.0f) * (half[axis] + 1e-3f);
            }
            hit.position = glm::vec3(solid.toWorld * glm::vec4(local, 1.0f));
        }
        hit.position += hit.normal * 1e-3f;
        if (m_groundPlane) hit.position.y = std::max(hit.position.y, m_groundHeight + radius);
        for (const Terrain& terrain : m_terrains) {
            glm::vec3 local = glm::vec3(terrain.toLocal * glm::vec4(hit.position, 1.0f));
            const float localRadius = radius / std::max(glm::length(glm::vec3(terrain.toWorld[1])), 1e-4f);
            bool inside = false;
            const float h = TerrainHeightLocal(terrain, local.x, local.z, inside);
            if (inside && local.y < h + localRadius) {
                local.y = h + localRadius + 1e-3f;
                hit.position = glm::vec3(terrain.toWorld * glm::vec4(local, 1.0f));
            }
        }
    }
    return true;
}

bool ParticleColliders::SampleWaterGrid(uint32_t resolution, glm::vec2& originXZ, glm::vec2& extentXZ, std::vector<float>& heights) const {
    if (m_oceans.empty() || resolution < 2) return false;
    originXZ = glm::vec2(m_regionMin.x, m_regionMin.z);
    extentXZ = glm::max(glm::vec2(m_regionMax.x - m_regionMin.x, m_regionMax.z - m_regionMin.z), glm::vec2(0.01f));
    heights.resize(static_cast<size_t>(resolution) * resolution);
    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t x = 0; x < resolution; ++x) {
            const float wx = originXZ.x + extentXZ.x * static_cast<float>(x) / static_cast<float>(resolution - 1);
            const float wz = originXZ.y + extentXZ.y * static_cast<float>(z) / static_cast<float>(resolution - 1);
            bool inside = false;
            const float h = OceanHeight(wx, wz, inside);
            heights[z * resolution + x] = inside ? h : -1e9f;
        }
    }
    return true;
}
