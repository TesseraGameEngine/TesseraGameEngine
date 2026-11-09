#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Physics {

struct XPBDParticle {
    glm::vec3 position{0.0f};
    glm::vec3 prevPosition{0.0f};
    glm::vec3 velocity{0.0f};
    float invMass{1.0f}; // 0.0f = pinned/immovable
};

struct XPBDBoundaryCapsule {
    glm::vec3 p0{0.0f};
    glm::vec3 p1{0.0f, 1.0f, 0.0f};
    float radius{0.25f};
};

struct XPBDDistanceConstraint {
    uint32_t p1{0};
    uint32_t p2{0};
    float restLength{0.1f};
    float compliance{0.00001f}; // XPBD compliance alpha (m/N)
    float lambda{0.0f};         // Lagrange multiplier
};

struct XPBDClothSettings {
    bool enableSimulation{true};
    int substeps{4};
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    glm::vec3 windVelocity{2.0f, 0.0f, 1.0f};
    float dragCoefficient{0.05f};
    float stretchCompliance{0.00001f};
    float bendCompliance{0.001f};
    float damping{0.99f};
};

struct DynamicHairSettings {
    bool enableHairSim{true};
    int substeps{4};
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float bendStiffness{0.8f};
    float drag{0.1f};
    float damping{0.95f};
};

class XPBDClothSimulator {
public:
    static XPBDClothSimulator& Get();

    void InitializeGridCloth(float width, float height, uint32_t resX, uint32_t resY, float totalMass = 1.0f);
    void PinParticle(uint32_t index);
    void UnpinParticle(uint32_t index);

    // Collision volumes (e.g. character limbs/torso capsules)
    void ClearColliders();
    void AddCapsuleCollider(const glm::vec3& p0, const glm::vec3& p1, float radius);

    // Simulation stepping
    void Step(float dt, const XPBDClothSettings& settings = XPBDClothSettings());

    const std::vector<XPBDParticle>& GetParticles() const { return m_particles; }
    std::vector<XPBDParticle>& GetParticlesMutable() { return m_particles; }
    const std::vector<XPBDDistanceConstraint>& GetConstraints() const { return m_constraints; }
    size_t GetParticleCount() const { return m_particles.size(); }
    size_t GetConstraintCount() const { return m_constraints.size(); }

    XPBDClothSimulator();

private:
    void SolveConstraints(float dtSub);
    void ResolveCollisions();

    std::vector<XPBDParticle> m_particles;
    std::vector<XPBDDistanceConstraint> m_constraints;
    std::vector<XPBDBoundaryCapsule> m_colliders;
};

// Dynamic Hair Simulation using XPBD elastic rod chains
class DynamicHairSimulator {
public:
    static DynamicHairSimulator& Get();

    void CreateStrand(const glm::vec3& rootPos, const glm::vec3& dir, float strandLength, uint32_t segments);
    void ClearStrands();

    void Step(float dt, const DynamicHairSettings& settings = DynamicHairSettings());

    const std::vector<XPBDParticle>& GetHairParticles() const { return m_particles; }
    size_t GetStrandCount() const { return m_strandRootIndices.size(); }
    size_t GetTotalParticles() const { return m_particles.size(); }

private:
    DynamicHairSimulator();
    void SolveStrands(float dtSub);

    std::vector<XPBDParticle> m_particles;
    std::vector<XPBDDistanceConstraint> m_constraints;
    std::vector<uint32_t> m_strandRootIndices;
    std::vector<uint32_t> m_strandSegmentCounts;
};

} // namespace Physics

