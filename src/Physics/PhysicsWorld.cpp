#include "PhysicsWorld.h"
#include "Animation/IKSolver.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include "Scene/Nodes/3D/TerrainNode.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <glm/gtc/matrix_transform.hpp>
#include <thread>
#include <mutex>
#include <algorithm>

JPH_SUPPRESS_WARNINGS

namespace Physics {

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                return false;
        }
    }
};


struct CollisionFilter {
    uint32_t layer{1};
    uint32_t mask{1};
};

class TesseraContactListener : public JPH::ContactListener {
public:
    PhysicsWorld::Impl* impl{nullptr};

    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                          JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override;
    // Called from the physics threads when two bodies start touching
    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                return false;
        }
    }
};

namespace {
std::once_flag g_joltInitFlag;

void EnsureJoltGlobalInitialized() {
    std::call_once(g_joltInitFlag, []() {
        JPH::RegisterDefaultAllocator();
        if (!JPH::Factory::sInstance) {
            JPH::Factory::sInstance = new JPH::Factory();
        }
        JPH::RegisterTypes();
    });
}

JPH::RVec3 ToJoltRVec3(const glm::vec3& v) {
    return JPH::RVec3(v.x, v.y, v.z);
}

JPH::Vec3 ToJoltVec3(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

glm::vec3 ToGlmVec3(const JPH::RVec3& v) {
    return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()));
}

glm::vec3 ToGlmVec3f(const JPH::Vec3& v) {
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

JPH::Quat EulerDegreesToJoltQuat(const glm::vec3& eulerDeg) {
    glm::quat q = glm::quat(glm::radians(eulerDeg));
    return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
}

glm::vec3 JoltQuatToEulerDegrees(const JPH::Quat& jq) {
    glm::quat q(jq.GetW(), jq.GetX(), jq.GetY(), jq.GetZ());
    return glm::degrees(glm::eulerAngles(q));
}
} // namespace

struct PhysicsWorld::Impl {
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

    std::map<uint32_t, JPH::BodyID> nodeToBody;
    std::map<uint32_t, RagdollInstance> ragdolls;
    std::vector<JPH::Ref<JPH::Constraint>> constraints;
    std::map<uint32_t, CollisionFilter> nodeCollisionFilters;
    TesseraContactListener contactListener;
    std::mutex contactMutex;
    std::vector<JPH::BodyID> staticExtras; // terrain heightfields and solid foliage (AddTerrainBodies)
    std::vector<std::pair<uint32_t, uint32_t>> newContacts; // node ids, since TakeNewContacts
};

void TesseraContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                            const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) {
    (void)inManifold;
    (void)ioSettings;
    const uint32_t id1 = static_cast<uint32_t>(inBody1.GetUserData());
    const uint32_t id2 = static_cast<uint32_t>(inBody2.GetUserData());
    if (!impl || id1 == 0 || id2 == 0) return;
    std::lock_guard<std::mutex> lock(impl->contactMutex);
    if (impl->newContacts.size() < 4096) impl->newContacts.emplace_back(id1, id2);
}

std::vector<std::pair<uint32_t, uint32_t>> PhysicsWorld::TakeNewContacts() {
    std::lock_guard<std::mutex> lock(m_impl->contactMutex);
    std::vector<std::pair<uint32_t, uint32_t>> contacts;
    contacts.swap(m_impl->newContacts);
    return contacts;
}


