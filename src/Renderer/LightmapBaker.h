#pragma once

#include "Scene/Mesh.h"
#include "VulkanBuffer.h"
#include <glm/glm.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Baked lightmaps: indirect light (sky light and bounced light) stored per texel for static
// objects. Direct light stays real time, so lightmaps add the soft, detailed part a probe grid
// can't: dark corners, colour bleeding, light coming in through a doorway.
//
// 1. Unwrap: each lightmapped node's mesh is split into charts (connected triangles facing the
//    same axis) and laid flat at the node's own world scale.
// 2. PackAtlas: every chart of every node goes into one square atlas.
// 3. Baker: a CPU path tracer (all cores, in the background) fills the atlas: direct light at
//    every texel, then each bounce gathers light from the sky and from the previous bounce.
namespace Lightmap {

// Lightmap UVs for one node. Vertices are copies of the source mesh's vertices (sourceIndex),
// so a vertex shared by two charts can have two lightmap positions. Triangles keep the source
// mesh's order, so the lightmapped copy rasterises exactly like the original.
struct NodeLayout {
    uint32_t nodeId{0};
    std::string sourceKey;
    std::vector<uint32_t> sourceIndex; // per lightmap vertex: index into the source mesh
    std::vector<uint32_t> indices;     // triangles over lightmap vertices
    std::vector<glm::vec2> uv;         // per lightmap vertex: atlas UV (0..1), set by PackAtlas

    // Layout working data
    struct Chart {
        glm::vec2 minCorner{0.0f};
        glm::vec2 maxCorner{0.0f};
        glm::ivec2 origin{0};  // atlas texel of the chart's padded corner
    };
    std::vector<Chart> charts;
    std::vector<glm::vec2> chartCoord; // per lightmap vertex: position within its chart (metres)
    std::vector<uint32_t> vertexChart;
};

// Splits the mesh into charts and lays each out flat at worldScale (the node's scale)
NodeLayout Unwrap(const MeshData& mesh, const glm::vec3& worldScale);

// Places every chart in one square atlas at texelsPerMetre, lowered as needed to fit maxSize,
// and fills each layout's uv. Returns the atlas size (0 if there is nothing to pack).
uint32_t PackAtlas(std::vector<NodeLayout>& layouts, float& texelsPerMetre, uint32_t maxSize);

// The source mesh's vertices rearranged for the layout, with lightmap UVs
std::vector<Vertex> BuildVertices(const MeshData& source, const NodeLayout& layout);

// One object in the bake: a lightmapped node (atlasUV set) or a plain occluder / bouncer
struct BakeObject {
    std::vector<glm::vec3> positions; // world space
    std::vector<glm::vec3> normals;   // world space
    std::vector<glm::vec2> atlasUV;   // per vertex; empty when the object isn't lightmapped
    std::vector<uint32_t> indices;
    glm::vec3 albedo{0.8f};           // diffuse colour (already times 1 - metallic)
    glm::vec3 emissive{0.0f};
};

struct BakeSettings {
    uint32_t atlasSize{0};
    int samples{128};      // rays per texel per bounce
    int bounces{2};
    std::vector<LightData> lights;
    bool sky{false};       // procedural sky on: its sun light is dimmed near the horizon
    glm::vec3 skyDirection{0.0f, 1.0f, 0.0f};
    float giStrength{1.0f}; // how strongly indirect light lights surfaces (the shader's GI intensity)
    // Light from rays that leave the scene: a cube capture of the sky (6 faces of skyFaceSize^2,
    // GI bake face layout) or, when empty, a flat sky and ground colour
    std::vector<glm::vec3> skyCube;
    uint32_t skyFaceSize{0};
    glm::vec3 flatSky{0.16f, 0.22f, 0.32f};
    glm::vec3 flatGround{0.05f, 0.04f, 0.03f};
};

// Runs a bake on worker threads. Poll Done(); then Texels() holds the atlas packed as B10G11R11.
class Baker {
public:
    ~Baker() { Cancel(); }
    void Start(std::vector<BakeObject> objects, BakeSettings settings);
    void Cancel();
    bool Running() const { return m_thread.joinable() && !m_done.load(); }
    bool Done() const { return m_done.load(); }
    // Joins the worker; call once Done() is true
    void Finish() { if (m_thread.joinable()) m_thread.join(); }
    std::string Status() const;
    const std::vector<uint32_t>& Texels() const { return m_packed; }
    size_t TexelCount() const { return m_texelCount; }

private:
    void Run();

    std::vector<BakeObject> m_objects;
    BakeSettings m_settings;
    std::vector<uint32_t> m_packed;
    size_t m_texelCount{0};
    std::thread m_thread;
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_cancel{false};
    std::atomic<size_t> m_progress{0};
    std::atomic<size_t> m_progressTotal{1};
    mutable std::mutex m_stageMutex;
    std::string m_stage;
};

} // namespace Lightmap
