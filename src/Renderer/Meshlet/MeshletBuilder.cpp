#include "MeshletBuilder.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace Graphics {
namespace Meshlet {

MeshletModel MeshletBuilder::BuildMeshlets(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t maxVertices,
    uint32_t maxTriangles)
{
    MeshletModel model;
    model.originalVertexCount = static_cast<uint32_t>(vertices.size());
    model.originalTriangleCount = static_cast<uint32_t>(indices.size() / 3);

    if (vertices.empty() || indices.size() < 3) {
        return model;
    }

    maxVertices = std::min(maxVertices, kMaxMeshletVertices);
    maxTriangles = std::min(maxTriangles, kMaxMeshletTriangles);

    const size_t numTriangles = indices.size() / 3;
    std::vector<bool> visited(numTriangles, false);

    // Build vertex-to-triangle adjacency for greedy cluster growth
    std::vector<std::vector<uint32_t>> vertexToTriangles(vertices.size());
    for (uint32_t t = 0; t < numTriangles; ++t) {
        for (int j = 0; j < 3; ++j) {
            uint32_t v = indices[t * 3 + j];
            if (v < vertices.size()) {
                vertexToTriangles[v].push_back(t);
            }
        }
    }

    size_t processedTriangles = 0;
    while (processedTriangles < numTriangles) {
        // Find an unvisited triangle to seed the meshlet
        uint32_t seedTri = 0;
        for (size_t t = 0; t < numTriangles; ++t) {
            if (!visited[t]) {
                seedTri = static_cast<uint32_t>(t);
                break;
            }
        }

        Meshlet meshlet{};
        meshlet.vertexOffset = static_cast<uint32_t>(model.vertexIndices.size());
        meshlet.triangleOffset = static_cast<uint32_t>(model.triangles.size());

        std::unordered_map<uint32_t, uint8_t> localVertexMap;
        std::vector<uint32_t> localVertices;
        std::vector<MeshletTriangle> localTriangles;

        std::vector<uint32_t> candidates;
        candidates.push_back(seedTri);

        while (!candidates.empty() && localTriangles.size() < maxTriangles) {
            uint32_t currentTri = candidates.back();
            candidates.pop_back();

            if (visited[currentTri]) continue;

            uint32_t v0 = indices[currentTri * 3 + 0];
            uint32_t v1 = indices[currentTri * 3 + 1];
            uint32_t v2 = indices[currentTri * 3 + 2];

            // Count new vertices required
            uint32_t newVerts = 0;
            if (localVertexMap.find(v0) == localVertexMap.end()) newVerts++;
            if (localVertexMap.find(v1) == localVertexMap.end()) newVerts++;
            if (localVertexMap.find(v2) == localVertexMap.end()) newVerts++;

            if (localVertices.size() + newVerts > maxVertices) {
                // Triangle does not fit in this meshlet
                continue;
            }

            // Accept triangle
            visited[currentTri] = true;
            processedTriangles++;

            auto getOrAddLocal = [&](uint32_t globalIdx) -> uint8_t {
                auto it = localVertexMap.find(globalIdx);
                if (it != localVertexMap.end()) return it->second;
                uint8_t localIdx = static_cast<uint8_t>(localVertices.size());
                localVertexMap[globalIdx] = localIdx;
                localVertices.push_back(globalIdx);
                return localIdx;
            };

            MeshletTriangle tri;
            tri.i0 = getOrAddLocal(v0);
            tri.i1 = getOrAddLocal(v1);
            tri.i2 = getOrAddLocal(v2);
            localTriangles.push_back(tri);

            // Add adjacent unvisited triangles to candidates
            for (uint32_t v : {v0, v1, v2}) {
                if (v < vertexToTriangles.size()) {
                    for (uint32_t adjTri : vertexToTriangles[v]) {
                        if (!visited[adjTri]) {
                            candidates.push_back(adjTri);
                        }
                    }
                }
            }
        }

        meshlet.vertexCount = static_cast<uint32_t>(localVertices.size());
        meshlet.triangleCount = static_cast<uint32_t>(localTriangles.size());

        // Append to global model arrays
        model.vertexIndices.insert(model.vertexIndices.end(), localVertices.begin(), localVertices.end());
        model.triangles.insert(model.triangles.end(), localTriangles.begin(), localTriangles.end());

        // Calculate spatial bounds and normal cone
        ComputeBoundingSphere(
            vertices,
            model.vertexIndices,
            meshlet.vertexOffset,
            meshlet.vertexCount,
            meshlet.center,
            meshlet.radius
        );

        ComputeNormalCone(
            vertices,
            model.vertexIndices,
            model.triangles,
            meshlet.vertexOffset,
            meshlet.triangleOffset,
            meshlet.triangleCount,
            meshlet.coneApex,
            meshlet.coneAxis,
            meshlet.coneCutoff
        );

        model.meshlets.push_back(meshlet);
    }

    return model;
}

void MeshletBuilder::ComputeBoundingSphere(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& vertexIndices,
    uint32_t vertexOffset,
    uint32_t vertexCount,
    glm::vec3& outCenter,
    float& outRadius)
{
    if (vertexCount == 0) {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.0f;
        return;
    }

    // Centroid
    glm::vec3 sum(0.0f);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        uint32_t vIdx = vertexIndices[vertexOffset + i];
        sum += vertices[vIdx].pos;
    }
    outCenter = sum / static_cast<float>(vertexCount);