JPH::ValidateResult TesseraContactListener::OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                                              JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) {
    uint64_t u1 = inBody1.GetUserData();
    uint64_t u2 = inBody2.GetUserData();
    uint32_t id1 = static_cast<uint32_t>(u1);
    uint32_t id2 = static_cast<uint32_t>(u2);

    if (impl && id1 != 0 && id2 != 0) {
        auto it1 = impl->nodeCollisionFilters.find(id1);
        auto it2 = impl->nodeCollisionFilters.find(id2);
        if (it1 != impl->nodeCollisionFilters.end() && it2 != impl->nodeCollisionFilters.end()) {
            uint32_t layer1 = it1->second.layer;
            uint32_t mask1 = it1->second.mask;
            uint32_t layer2 = it2->second.layer;
            uint32_t mask2 = it2->second.mask;

            bool canCollide = ((layer1 & mask2) != 0) || ((layer2 & mask1) != 0);
            if (!canCollide) {
                return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
            }
        }
    }
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

PhysicsWorld::PhysicsWorld() : m_impl(std::make_unique<Impl>()) {
    Initialize();
}

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

bool PhysicsWorld::Initialize() {
    if (m_initialized) return true;

    EnsureJoltGlobalInitialized();

    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    uint32_t numThreads = std::max(1u, std::min(4u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u));
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

    m_impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    const JPH::uint cMaxBodies = 16384; // terrain foliage can add thousands of static bodies
    const JPH::uint cNumBodyMutexes = 0;
    const JPH::uint cMaxBodyPairs = 4096;
    const JPH::uint cMaxContactConstraints = 2048;

    m_impl->physicsSystem->Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        m_impl->broadPhaseLayerInterface,
        m_impl->objectVsBroadPhaseLayerFilter,
        m_impl->objectLayerPairFilter
    );

    m_impl->contactListener.impl = m_impl.get();
    m_impl->physicsSystem->SetContactListener(&m_impl->contactListener);
    m_impl->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    m_initialized = true;
    return true;
}

void PhysicsWorld::Shutdown() {
    if (!m_initialized) return;

    auto& bodyInterface = m_impl->physicsSystem->GetBodyInterface();

    for (auto& c : m_impl->constraints) {
        m_impl->physicsSystem->RemoveConstraint(c);
    }
    m_impl->constraints.clear();
    m_constraintCount = 0;

    for (const auto& kv : m_impl->nodeToBody) {
        if (!kv.second.IsInvalid()) {
            bodyInterface.RemoveBody(kv.second);
            bodyInterface.DestroyBody(kv.second);
        }
    }
    m_impl->nodeToBody.clear();
    for (const JPH::BodyID& id : m_impl->staticExtras) {
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }
    m_impl->staticExtras.clear();

    for (const auto& kv : m_impl->ragdolls) {
        for (uint32_t rawId : kv.second.joltBodyIds) {
            JPH::BodyID bid(rawId);
            if (!bid.IsInvalid()) {
                bodyInterface.RemoveBody(bid);
                bodyInterface.DestroyBody(bid);
            }
        }
    }
    m_impl->ragdolls.clear();

    m_impl->physicsSystem.reset();
    m_impl->jobSystem.reset();
    m_impl->tempAllocator.reset();
    m_initialized = false;
}

void PhysicsWorld::SetGravity(const glm::vec3& gravity) {
    if (!m_initialized) return;
    m_impl->physicsSystem->SetGravity(ToJoltVec3(gravity));
}

glm::vec3 PhysicsWorld::GetGravity() const {
    if (!m_initialized) return glm::vec3(0.0f, -9.81f, 0.0f);
    return ToGlmVec3f(m_impl->physicsSystem->GetGravity());
}

void PhysicsWorld::BuildFromScene(Scene& scene) {
    if (!m_initialized) Initialize();

    auto& bodyInterface = m_impl->physicsSystem->GetBodyInterface();
    for (auto& c : m_impl->constraints) {
        m_impl->physicsSystem->RemoveConstraint(c);
    }
    m_impl->constraints.clear();
    m_constraintCount = 0;

    for (const auto& kv : m_impl->nodeToBody) {
        if (!kv.second.IsInvalid()) {
            bodyInterface.RemoveBody(kv.second);
            bodyInterface.DestroyBody(kv.second);
        }
    }
    m_impl->nodeToBody.clear();
    for (const JPH::BodyID& id : m_impl->staticExtras) {
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }
    m_impl->staticExtras.clear();

    for (const auto& node : scene.GetNodes()) {
        if (!node) continue;
        if (node->nodeType == NodeType::Terrain) continue; // AddTerrainBodies
        if (node->rigidBodyType != RigidBodyType::None || node->nodeType == NodeType::Collider) {
            CreateBodyForNode(scene, node);
        }
    }
    m_impl->physicsSystem->OptimizeBroadPhase();
}

