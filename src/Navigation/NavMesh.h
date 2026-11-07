#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "Scene/Scene.h"

namespace Navigation {

struct NavPoly {
    uint32_t id{0};
    glm::vec3 vertices[3]{ glm::vec3(0.0f) };
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool walkable{true};
    std::vector<uint32_t> neighbors;
};

class NavMesh {
public:
    // Builds a walkable triangle NavMesh from raw world-space triangles, filtering by maxSlopeDeg
    void BuildFromTriangles(const std::vector<glm::vec3>& triangleVertices, float maxSlopeDeg = 45.0f);

    // Builds a grid-triangulated walkable NavMesh from a Scene, carving out obstacle colliders
    void BuildFromScene(const Scene& scene,
                        const glm::vec3& boundsMin = glm::vec3(-20.0f, 0.0f, -20.0f),
                        const glm::vec3& boundsMax = glm::vec3(20.0f, 0.0f, 20.0f),
                        float cellSize = 2.0f,
                        float maxSlopeDeg = 45.0f);

    // Finds the closest walkable NavPoly to a 3D world position
    int FindNearestPoly(const glm::vec3& worldPos) const;

    // Computes an optimal smooth path from startPos to endPos using A* search over the NavMesh
    std::vector<glm::vec3> FindPath(const glm::vec3& startPos, const glm::vec3& endPos) const;

    size_t GetWalkablePolyCount() const;
    const std::vector<NavPoly>& GetPolys() const { return m_polys; }
    void Clear() { m_polys.clear(); }

private:
    void BuildAdjacency(float sharedVertexTolerance = 0.05f);

    std::vector<NavPoly> m_polys;
};

} // namespace Navigation

