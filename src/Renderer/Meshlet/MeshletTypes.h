#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "Renderer/RenderCommon.h"
#include "Scene/Mesh.h"

namespace Graphics {
namespace Meshlet {

constexpr uint32_t kMaxMeshletVertices = 64;
constexpr uint32_t kMaxMeshletTriangles = 126;

// Single GPU-ready meshlet primitive cluster
struct Meshlet {
    uint32_t vertexOffset{0};      // Offset into MeshletModel::vertexIndices
    uint32_t vertexCount{0};       // Number of unique vertices referenced (<= 64)
    uint32_t triangleOffset{0};    // Offset into MeshletModel::triangles
    uint32_t triangleCount{0};     // Number of micro-triangles (<= 126)

    // Spatial bounding sphere (in local object space)
    glm::vec3 center{0.0f};
    float radius{0.0f};

    // Normal bounding cone (for cluster backface culling)
    glm::vec3 coneApex{0.0f};      // Center/apex of the cone in object space
    glm::vec3 coneAxis{0.0f};      // Normalized average normal direction
    float coneCutoff{1.0f};        // cos(angle + pi/2): if dot(view, axis) >= cutoff, cull
};

// 3-byte micro-triangle indexing local meshlet vertices (0..vertexCount - 1)
struct MeshletTriangle {
    uint8_t i0{0};
    uint8_t i1{0};
    uint8_t i2{0};
};

// Complete meshlet representation of a 3D geometry model
struct MeshletModel {
    std::vector<Meshlet> meshlets;
    std::vector<uint32_t> vertexIndices;       // Indirection back to master Vertex buffer
    std::vector<MeshletTriangle> triangles;    // Packed micro-indices

    uint32_t originalVertexCount{0};
    uint32_t originalTriangleCount{0};

    size_t GetMemoryBytes() const {
        return (meshlets.size() * sizeof(Meshlet)) +
               (vertexIndices.size() * sizeof(uint32_t)) +
               (triangles.size() * sizeof(MeshletTriangle));
    }
};

// Results of cluster culling
struct MeshletCullResult {
    uint32_t totalMeshlets{0};
    uint32_t passedMeshlets{0};
    uint32_t frustumCulled{0};
    uint32_t backfaceConeCulled{0};
    std::vector<uint32_t> visibleMeshletIndices;

    float GetCullRatioPercent() const {
        if (totalMeshlets == 0) return 0.0f;
        return (static_cast<float>(frustumCulled + backfaceConeCulled) / static_cast<float>(totalMeshlets)) * 100.0f;
    }
};

// Runtime settings for Meshlet GPU Virtualization
struct MeshletSettings {
    bool enableMeshletPipeline{false};     // Master toggle for Meshlet GPU rendering
    bool enableConeCulling{true};          // Cluster backface cone culling
    bool enableFrustumCulling{true};       // Cluster bounding sphere frustum culling
    bool debugDrawMeshletBounds{false};    // Draw wireframe bounding spheres in viewport
};

// Real-time telemetry and hardware capabilities
struct MeshletStats {
    bool extensionSupported{false};
    std::string deviceName{""};
    uint32_t totalMeshletsActive{0};
    uint32_t visibleMeshletsLastFrame{0};
    uint32_t culledMeshletsLastFrame{0};
    float cullingEfficiencyPercent{0.0f};
    uint64_t gpuMemoryBytes{0};
    uint32_t maxPreferredMeshletVertices{64};
    uint32_t maxPreferredMeshletTriangles{126};
};

} // namespace Meshlet
} // namespace Graphics