uint32_t PhysicsWorld::CreateBodyForNode(Scene& scene, const std::shared_ptr<Node>& node) {
    if (!m_initialized || !node) return 0;

    auto& bodyInterface = m_impl->physicsSystem->GetBodyInterface();
    auto existingIt = m_impl->nodeToBody.find(node->id);
    if (existingIt != m_impl->nodeToBody.end() && !existingIt->second.IsInvalid()) {
        bodyInterface.RemoveBody(existingIt->second);
        bodyInterface.DestroyBody(existingIt->second);
    }

    RigidBodyType rbType = node->rigidBodyType;
    if (rbType == RigidBodyType::None && node->nodeType == NodeType::Collider) {
        rbType = RigidBodyType::Static;
    }
    if (rbType == RigidBodyType::None) return 0;

    glm::vec3 halfExtents = glm::max(node->scale * 0.5f, glm::vec3(0.05f));
    if (node->nodeType == NodeType::Collider) {
        halfExtents = glm::max(node->colliderSize * node->scale * 0.5f, glm::vec3(0.05f));
    }

    JPH::RefConst<JPH::Shape> shape;
    if (node->nodeType == NodeType::Collider && node->colliderType == ColliderType::Sphere) {
        float r = std::max(node->colliderRadius * std::max({node->scale.x, node->scale.y, node->scale.z}), 0.05f);
        shape = new JPH::SphereShape(r);
    } else if (node->nodeType == NodeType::Collider && node->colliderType == ColliderType::Capsule) {
        float r = std::max(node->colliderRadius * std::max(node->scale.x, node->scale.z), 0.05f);
        float halfH = std::max((node->colliderHeight * node->scale.y) * 0.5f, 0.05f);
        shape = new JPH::CapsuleShape(halfH, r);
    } else if (node->type == ShapeType::Sphere && node->nodeType == NodeType::Mesh) {
        float r = std::max({halfExtents.x, halfExtents.y, halfExtents.z, 0.05f});
        shape = new JPH::SphereShape(r);
    } else {
        shape = new JPH::BoxShape(ToJoltVec3(halfExtents));
    }

    JPH::EMotionType motionType = JPH::EMotionType::Static;
    JPH::ObjectLayer layer = Layers::NON_MOVING;
    if (rbType == RigidBodyType::Dynamic) {
        motionType = JPH::EMotionType::Dynamic;
        layer = Layers::MOVING;
    } else if (rbType == RigidBodyType::Kinematic) {
        motionType = JPH::EMotionType::Kinematic;
        layer = Layers::MOVING;
    }

    glm::vec3 worldPos = node->GetWorldPosition(scene);
    JPH::Quat rot = EulerDegreesToJoltQuat(node->rotation);

    JPH::BodyCreationSettings settings(shape, ToJoltRVec3(worldPos), rot, motionType, layer);
    settings.mUserData = static_cast<uint64_t>(node->id);
    m_impl->nodeCollisionFilters[node->id] = { node->collisionLayer, node->collisionMask };
    settings.mLinearDamping = node->linearDamping;
    settings.mAngularDamping = node->angularDamping;
    settings.mGravityFactor = node->useGravity ? 1.0f : 0.0f;
    settings.mRestitution = node->bounciness;
    settings.mFriction = node->friction;
    if (rbType == RigidBodyType::Dynamic && node->mass > 0.0f) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = node->mass;
    }

    JPH::EActivation activation = (rbType == RigidBodyType::Static)
        ? JPH::EActivation::DontActivate
        : JPH::EActivation::Activate;
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, activation);
    if (bodyId.IsInvalid()) return 0;

    if (rbType != RigidBodyType::Static) {
        if (glm::length(node->linearVelocity) > 1e-5f) {
            bodyInterface.SetLinearVelocity(bodyId, ToJoltVec3(node->linearVelocity));
        }
        if (glm::length(node->angularVelocity) > 1e-5f) {
            bodyInterface.SetAngularVelocity(bodyId, ToJoltVec3(node->angularVelocity));
        }
    }

    m_impl->nodeToBody[node->id] = bodyId;
    return bodyId.GetIndexAndSequenceNumber();
}

