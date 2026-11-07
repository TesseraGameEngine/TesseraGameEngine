#include "NavMesh.h"
#include <queue>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

namespace Navigation {

void NavMesh::BuildFromTriangles(const std::vector<glm::vec3>& triangleVertices, float maxSlopeDeg) {
    m_polys.clear();
    if (triangleVertices.size() < 3) return;

    float minUpDot = std::cos(glm::radians(std::clamp(maxSlopeDeg, 1.0f, 89.0f)));
    uint32_t nextId = 0;

    for (size_t i = 0; i + 2 < triangleVertices.size(); i += 3) {
        const glm::vec3& v0 = triangleVertices[i];
        const glm::vec3& v1 = triangleVertices[i + 1];
        const glm::vec3& v2 = triangleVertices[i + 2];

        glm::vec3 crossVec = glm::cross(v1 - v0, v2 - v0);
        float len = glm::length(crossVec);
        if (len < 1e-5f) continue;

        glm::vec3 normal = crossVec / len;
        if (normal.y < 0.0f) normal = -normal; // Two-sided floor normal check

        if (normal.y < minUpDot) continue; // Too steep to walk on

        NavPoly poly;
        poly.id = nextId++;
        poly.vertices[0] = v0;
        poly.vertices[1] = v1;
        poly.vertices[2] = v2;
        poly.center = (v0 + v1 + v2) / 3.0f;
        poly.normal = normal;
        poly.walkable = true;
        m_polys.push_back(poly);
    }

    BuildAdjacency();
}

void NavMesh::BuildFromScene(const Scene& scene,
                             const glm::vec3& boundsMin,
                             const glm::vec3& boundsMax,
                             float cellSize,
                             float maxSlopeDeg) {
    cellSize = std::max(cellSize, 0.25f);
    int cols = std::max(1, static_cast<int>(std::ceil((boundsMax.x - boundsMin.x) / cellSize)));
    int rows = std::max(1, static_cast<int>(std::ceil((boundsMax.z - boundsMin.z) / cellSize)));

    // Gather non-trigger obstacle AABBs from colliders sitting above ground
    struct ObstacleBox {
        glm::vec3 minPt;
        glm::vec3 maxPt;
    };
    std::vector<ObstacleBox> obstacles;

    for (const auto& col : scene.GetColliders()) {
        if (!col || col->isTrigger) continue;
        glm::vec3 pos = col->GetWorldPosition(scene) + col->colliderCenter;
        glm::vec3 halfExt = col->colliderSize * col->scale * 0.5f;
        // Skip flat ground floor colliders so they don't block themselves
        if (halfExt.x > 8.0f && halfExt.z > 8.0f && halfExt.y < 1.0f) continue;
        obstacles.push_back({ pos - halfExt, pos + halfExt });
    }

    auto isBlocked = [&](const glm::vec3& pt) -> bool {
        const float agentRadius = 0.25f;
        for (const auto& ob : obstacles) {
            if (pt.x >= ob.minPt.x - agentRadius && pt.x <= ob.maxPt.x + agentRadius &&
                pt.z >= ob.minPt.z - agentRadius && pt.z <= ob.maxPt.z + agentRadius &&
                ob.maxPt.y > boundsMin.y + 0.2f) {
                return true;
            }
        }
        return false;
    };

    std::vector<glm::vec3> tris;
    tris.reserve(static_cast<size_t>(cols) * rows * 6);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float x0 = boundsMin.x + c * cellSize;
            float z0 = boundsMin.z + r * cellSize;
            float x1 = std::min(x0 + cellSize, boundsMax.x);
            float z1 = std::min(z0 + cellSize, boundsMax.z);
            float y = boundsMin.y;

            glm::vec3 cellCenter((x0 + x1) * 0.5f, y, (z0 + z1) * 0.5f);
            if (isBlocked(cellCenter)) continue;

            glm::vec3 p00(x0, y, z0);
            glm::vec3 p10(x1, y, z0);
            glm::vec3 p11(x1, y, z1);
            glm::vec3 p01(x0, y, z1);

            // Triangle 1
            tris.push_back(p00);
            tris.push_back(p01);
            tris.push_back(p11);
            // Triangle 2
            tris.push_back(p00);
            tris.push_back(p11);
            tris.push_back(p10);
        }
    }

    BuildFromTriangles(tris, maxSlopeDeg);
}