    // Max distance
    float maxDistSq = 0.0f;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        uint32_t vIdx = vertexIndices[vertexOffset + i];
        glm::vec3 diff = vertices[vIdx].pos - outCenter;
        float d2 = glm::dot(diff, diff);
        if (d2 > maxDistSq) {
            maxDistSq = d2;
        }
    }
    outRadius = std::sqrt(maxDistSq);
}

void MeshletBuilder::ComputeNormalCone(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& vertexIndices,
    const std::vector<MeshletTriangle>& triangles,
    uint32_t vertexOffset,
    uint32_t triangleOffset,
    uint32_t triangleCount,
    glm::vec3& outApex,
    glm::vec3& outAxis,
    float& outCutoff)
{
    if (triangleCount == 0) {
        outApex = glm::vec3(0.0f);
        outAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        outCutoff = 1.0f;
        return;
    }

    glm::vec3 normalSum(0.0f);
    glm::vec3 centroidSum(0.0f);
    std::vector<glm::vec3> triNormals;
    triNormals.reserve(triangleCount);

    for (uint32_t i = 0; i < triangleCount; ++i) {
        const auto& tri = triangles[triangleOffset + i];
        uint32_t vi0 = vertexIndices[vertexOffset + tri.i0];
        uint32_t vi1 = vertexIndices[vertexOffset + tri.i1];
        uint32_t vi2 = vertexIndices[vertexOffset + tri.i2];

        const glm::vec3& p0 = vertices[vi0].pos;
        const glm::vec3& p1 = vertices[vi1].pos;
        const glm::vec3& p2 = vertices[vi2].pos;

        glm::vec3 triCenter = (p0 + p1 + p2) / 3.0f;
        centroidSum += triCenter;

        glm::vec3 e0 = p1 - p0;
        glm::vec3 e1 = p2 - p0;
        glm::vec3 n = glm::cross(e0, e1);
        float len2 = glm::dot(n, n);
        if (len2 > 1e-8f) {
            n = glm::normalize(n);
        } else {
            n = vertices[vi0].normal;
        }

        triNormals.push_back(n);
        normalSum += n;
    }

    outApex = centroidSum / static_cast<float>(triangleCount);

    float normLen2 = glm::dot(normalSum, normalSum);
    if (normLen2 > 1e-8f) {
        outAxis = glm::normalize(normalSum);
    } else {
        outAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Find maximum angular deviation of any triangle normal from the cone axis
    float maxAngle = 0.0f;
    for (const auto& n : triNormals) {
        float cosAngle = glm::clamp(glm::dot(n, outAxis), -1.0f, 1.0f);
        float angle = std::acos(cosAngle);
        if (angle > maxAngle) {
            maxAngle = angle;
        }
    }

    constexpr float kPi = 3.14159265358979323846f;
    if (maxAngle >= kPi * 0.5f) {
        // Cone spans more than a hemisphere; cannot be backface culled
        outCutoff = 1.0f;
    } else {
        // cos(angle + pi/2) = -sin(angle)
        outCutoff = -std::sin(maxAngle);
    }
}

MeshletCullResult MeshletBuilder::CullMeshlets(
    const MeshletModel& model,
    const glm::mat4& worldTransform,
    const glm::vec3& cameraWorldPos,
    const glm::mat4& viewProjMatrix,
    const MeshletSettings& settings)
{
    MeshletCullResult result;
    result.totalMeshlets = static_cast<uint32_t>(model.meshlets.size());

    // Extract frustum planes from view-projection matrix
    glm::vec4 planes[6];
    // Left
    planes[0] = glm::vec4(viewProjMatrix[0][3] + viewProjMatrix[0][0],
                          viewProjMatrix[1][3] + viewProjMatrix[1][0],
                          viewProjMatrix[2][3] + viewProjMatrix[2][0],
                          viewProjMatrix[3][3] + viewProjMatrix[3][0]);
    // Right
    planes[1] = glm::vec4(viewProjMatrix[0][3] - viewProjMatrix[0][0],
                          viewProjMatrix[1][3] - viewProjMatrix[1][0],
                          viewProjMatrix[2][3] - viewProjMatrix[2][0],
                          viewProjMatrix[3][3] - viewProjMatrix[3][0]);
    // Bottom
    planes[2] = glm::vec4(viewProjMatrix[0][3] + viewProjMatrix[0][1],
                          viewProjMatrix[1][3] + viewProjMatrix[1][1],
                          viewProjMatrix[2][3] + viewProjMatrix[2][1],
                          viewProjMatrix[3][3] + viewProjMatrix[3][1]);
    // Top
    planes[3] = glm::vec4(viewProjMatrix[0][3] - viewProjMatrix[0][1],
                          viewProjMatrix[1][3] - viewProjMatrix[1][1],
                          viewProjMatrix[2][3] - viewProjMatrix[2][1],
                          viewProjMatrix[3][3] - viewProjMatrix[3][1]);
    // Near
    planes[4] = glm::vec4(viewProjMatrix[0][2],
                          viewProjMatrix[1][2],
                          viewProjMatrix[2][2],
                          viewProjMatrix[3][2]);
    // Far
    planes[5] = glm::vec4(viewProjMatrix[0][3] - viewProjMatrix[0][2],
                          viewProjMatrix[1][3] - viewProjMatrix[1][2],
                          viewProjMatrix[2][3] - viewProjMatrix[2][2],
                          viewProjMatrix[3][3] - viewProjMatrix[3][2]);

    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 1e-8f) planes[i] /= len;
    }

    // Normal transform matrix
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    // Scale factor for sphere radius
    float scaleX = glm::length(glm::vec3(worldTransform[0]));
    float scaleY = glm::length(glm::vec3(worldTransform[1]));
    float scaleZ = glm::length(glm::vec3(worldTransform[2]));
    float maxScale = std::max(scaleX, std::max(scaleY, scaleZ));

    for (uint32_t idx = 0; idx < model.meshlets.size(); ++idx) {
        const auto& meshlet = model.meshlets[idx];

        // 1. Frustum Culling
        if (settings.enableFrustumCulling) {
            glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(meshlet.center, 1.0f));
            float worldRadius = meshlet.radius * maxScale;

            bool inside = true;
            for (int p = 0; p < 6; ++p) {
                float dist = glm::dot(glm::vec3(planes[p]), worldCenter) + planes[p].w;
                if (dist < -worldRadius) {
                    inside = false;
                    break;
                }
            }

            if (!inside) {
                result.frustumCulled++;
                continue;
            }
        }

        // 2. Normal Cone Backface Culling
        if (settings.enableConeCulling && meshlet.coneCutoff < 0.999f) {
            glm::vec3 worldApex = glm::vec3(worldTransform * glm::vec4(meshlet.coneApex, 1.0f));
            glm::vec3 worldAxis = glm::normalize(normalMat * meshlet.coneAxis);

            glm::vec3 toApex = worldApex - cameraWorldPos;
            float dist = glm::length(toApex);
            if (dist > 1e-4f) {
                glm::vec3 viewDir = toApex / dist;
                // If dot(viewDir, normalAxis) >= cutoff, entire meshlet is facing away from camera
                if (glm::dot(viewDir, worldAxis) >= meshlet.coneCutoff) {
                    result.backfaceConeCulled++;
                    continue;
                }
            }
        }

        result.passedMeshlets++;
        result.visibleMeshletIndices.push_back(idx);
    }

    return result;
}

} // namespace Meshlet
} // namespace Graphics