void PhysicsWorld::AddTerrainBodies(Scene& scene) {
    if (!m_initialized) return;
    auto& bodyInterface = m_impl->physicsSystem->GetBodyInterface();
    constexpr size_t kMaxFoliageBodies = 12000;
    size_t foliageBodies = 0;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::Terrain) continue;
        auto terrain = std::dynamic_pointer_cast<TerrainNode>(node);
        if (!terrain) continue;
        const uint32_t N = terrain->gridResolution;
        if (N < 2 || terrain->heightData.size() != static_cast<size_t>(N) * N) continue;
        // The ground: a heightfield of the terrain's samples (Jolt wants a multiple of its block size)
        uint32_t samples = N;
        if (samples % 2 != 0) ++samples;
        std::vector<float> heights(static_cast<size_t>(samples) * samples);
        for (uint32_t z = 0; z < samples; ++z) {
            for (uint32_t x = 0; x < samples; ++x) {
                const uint32_t sx = std::min(x, N - 1), sz = std::min(z, N - 1);
                heights[static_cast<size_t>(z) * samples + x] = terrain->heightData[static_cast<size_t>(sz) * N + sx] * terrain->heightScale;
            }
        }
        const glm::vec3 nodeScale = glm::max(glm::abs(terrain->scale), glm::vec3(1e-4f));
        const glm::vec3 step(terrain->terrainSize.x / (N - 1), 1.0f, terrain->terrainSize.y / (N - 1));
        JPH::HeightFieldShapeSettings settings(heights.data(),
                                               JPH::Vec3(-terrain->terrainSize.x * 0.5f * nodeScale.x, 0.0f, -terrain->terrainSize.y * 0.5f * nodeScale.z),
                                               JPH::Vec3(step.x * nodeScale.x, nodeScale.y, step.z * nodeScale.z), samples);
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.IsValid()) {
            JPH::BodyCreationSettings body(result.Get(), ToJoltRVec3(terrain->GetWorldPosition(scene)), EulerDegreesToJoltQuat(terrain->rotation),
                                           JPH::EMotionType::Static, Layers::NON_MOVING);
            body.mUserData = static_cast<uint64_t>(terrain->id);
            body.mFriction = 0.8f;
            const JPH::BodyID id = bodyInterface.CreateAndAddBody(body, JPH::EActivation::DontActivate);
            if (!id.IsInvalid()) m_impl->staticExtras.push_back(id);
        }
        // Solid foliage: trunks as capsules, rocks and bushes as balls
        for (const auto& [type, model] : terrain->CollidingFoliage(scene)) {
            if (foliageBodies >= kMaxFoliageBodies) break;
            const int builtIn = terrain->foliageTypes[type].builtIn;
            const float s = glm::length(glm::vec3(model[1]));
            const glm::vec3 base(model[3]);
            JPH::RefConst<JPH::Shape> shape;
            glm::vec3 center = base;
            if (builtIn == 3 || builtIn == 4) {
                const float radius = 0.22f * s, halfHeight = 1.6f * s;
                shape = new JPH::CapsuleShape(halfHeight, radius);
                center = base + glm::vec3(0.0f, halfHeight + radius, 0.0f);
            } else {
                const float radius = (builtIn == 5 ? 0.42f : 0.5f) * s;
                shape = new JPH::SphereShape(radius);
                center = base + glm::vec3(0.0f, radius * 0.6f, 0.0f);
            }
            JPH::BodyCreationSettings body(shape, ToJoltRVec3(center), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
            body.mUserData = static_cast<uint64_t>(terrain->id);
            const JPH::BodyID id = bodyInterface.CreateAndAddBody(body, JPH::EActivation::DontActivate);
            if (!id.IsInvalid()) {
                m_impl->staticExtras.push_back(id);
                ++foliageBodies;
            }
        }
    }
    m_impl->physicsSystem->OptimizeBroadPhase();
}

bool PhysicsWorld::HasBodyForNode(uint32_t nodeId) const {
    auto it = m_impl->nodeToBody.find(nodeId);
    return it != m_impl->nodeToBody.end() && !it->second.IsInvalid();
}

