#pragma once

#include <memory>
#include <vector>
#include <map>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Scene/Scene.h"
#include "Animation/Skeleton.h"

namespace Physics {

enum class JointType {
    Fixed = 0,
    Hinge = 1,
    Distance = 2,
    SwingTwist = 3
};

struct RagdollInstance {
    uint32_t id{0};
    std::vector<uint32_t> joltBodyIds; // Raw JPH::BodyID values per bone
    std::vector<int> boneIndices;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    void SetGravity(const glm::vec3& gravity);
    glm::vec3 GetGravity() const;

    // 1. Rigid Body Dynamics (JoltPhysics)
    void BuildFromScene(Scene& scene);
    uint32_t CreateBodyForNode(Scene& scene, const std::shared_ptr<Node>& node);
    bool HasBodyForNode(uint32_t nodeId) const;
    // Streaming: bodies for nodes that joined a running scene (rigid bodies, colliders, and
    // terrains with their solid foliage), and removal of everything a node owns
    void AddBodiesForNodes(Scene& scene, const std::vector<std::shared_ptr<Node>>& nodes);
    void RemoveBodiesForNode(uint32_t nodeId);
    // Number of bodies a node owns (its own body plus terrain / foliage extras)
    size_t GetBodyCountForNode(uint32_t nodeId) const;

    void SetLinearVelocity(uint32_t nodeId, const glm::vec3& velocity);
    glm::vec3 GetLinearVelocity(uint32_t nodeId) const;

    void SetAngularVelocity(uint32_t nodeId, const glm::vec3& angularVel);
    glm::vec3 GetAngularVelocity(uint32_t nodeId) const;

    void AddForce(uint32_t nodeId, const glm::vec3& force);
    // Floats a body in water whose surface near it is the plane (surfacePoint, surfaceNormal),
    // moving at fluidVelocity. Call before Step(dt). False when the body isn't in the water.
    bool ApplyBuoyancy(uint32_t nodeId, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal,
                       float buoyancy, const glm::vec3& fluidVelocity, float dt);
    void AddImpulse(uint32_t nodeId, const glm::vec3& impulse);
    // 1b. Precision Collision Layer & Mask Bitfields
    void SetCollisionLayer(uint32_t nodeId, uint32_t layer);
    uint32_t GetCollisionLayer(uint32_t nodeId) const;
    void SetCollisionMask(uint32_t nodeId, uint32_t mask);
    uint32_t GetCollisionMask(uint32_t nodeId) const;
    bool CanCollide(uint32_t nodeIdA, uint32_t nodeIdB) const;

    void SetPosition(uint32_t nodeId, const glm::vec3& position);
    glm::vec3 GetPosition(uint32_t nodeId) const;
    void SetRotation(uint32_t nodeId, const glm::vec3& eulerDeg);
    glm::vec3 GetRotation(uint32_t nodeId) const;

    // Advances JoltPhysics simulation and writes updated transforms back to Scene nodes
    void Step(Scene& scene, float dt, int collisionSteps = 1);
    // Terrains as solid ground (a heightfield each) and their solid foliage (trees, rocks) as
    // static bodies. Call after BuildFromScene; they go with the scene's other bodies.
    void AddTerrainBodies(Scene& scene);
    // One terrain's heightfield and solid foliage (owned by that node: RemoveBodiesForNode)
    void AddTerrainBodiesForNode(Scene& scene, const std::shared_ptr<Node>& node);
    // Pairs of nodes whose bodies started touching since the last call (scripts' OnCollision)
    std::vector<std::pair<uint32_t, uint32_t>> TakeNewContacts();

    // 2. Joints & Constraints
    bool AddFixedJoint(uint32_t nodeIdA, uint32_t nodeIdB);
    bool AddHingeJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                       const glm::vec3& worldAnchor,
                       const glm::vec3& worldHingeAxis = glm::vec3(0.0f, 1.0f, 0.0f),
                       float minAngleDeg = -90.0f,
                       float maxAngleDeg = 90.0f);
    bool AddDistanceJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                          float minDistance = 0.5f,
                          float maxDistance = 3.0f);
    bool AddSwingTwistJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                            const glm::vec3& worldAnchor,
                            const glm::vec3& twistAxis = glm::vec3(0.0f, 1.0f, 0.0f),
                            float normalHalfConeDeg = 40.0f,
                            float planeHalfConeDeg = 40.0f,
                            float twistMinDeg = -30.0f,
                            float twistMaxDeg = 30.0f);

    uint32_t GetConstraintCount() const { return m_constraintCount; }

    // 3. Ragdoll Physics (Articulated Jolt bodies + SwingTwist constraints synced to Skeleton)
    uint32_t CreateHumanoidRagdoll(const Animation::Skeleton& skeleton,
                                   const glm::vec3& rootWorldOrigin = glm::vec3(0.0f, 5.0f, 0.0f));
    bool SyncRagdollToSkeleton(uint32_t ragdollId, Animation::Skeleton& skeleton) const;
    void ApplyRagdollImpulse(uint32_t ragdollId, const glm::vec3& impulse);
    glm::vec3 GetRagdollRootPosition(uint32_t ragdollId) const;

public:
    struct Impl;
private:
    std::unique_ptr<Impl> m_impl;
    bool m_initialized{false};
    uint32_t m_constraintCount{0};
    uint32_t m_nextRagdollId{1};
};

} // namespace Physics

