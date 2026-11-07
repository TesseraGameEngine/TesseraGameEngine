#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

struct Node;
class Scene;

// What particles collide with (ParticleNode's collision module, the ocean spray, and the GPU
// particle simulation, which is sent the same shapes): solid meshes as boxes or spheres, terrain
// heightfields, an optional ground plane, and water (oceans and liquids), which particles sink into.
class ParticleColliders {
public:
    struct Solid {
        glm::mat4 toLocal;  // world -> the unit box / sphere (half size 0.5)
        glm::mat4 toWorld;
        bool sphere{false};
        bool water{false};  // a liquid's contents (the box's top is the surface)
    };
    struct Terrain {
        glm::mat4 toLocal;
        glm::mat4 toWorld;
        const std::vector<float>* heights{nullptr}; // normalised, resolution^2
        uint32_t resolution{0};
        glm::vec2 size{0.0f};
        float heightScale{1.0f};
        uint32_t nodeId{0};
        uint64_t version{0}; // changes when the heights do
    };

    struct Settings {
        bool solids{true};
        bool terrain{true};
        bool water{true};
        bool groundPlane{false};
        float groundHeight{0.0f};
        uint32_t excludeNodeId{0}; // the emitter itself
    };

    // Gathers the shapes overlapping the world box [regionMin, regionMax]
    void Build(const Scene& scene, const glm::vec3& regionMin, const glm::vec3& regionMax, const Settings& settings);
    void Clear();
    bool Empty() const { return m_solids.empty() && m_terrains.empty() && !m_groundPlane && m_oceans.empty(); }

    struct Hit {
        glm::vec3 position{0.0f}; // on the surface, pushed out by the radius
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float t{1.0f};            // how far along from -> to
        bool water{false};
    };
    // The first surface the path from -> to (a sphere of the given radius) meets
    bool Sweep(const glm::vec3& from, const glm::vec3& to, float radius, Hit& hit) const;

    const std::vector<Solid>& Solids() const { return m_solids; }
    const std::vector<Terrain>& Terrains() const { return m_terrains; }
    bool HasGroundPlane() const { return m_groundPlane; }
    float GroundHeight() const { return m_groundHeight; }
    // The ocean's surface on a grid over the region (for the GPU, which can't run the waves itself):
    // heights[res * z + x], covering [originXZ, originXZ + extentXZ]
    bool SampleWaterGrid(uint32_t resolution, glm::vec2& originXZ, glm::vec2& extentXZ, std::vector<float>& heights) const;

    static constexpr size_t kMaxSolids = 64;
    static constexpr size_t kMaxTerrains = 4;

private:
    struct Ocean {
        std::shared_ptr<Node> node;
        float minX, minZ, maxX, maxZ;
    };
    float TerrainHeightLocal(const Terrain& terrain, float x, float z, bool& inside) const;
    float OceanHeight(float x, float z, bool& inside) const;

    const Scene* m_scene{nullptr};
    std::vector<Solid> m_solids;
    std::vector<Terrain> m_terrains;
    std::vector<Ocean> m_oceans;
    bool m_groundPlane{false};
    float m_groundHeight{0.0f};
    glm::vec3 m_regionMin{0.0f};
    glm::vec3 m_regionMax{0.0f};
    float m_time{0.0f};
};
