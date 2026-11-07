#pragma once

#include "MeshletTypes.h"
#include <vector>
#include <glm/glm.hpp>

namespace Graphics {
namespace Meshlet {

class MeshletBuilder {
public:
    // Builds meshlets from arbitrary vertex and index buffers
    static MeshletModel BuildMeshlets(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t maxVertices = kMaxMeshletVertices,
        uint32_t maxTriangles = kMaxMeshletTriangles
    );

    // Evaluates GPU-style frustum and cluster backface cone culling
    static MeshletCullResult CullMeshlets(
        const MeshletModel& model,
        const glm::mat4& worldTransform,
        const glm::vec3& cameraWorldPos,
        const glm::mat4& viewProjMatrix,
        const MeshletSettings& settings = MeshletSettings{}
    );

    // Computes tight bounding sphere for a subset of vertices
    static void ComputeBoundingSphere(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& vertexIndices,
        uint32_t vertexOffset,
        uint32_t vertexCount,
        glm::vec3& outCenter,
        float& outRadius
    );

    // Computes normal orientation bounding cone for cluster backface culling
    static void ComputeNormalCone(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& vertexIndices,
        const std::vector<MeshletTriangle>& triangles,
        uint32_t vertexOffset,
        uint32_t triangleOffset,
        uint32_t triangleCount,
        glm::vec3& outApex,
        glm::vec3& outAxis,
        float& outCutoff
    );
};

} // namespace Meshlet
} // namespace Graphics