void PhysicsWorld::SetLinearVelocity(uint32_t nodeId, const glm::vec3& velocity) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.ActivateBody(it->second);
    bi.SetLinearVelocity(it->second, ToJoltVec3(velocity));
}

glm::vec3 PhysicsWorld::GetLinearVelocity(uint32_t nodeId) const {
    if (!m_initialized) return glm::vec3(0.0f);
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return glm::vec3(0.0f);
    return ToGlmVec3f(m_impl->physicsSystem->GetBodyInterface().GetLinearVelocity(it->second));
}

void PhysicsWorld::SetAngularVelocity(uint32_t nodeId, const glm::vec3& angularVel) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.ActivateBody(it->second);
    bi.SetAngularVelocity(it->second, ToJoltVec3(angularVel));
}

glm::vec3 PhysicsWorld::GetAngularVelocity(uint32_t nodeId) const {
    if (!m_initialized) return glm::vec3(0.0f);
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return glm::vec3(0.0f);
    return ToGlmVec3f(m_impl->physicsSystem->GetBodyInterface().GetAngularVelocity(it->second));
}

bool PhysicsWorld::ApplyBuoyancy(uint32_t nodeId, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal,
                                 float buoyancy, const glm::vec3& fluidVelocity, float dt) {
    if (!m_initialized || dt <= 0.0f) return false;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return false;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    // A body resting on the bottom has gone to sleep; the water still has to lift it
    bi.ActivateBody(it->second);
    // Linear drag 0.5 and angular drag 0.05 settle bobbing without looking like syrup
    return bi.ApplyBuoyancyImpulse(it->second, ToJoltRVec3(surfacePoint), ToJoltVec3(glm::normalize(surfaceNormal)),
                                   buoyancy, 0.5f, 0.05f, ToJoltVec3(fluidVelocity),
                                   m_impl->physicsSystem->GetGravity(), dt);
}

void PhysicsWorld::AddForce(uint32_t nodeId, const glm::vec3& force) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.ActivateBody(it->second);
    bi.AddForce(it->second, ToJoltVec3(force));
}

void PhysicsWorld::AddImpulse(uint32_t nodeId, const glm::vec3& impulse) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.ActivateBody(it->second);
    bi.AddImpulse(it->second, ToJoltVec3(impulse));
}

void PhysicsWorld::SetPosition(uint32_t nodeId, const glm::vec3& position) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.SetPosition(it->second, ToJoltRVec3(position), JPH::EActivation::Activate);
}

glm::vec3 PhysicsWorld::GetPosition(uint32_t nodeId) const {
    if (!m_initialized) return glm::vec3(0.0f);
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return glm::vec3(0.0f);
    return ToGlmVec3(m_impl->physicsSystem->GetBodyInterface().GetPosition(it->second));
}

void PhysicsWorld::SetRotation(uint32_t nodeId, const glm::vec3& eulerDeg) {
    if (!m_initialized) return;
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    bi.SetRotation(it->second, EulerDegreesToJoltQuat(eulerDeg), JPH::EActivation::Activate);
}

glm::vec3 PhysicsWorld::GetRotation(uint32_t nodeId) const {
    if (!m_initialized) return glm::vec3(0.0f);
    auto it = m_impl->nodeToBody.find(nodeId);
    if (it == m_impl->nodeToBody.end() || it->second.IsInvalid()) return glm::vec3(0.0f);
    return JoltQuatToEulerDegrees(m_impl->physicsSystem->GetBodyInterface().GetRotation(it->second));
}

void PhysicsWorld::Step(Scene& scene, float dt, int collisionSteps) {
    if (!m_initialized || dt <= 0.0f) return;

    m_impl->physicsSystem->Update(dt, std::max(1, collisionSteps), m_impl->tempAllocator.get(), m_impl->jobSystem.get());

    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    for (const auto& kv : m_impl->nodeToBody) {
        uint32_t nodeId = kv.first;
        JPH::BodyID bid = kv.second;
        if (bid.IsInvalid()) continue;

        auto node = scene.GetNodeById(nodeId);
        if (!node || node->rigidBodyType == RigidBodyType::Static || node->rigidBodyType == RigidBodyType::None) continue;

        JPH::RVec3 pos;
        JPH::Quat rot;
        bi.GetPositionAndRotation(bid, pos, rot);

        node->position = ToGlmVec3(pos);
        node->rotation = JoltQuatToEulerDegrees(rot);
        node->linearVelocity = ToGlmVec3f(bi.GetLinearVelocity(bid));
        node->angularVelocity = ToGlmVec3f(bi.GetAngularVelocity(bid));
    }
}

