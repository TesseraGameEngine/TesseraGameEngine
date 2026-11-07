#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <vector>
#include <limits>
#include "Scene/Scene.h"
#include "Scene/Nodes/3D/TerrainNode.h"

namespace Physics {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

struct RayHit {
    bool hit{false};
    float distance{0.0f};
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    uint32_t nodeId{0};
};

struct AABB {
    glm::vec3 minBound{-0.5f};
    glm::vec3 maxBound{0.5f};

    glm::vec3 GetCenter() const { return (minBound + maxBound) * 0.5f; }
    glm::vec3 GetExtents() const { return (maxBound - minBound) * 0.5f; }
};

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius{0.5f};
};

inline bool CheckAABBOverlap(const AABB& a, const AABB& b) {
    if (a.maxBound.x < b.minBound.x || a.minBound.x > b.maxBound.x) return false;
    if (a.maxBound.y < b.minBound.y || a.minBound.y > b.maxBound.y) return false;
    if (a.maxBound.z < b.minBound.z || a.minBound.z > b.maxBound.z) return false;
    return true;
}

inline bool CheckSphereSphereOverlap(const BoundingSphere& a, const BoundingSphere& b) {
    float distSq = glm::dot(a.center - b.center, a.center - b.center);
    float radSum = a.radius + b.radius;
    return distSq <= (radSum * radSum);
}

inline bool CheckSphereAABBOverlap(const BoundingSphere& s, const AABB& b) {
    // Find closest point on AABB to sphere center
    glm::vec3 closest = glm::clamp(s.center, b.minBound, b.maxBound);
    float distSq = glm::dot(s.center - closest, s.center - closest);
    return distSq <= (s.radius * s.radius);
}

inline bool RayIntersectsAABB(const Ray& ray, const AABB& box, float& outDistance) {
    glm::vec3 invD = 1.0f / (ray.direction + glm::vec3(1e-8f));
    glm::vec3 t0 = (box.minBound - ray.origin) * invD;
    glm::vec3 t1 = (box.maxBound - ray.origin) * invD;

    glm::vec3 tSmall = glm::min(t0, t1);
    glm::vec3 tBig = glm::max(t0, t1);

    float tNear = std::max(std::max(tSmall.x, tSmall.y), tSmall.z);
    float tFar = std::min(std::min(tBig.x, tBig.y), tBig.z);

    if (tNear <= tFar && tFar >= 0.0f) {
        outDistance = (tNear >= 0.0f) ? tNear : tFar;
        return true;
    }
    return false;
}

inline bool RayIntersectsSphere(const Ray& ray, const BoundingSphere& sphere, float& outDistance) {
    glm::vec3 m = ray.origin - sphere.center;
    float b = glm::dot(m, ray.direction);
    float c = glm::dot(m, m) - sphere.radius * sphere.radius;

    if (c > 0.0f && b > 0.0f) return false;

    float discr = b * b - c;
    if (discr < 0.0f) return false;

    outDistance = -b - std::sqrt(discr);
    if (outDistance < 0.0f) outDistance = 0.0f;
    return true;
}

inline AABB GetNodeAABB(const Scene& scene, const Node& node) {
    glm::vec3 center = node.GetWorldPosition(scene);
    glm::vec3 halfSize = node.scale * 0.5f;

    if (node.nodeType == NodeType::Collider) {
        if (node.colliderType == ColliderType::Box) {
            halfSize = (node.scale * node.colliderSize) * 0.5f;
        } else if (node.colliderType == ColliderType::Sphere) {
            float r = node.colliderRadius * std::max(std::max(node.scale.x, node.scale.y), node.scale.z);
            halfSize = glm::vec3(r);
        }
        center += node.colliderCenter;
    } else if (node.type == ShapeType::Plane) {
        // A 3D Plane is flat on the Y=0 plane in its local space.
        // Its surface in world space is exactly at center.y.
        // We set the AABB top boundary at center.y so raycasts and physical contacts
        // stop directly on the plane's visible surface instead of a thick box.
        return AABB{
            glm::vec3(center.x - halfSize.x, center.y - 0.02f, center.z - halfSize.z),
            glm::vec3(center.x + halfSize.x, center.y, center.z + halfSize.z)
        };
    }

    return AABB{ center - halfSize, center + halfSize };
}