void NavMesh::BuildAdjacency(float sharedVertexTolerance) {
    float tolSq = sharedVertexTolerance * sharedVertexTolerance;
    size_t n = m_polys.size();
    for (size_t i = 0; i < n; ++i) {
        m_polys[i].neighbors.clear();
    }

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            int sharedVerts = 0;
            for (int vi = 0; vi < 3; ++vi) {
                for (int vj = 0; vj < 3; ++vj) {
                    glm::vec3 d = m_polys[i].vertices[vi] - m_polys[j].vertices[vj];
                    if (glm::dot(d, d) <= tolSq) {
                        ++sharedVerts;
                        break;
                    }
                }
            }
            // Share an edge (2 vertices) or corner (1 vertex in grid connectivity)
            if (sharedVerts >= 1) {
                m_polys[i].neighbors.push_back(static_cast<uint32_t>(j));
                m_polys[j].neighbors.push_back(static_cast<uint32_t>(i));
            }
        }
    }
}

int NavMesh::FindNearestPoly(const glm::vec3& worldPos) const {
    int bestIdx = -1;
    float bestDistSq = 1e30f;

    for (size_t i = 0; i < m_polys.size(); ++i) {
        if (!m_polys[i].walkable) continue;
        glm::vec3 d = m_polys[i].center - worldPos;
        float distSq = glm::dot(d, d);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

std::vector<glm::vec3> NavMesh::FindPath(const glm::vec3& startPos, const glm::vec3& endPos) const {
    std::vector<glm::vec3> result;
    if (m_polys.empty()) return result;

    int startPoly = FindNearestPoly(startPos);
    int endPoly = FindNearestPoly(endPos);
    if (startPoly < 0 || endPoly < 0) return result;

    if (startPoly == endPoly) {
        result.push_back(startPos);
        result.push_back(endPos);
        return result;
    }

    struct QueueEntry {
        uint32_t polyId;
        float fScore;
        bool operator>(const QueueEntry& other) const { return fScore > other.fScore; }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> openSet;
    std::map<uint32_t, float> gScore;
    std::map<uint32_t, uint32_t> cameFrom;
    std::set<uint32_t> closedSet;

    uint32_t startId = static_cast<uint32_t>(startPoly);
    uint32_t goalId = static_cast<uint32_t>(endPoly);

    gScore[startId] = 0.0f;
    float hStart = glm::length(m_polys[goalId].center - m_polys[startId].center);
    openSet.push({ startId, hStart });

    bool found = false;
    while (!openSet.empty()) {
        uint32_t current = openSet.top().polyId;
        openSet.pop();

        if (current == goalId) {
            found = true;
            break;
        }
        if (closedSet.count(current)) continue;
        closedSet.insert(current);

        for (uint32_t neighbor : m_polys[current].neighbors) {
            if (!m_polys[neighbor].walkable || closedSet.count(neighbor)) continue;

            float stepCost = glm::length(m_polys[neighbor].center - m_polys[current].center);
            float tentativeG = gScore[current] + stepCost;

            auto it = gScore.find(neighbor);
            if (it == gScore.end() || tentativeG < it->second) {
                cameFrom[neighbor] = current;
                gScore[neighbor] = tentativeG;
                float h = glm::length(m_polys[goalId].center - m_polys[neighbor].center);
                openSet.push({ neighbor, tentativeG + h });
            }
        }
    }

    if (!found) return result;

    std::vector<glm::vec3> rawWaypoints;
    rawWaypoints.push_back(endPos);
    uint32_t curr = goalId;
    while (curr != startId) {
        rawWaypoints.push_back(m_polys[curr].center);
        curr = cameFrom[curr];
    }
    rawWaypoints.push_back(startPos);
    std::reverse(rawWaypoints.begin(), rawWaypoints.end());

    // Collinear waypoint smoothing
    result.push_back(rawWaypoints.front());
    for (size_t i = 1; i + 1 < rawWaypoints.size(); ++i) {
        glm::vec3 d1 = rawWaypoints[i] - result.back();
        glm::vec3 d2 = rawWaypoints[i + 1] - rawWaypoints[i];
        if (glm::length(d1) > 1e-4f && glm::length(d2) > 1e-4f) {
            float cosAng = glm::dot(glm::normalize(d1), glm::normalize(d2));
            if (cosAng > 0.995f) continue; // Skip redundant collinear waypoint
        }
        result.push_back(rawWaypoints[i]);
    }
    result.push_back(rawWaypoints.back());
    return result;
}

size_t NavMesh::GetWalkablePolyCount() const {
    size_t count = 0;
    for (const auto& p : m_polys) {
        if (p.walkable) ++count;
    }
    return count;
}

} // namespace Navigation