bool PhysicsWorld::AddFixedJoint(uint32_t nodeIdA, uint32_t nodeIdB) {
    if (!m_initialized) return false;
    auto itA = m_impl->nodeToBody.find(nodeIdA);
    auto itB = m_impl->nodeToBody.find(nodeIdB);
    if (itA == m_impl->nodeToBody.end() || itB == m_impl->nodeToBody.end()) return false;

    JPH::FixedConstraintSettings settings;
    settings.mAutoDetectPoint = true;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    JPH::Ref<JPH::Constraint> c = bi.CreateConstraint(&settings, itA->second, itB->second);
    if (!c) return false;

    m_impl->physicsSystem->AddConstraint(c);
    m_impl->constraints.push_back(c);
    ++m_constraintCount;
    return true;
}

bool PhysicsWorld::AddHingeJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                                 const glm::vec3& worldAnchor,
                                 const glm::vec3& worldHingeAxis,
                                 float minAngleDeg,
                                 float maxAngleDeg) {
    if (!m_initialized) return false;
    auto itA = m_impl->nodeToBody.find(nodeIdA);
    auto itB = m_impl->nodeToBody.find(nodeIdB);
    if (itA == m_impl->nodeToBody.end() || itB == m_impl->nodeToBody.end()) return false;

    glm::vec3 axis = glm::normalize(worldHingeAxis);
    glm::vec3 normAxis = (std::abs(axis.x) < 0.9f)
        ? glm::normalize(glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f)))
        : glm::normalize(glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f)));

    JPH::HingeConstraintSettings settings;
    settings.mPoint1 = settings.mPoint2 = ToJoltRVec3(worldAnchor);
    settings.mHingeAxis1 = settings.mHingeAxis2 = ToJoltVec3(axis);
    settings.mNormalAxis1 = settings.mNormalAxis2 = ToJoltVec3(normAxis);
    settings.mLimitsMin = glm::radians(std::clamp(minAngleDeg, -180.0f, 0.0f));
    settings.mLimitsMax = glm::radians(std::clamp(maxAngleDeg, 0.0f, 180.0f));

    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    JPH::Ref<JPH::Constraint> c = bi.CreateConstraint(&settings, itA->second, itB->second);
    if (!c) return false;

    m_impl->physicsSystem->AddConstraint(c);
    m_impl->constraints.push_back(c);
    ++m_constraintCount;
    return true;
}

bool PhysicsWorld::AddDistanceJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                                    float minDistance,
                                    float maxDistance) {
    if (!m_initialized) return false;
    auto itA = m_impl->nodeToBody.find(nodeIdA);
    auto itB = m_impl->nodeToBody.find(nodeIdB);
    if (itA == m_impl->nodeToBody.end() || itB == m_impl->nodeToBody.end()) return false;

    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    JPH::DistanceConstraintSettings settings;
    settings.mPoint1 = bi.GetPosition(itA->second);
    settings.mPoint2 = bi.GetPosition(itB->second);
    settings.mMinDistance = std::max(0.0f, minDistance);
    settings.mMaxDistance = std::max(settings.mMinDistance, maxDistance);

    JPH::Ref<JPH::Constraint> c = bi.CreateConstraint(&settings, itA->second, itB->second);
    if (!c) return false;

    m_impl->physicsSystem->AddConstraint(c);
    m_impl->constraints.push_back(c);
    ++m_constraintCount;
    return true;
}