inline RayHit Raycast(const Scene& scene, const Ray& ray, float maxDistance = 1000.0f) {
    RayHit bestHit{};
    bestHit.distance = maxDistance;

    for (const auto& node : scene.GetNodes()) {
        if (node->nodeType == NodeType::Camera || node->nodeType == NodeType::Light) continue;

        AABB box = GetNodeAABB(scene, *node);
        float dist = 0.0f;
        if (RayIntersectsAABB(ray, box, dist) && dist < bestHit.distance && dist >= 0.0f) {
            bestHit.hit = true;
            bestHit.distance = dist;
            bestHit.point = ray.origin + ray.direction * dist;
            bestHit.nodeId = node->id;
            glm::vec3 center = box.GetCenter();
            bestHit.normal = glm::normalize(bestHit.point - center);
        }
    }
    return bestHit;
}

inline RayHit RaycastDown(const Scene& scene, const glm::vec3& origin, float maxDistance = 100.0f, uint32_t ignoreNodeId = 0, float* outGroundY = nullptr) {
    Ray ray;
    ray.origin = origin;
    ray.direction = glm::vec3(0.0f, -1.0f, 0.0f);

    RayHit bestHit{};
    bestHit.distance = maxDistance;

    for (const auto& node : scene.GetNodes()) {
        if (node->id == ignoreNodeId) continue;
        if (node->nodeType == NodeType::Camera || node->nodeType == NodeType::Light || node->IsUINode()) continue;

        if (node->nodeType == NodeType::Terrain) {
            auto terr = std::dynamic_pointer_cast<const TerrainNode>(node);
            if (terr) {
                float terrY = terr->SampleHeight(origin.x, origin.z, scene);
                float dist = origin.y - terrY;
                if (dist >= 0.0f && dist < bestHit.distance) {
                    bestHit.hit = true;
                    bestHit.distance = dist;
                    bestHit.point = glm::vec3(origin.x, terrY, origin.z);
                    bestHit.nodeId = node->id;
                    bestHit.normal = terr->SampleNormal(origin.x, origin.z, scene);
                }
            }
            continue;
        }

        AABB box = GetNodeAABB(scene, *node);
        float dist = 0.0f;
        if (RayIntersectsAABB(ray, box, dist) && dist < bestHit.distance && dist >= 0.0f) {
            bestHit.hit = true;
            bestHit.distance = dist;
            bestHit.point = ray.origin + ray.direction * dist;
            bestHit.nodeId = node->id;
            bestHit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    if (bestHit.hit && outGroundY) {
        *outGroundY = bestHit.point.y;
    }
    return bestHit;
}

inline float GetNodeHalfHeight(const Node& node) {
    if (node.type == ShapeType::Custom && node.boundsMin.y < -0.001f) {
        return -node.boundsMin.y * node.scale.y;
    }
    return node.scale.y * 0.5f;
}

inline void ApplyCharacterGravityAndClamping(const Scene& scene, Node& node, float dt, float gravity = 9.81f, float heightOffset = 0.0f, bool fallbackToZeroGround = false) {
    float halfHeight = GetNodeHalfHeight(node);
    float effectiveOffset = heightOffset > 0.0001f ? heightOffset : halfHeight;
    float rayOriginY = node.position.y + halfHeight + 0.5f;
    glm::vec3 origin(node.position.x, rayOriginY, node.position.z);

    float groundY = -999999.0f;
    float hitY = 0.0f;
    RayHit hit = RaycastDown(scene, origin, 1000.0f, node.id, &hitY);
    if (hit.hit) {
        groundY = hitY;
    } else if (fallbackToZeroGround) {
        groundY = 0.0f;
    }

    if (groundY > -900000.0f) {
        float targetY = groundY + effectiveOffset;
        if (node.position.y <= targetY) {
            // Solid contact clamping - stops exactly on the surface, zero penetration
            node.position.y = targetY;
        } else {
            float nextY = node.position.y - gravity * dt;
            node.position.y = (std::max)(nextY, targetY);
        }
    } else {
        node.position.y -= gravity * dt;
    }
}

inline std::vector<uint32_t> QueryCollidersOverlap(const Scene& scene, uint32_t queryNodeId) {
    std::vector<uint32_t> overlapping;
    auto target = scene.GetNodeById(queryNodeId);
    if (!target) return overlapping;

    AABB targetBox = GetNodeAABB(scene, *target);

    for (const auto& node : scene.GetNodes()) {
        if (node->id == queryNodeId) continue;
        if (node->nodeType != NodeType::Collider && node->nodeType != NodeType::Mesh) continue;

        AABB otherBox = GetNodeAABB(scene, *node);
        if (CheckAABBOverlap(targetBox, otherBox)) {
            overlapping.push_back(node->id);
        }
    }
    return overlapping;
}

} // namespace Physics