bool PhysicsWorld::AddSwingTwistJoint(uint32_t nodeIdA, uint32_t nodeIdB,
                                      const glm::vec3& worldAnchor,
                                      const glm::vec3& twistAxis,
                                      float normalHalfConeDeg,
                                      float planeHalfConeDeg,
                                      float twistMinDeg,
                                      float twistMaxDeg) {
    if (!m_initialized) return false;
    auto itA = m_impl->nodeToBody.find(nodeIdA);
    auto itB = m_impl->nodeToBody.find(nodeIdB);
    if (itA == m_impl->nodeToBody.end() || itB == m_impl->nodeToBody.end()) return false;

    glm::vec3 tAxis = glm::normalize(twistAxis);
    glm::vec3 pAxis = (std::abs(tAxis.x) < 0.9f)
        ? glm::normalize(glm::cross(tAxis, glm::vec3(1.0f, 0.0f, 0.0f)))
        : glm::normalize(glm::cross(tAxis, glm::vec3(0.0f, 1.0f, 0.0f)));

    JPH::SwingTwistConstraintSettings settings;
    settings.mPosition1 = settings.mPosition2 = ToJoltRVec3(worldAnchor);
    settings.mTwistAxis1 = settings.mTwistAxis2 = ToJoltVec3(tAxis);
    settings.mPlaneAxis1 = settings.mPlaneAxis2 = ToJoltVec3(pAxis);
    settings.mNormalHalfConeAngle = glm::radians(normalHalfConeDeg);
    settings.mPlaneHalfConeAngle = glm::radians(planeHalfConeDeg);
    settings.mTwistMinAngle = glm::radians(twistMinDeg);
    settings.mTwistMaxAngle = glm::radians(twistMaxDeg);

    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    JPH::Ref<JPH::Constraint> c = bi.CreateConstraint(&settings, itA->second, itB->second);
    if (!c) return false;

    m_impl->physicsSystem->AddConstraint(c);
    m_impl->constraints.push_back(c);
    ++m_constraintCount;
    return true;
}

uint32_t PhysicsWorld::CreateHumanoidRagdoll(const Animation::Skeleton& skeleton,
                                             const glm::vec3& rootWorldOrigin) {
    if (!m_initialized || skeleton.bones.empty()) return 0;

    auto boneWorldPositions = Animation::IKSolver::ComputeBoneWorldPositions(skeleton);
    auto& bi = m_impl->physicsSystem->GetBodyInterface();

    RagdollInstance instance;
    instance.id = m_nextRagdollId++;

    JPH::RefConst<JPH::Shape> limbShape = new JPH::CapsuleShape(0.12f, 0.06f);

    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        glm::vec3 bonePos = rootWorldOrigin + boneWorldPositions[i];
        JPH::BodyCreationSettings bcs(
            limbShape,
            ToJoltRVec3(bonePos),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic,
            Layers::MOVING
        );
        bcs.mLinearDamping = 0.15f;
        bcs.mAngularDamping = 0.25f;
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = 5.0f;

        JPH::BodyID bid = bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
        instance.joltBodyIds.push_back(bid.GetIndexAndSequenceNumber());
        instance.boneIndices.push_back(static_cast<int>(i));

        int parentIdx = skeleton.bones[i].parentId;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(i)) {
            JPH::BodyID parentBid(instance.joltBodyIds[parentIdx]);
            JPH::SwingTwistConstraintSettings stSettings;
            stSettings.mPosition1 = stSettings.mPosition2 = ToJoltRVec3(bonePos);
            stSettings.mTwistAxis1 = stSettings.mTwistAxis2 = JPH::Vec3::sAxisY();
            stSettings.mPlaneAxis1 = stSettings.mPlaneAxis2 = JPH::Vec3::sAxisX();
            stSettings.mNormalHalfConeAngle = glm::radians(35.0f);
            stSettings.mPlaneHalfConeAngle = glm::radians(35.0f);
            stSettings.mTwistMinAngle = glm::radians(-25.0f);
            stSettings.mTwistMaxAngle = glm::radians(25.0f);

            JPH::Ref<JPH::Constraint> c = bi.CreateConstraint(&stSettings, parentBid, bid);
            if (c) {
                m_impl->physicsSystem->AddConstraint(c);
                m_impl->constraints.push_back(c);
                ++m_constraintCount;
            }
        }
    }

    m_impl->ragdolls[instance.id] = instance;
    return instance.id;
}

bool PhysicsWorld::SyncRagdollToSkeleton(uint32_t ragdollId, Animation::Skeleton& skeleton) const {
    if (!m_initialized) return false;
    auto it = m_impl->ragdolls.find(ragdollId);
    if (it == m_impl->ragdolls.end()) return false;

    const auto& rag = it->second;
    auto& bi = m_impl->physicsSystem->GetBodyInterface();

    std::vector<glm::vec3> worldPositions(rag.joltBodyIds.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < rag.joltBodyIds.size(); ++i) {
        JPH::BodyID bid(rag.joltBodyIds[i]);
        if (bid.IsInvalid()) continue;

        JPH::RVec3 pos;
        JPH::Quat rot;
        bi.GetPositionAndRotation(bid, pos, rot);
        worldPositions[i] = ToGlmVec3(pos);

        int boneIdx = rag.boneIndices[i];
        if (boneIdx >= 0 && boneIdx < static_cast<int>(skeleton.bones.size())) {
            skeleton.bones[boneIdx].currentRotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
            int parentIdx = skeleton.bones[boneIdx].parentId;
            if (parentIdx >= 0 && parentIdx < static_cast<int>(i)) {
                skeleton.bones[boneIdx].currentPosition = worldPositions[i] - worldPositions[parentIdx];
            } else {
                skeleton.bones[boneIdx].currentPosition = worldPositions[i];
            }
        }
    }
    return true;
}

void PhysicsWorld::ApplyRagdollImpulse(uint32_t ragdollId, const glm::vec3& impulse) {
    if (!m_initialized) return;
    auto it = m_impl->ragdolls.find(ragdollId);
    if (it == m_impl->ragdolls.end()) return;

    auto& bi = m_impl->physicsSystem->GetBodyInterface();
    for (uint32_t rawId : it->second.joltBodyIds) {
        JPH::BodyID bid(rawId);
        if (!bid.IsInvalid()) {
            bi.ActivateBody(bid);
            bi.AddImpulse(bid, ToJoltVec3(impulse));
        }
    }
}

glm::vec3 PhysicsWorld::GetRagdollRootPosition(uint32_t ragdollId) const {
    if (!m_initialized) return glm::vec3(0.0f);
    auto it = m_impl->ragdolls.find(ragdollId);
    if (it == m_impl->ragdolls.end() || it->second.joltBodyIds.empty()) return glm::vec3(0.0f);

    JPH::BodyID rootBid(it->second.joltBodyIds[0]);
    if (rootBid.IsInvalid()) return glm::vec3(0.0f);
    return ToGlmVec3(m_impl->physicsSystem->GetBodyInterface().GetPosition(rootBid));
}

void PhysicsWorld::SetCollisionLayer(uint32_t nodeId, uint32_t layer) {
    if (!m_impl) return;
    m_impl->nodeCollisionFilters[nodeId].layer = layer;
}

uint32_t PhysicsWorld::GetCollisionLayer(uint32_t nodeId) const {
    if (!m_impl) return 1;
    auto it = m_impl->nodeCollisionFilters.find(nodeId);
    return it != m_impl->nodeCollisionFilters.end() ? it->second.layer : 1;
}

void PhysicsWorld::SetCollisionMask(uint32_t nodeId, uint32_t mask) {
    if (!m_impl) return;
    m_impl->nodeCollisionFilters[nodeId].mask = mask;
}

uint32_t PhysicsWorld::GetCollisionMask(uint32_t nodeId) const {
    if (!m_impl) return 1;
    auto it = m_impl->nodeCollisionFilters.find(nodeId);
    return it != m_impl->nodeCollisionFilters.end() ? it->second.mask : 1;
}

bool PhysicsWorld::CanCollide(uint32_t nodeIdA, uint32_t nodeIdB) const {
    uint32_t lA = GetCollisionLayer(nodeIdA);
    uint32_t mA = GetCollisionMask(nodeIdA);
    uint32_t lB = GetCollisionLayer(nodeIdB);
    uint32_t mB = GetCollisionMask(nodeIdB);
    return ((lA & mB) != 0) || ((lB & mA) != 0);
}

} // namespace Physics
