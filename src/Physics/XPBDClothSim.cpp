#include "XPBDClothSim.h"
#include <cmath>
#include <algorithm>

namespace Physics {

XPBDClothSimulator& XPBDClothSimulator::Get() {
    static XPBDClothSimulator instance;
    return instance;
}

XPBDClothSimulator::XPBDClothSimulator() {
    InitializeGridCloth(1.0f, 1.0f, 10, 10);
}

void XPBDClothSimulator::InitializeGridCloth(float width, float height, uint32_t resX, uint32_t resY, float totalMass) {
    m_particles.clear();
    m_constraints.clear();

    if (resX < 2 || resY < 2) return;

    uint32_t totalParticles = resX * resY;
    float particleMass = totalMass / static_cast<float>(totalParticles);
    float invMass = particleMass > 0.0001f ? (1.0f / particleMass) : 1.0f;

    float dx = width / static_cast<float>(resX - 1);
    float dy = height / static_cast<float>(resY - 1);

    m_particles.resize(totalParticles);

    for (uint32_t y = 0; y < resY; ++y) {
        for (uint32_t x = 0; x < resX; ++x) {
            uint32_t idx = y * resX + x;
            glm::vec3 pos(
                (static_cast<float>(x) - static_cast<float>(resX - 1) * 0.5f) * dx,
                1.5f - static_cast<float>(y) * dy,
                0.0f
            );
            m_particles[idx].position = pos;
            m_particles[idx].prevPosition = pos;
            m_particles[idx].velocity = glm::vec3(0.0f);
            m_particles[idx].invMass = invMass;
        }
    }

    // Pin top edge corners by default
    PinParticle(0);
    PinParticle(resX - 1);

    // 1. Structural horizontal constraints
    for (uint32_t y = 0; y < resY; ++y) {
        for (uint32_t x = 0; x < resX - 1; ++x) {
            uint32_t p1 = y * resX + x;
            uint32_t p2 = y * resX + (x + 1);
            m_constraints.push_back({ p1, p2, dx, 0.00001f, 0.0f });
        }
    }

    // 2. Structural vertical constraints
    for (uint32_t y = 0; y < resY - 1; ++y) {
        for (uint32_t x = 0; x < resX; ++x) {
            uint32_t p1 = y * resX + x;
            uint32_t p2 = (y + 1) * resX + x;
            m_constraints.push_back({ p1, p2, dy, 0.00001f, 0.0f });
        }
    }

    // 3. Shear diagonal constraints
    float diag = std::sqrt(dx * dx + dy * dy);
    for (uint32_t y = 0; y < resY - 1; ++y) {
        for (uint32_t x = 0; x < resX - 1; ++x) {
            uint32_t p1 = y * resX + x;
            uint32_t p2 = (y + 1) * resX + (x + 1);
            m_constraints.push_back({ p1, p2, diag, 0.0001f, 0.0f });

            uint32_t p3 = y * resX + (x + 1);
            uint32_t p4 = (y + 1) * resX + x;
            m_constraints.push_back({ p3, p4, diag, 0.0001f, 0.0f });
        }
    }

    // 4. Bending constraints (second-neighbor)
    for (uint32_t y = 0; y < resY; ++y) {
        for (uint32_t x = 0; x < resX - 2; ++x) {
            uint32_t p1 = y * resX + x;
            uint32_t p2 = y * resX + (x + 2);
            m_constraints.push_back({ p1, p2, dx * 2.0f, 0.001f, 0.0f });
        }
    }
}

void XPBDClothSimulator::PinParticle(uint32_t index) {
    if (index < m_particles.size()) {
        m_particles[index].invMass = 0.0f;
    }
}

void XPBDClothSimulator::UnpinParticle(uint32_t index) {
    if (index < m_particles.size()) {
        m_particles[index].invMass = 1.0f;
    }
}

void XPBDClothSimulator::ClearColliders() {
    m_colliders.clear();
}

void XPBDClothSimulator::AddCapsuleCollider(const glm::vec3& p0, const glm::vec3& p1, float radius) {
    m_colliders.push_back({ p0, p1, radius });
}

void XPBDClothSimulator::Step(float dt, const XPBDClothSettings& settings) {
    if (!settings.enableSimulation || m_particles.empty() || dt <= 0.00001f) return;

    int substeps = std::max(1, settings.substeps);
    float dtSub = dt / static_cast<float>(substeps);

    for (int step = 0; step < substeps; ++step) {
        // 1. External forces & position prediction
        for (auto& p : m_particles) {
            if (p.invMass == 0.0f) continue;

            p.prevPosition = p.position;
            glm::vec3 relWind = settings.windVelocity - p.velocity;
            glm::vec3 aeroForce = relWind * settings.dragCoefficient;
            glm::vec3 accel = settings.gravity + aeroForce * p.invMass;

            p.velocity += accel * dtSub;
            p.position += p.velocity * dtSub;
        }

        // 2. Solve XPBD constraints
        SolveConstraints(dtSub);

        // 3. Resolve Collisions
        ResolveCollisions();

        // 4. Velocity update & damping
        for (auto& p : m_particles) {
            if (p.invMass == 0.0f) continue;
            p.velocity = (p.position - p.prevPosition) / dtSub * settings.damping;
        }
    }
}

void XPBDClothSimulator::SolveConstraints(float dtSub) {
    float dtSub2 = dtSub * dtSub;

    for (auto& c : m_constraints) {
        auto& p1 = m_particles[c.p1];
        auto& p2 = m_particles[c.p2];

        float wTotal = p1.invMass + p2.invMass;
        if (wTotal <= 0.00001f) continue;

        glm::vec3 diff = p1.position - p2.position;
        float dist = glm::length(diff);
        if (dist <= 0.00001f) continue;

        glm::vec3 n = diff / dist;
        float C = dist - c.restLength;

        // XPBD compliance: alphaTilde = compliance / dt^2
        float alphaTilde = c.compliance / dtSub2;
        float deltaLambda = (-C - alphaTilde * c.lambda) / (wTotal + alphaTilde);
        c.lambda += deltaLambda;

        glm::vec3 deltaP = n * deltaLambda;
        p1.position += deltaP * p1.invMass;
        p2.position -= deltaP * p2.invMass;
    }
}

void XPBDClothSimulator::ResolveCollisions() {
    for (const auto& cap : m_colliders) {
        glm::vec3 ab = cap.p1 - cap.p0;
        float abLen2 = glm::dot(ab, ab);

        for (auto& p : m_particles) {
            if (p.invMass == 0.0f) continue;

            // Project onto capsule segment
            float t = (abLen2 > 0.0001f) ? glm::clamp(glm::dot(p.position - cap.p0, ab) / abLen2, 0.0f, 1.0f) : 0.0f;
            glm::vec3 closestPoint = cap.p0 + ab * t;

            glm::vec3 toParticle = p.position - closestPoint;
            float dist = glm::length(toParticle);
            if (dist < cap.radius && dist > 0.00001f) {
                glm::vec3 normal = toParticle / dist;
                p.position = closestPoint + normal * cap.radius;
            }
        }
    }
}

// Dynamic Hair Simulator implementation
DynamicHairSimulator& DynamicHairSimulator::Get() {
    static DynamicHairSimulator instance;
    return instance;
}

DynamicHairSimulator::DynamicHairSimulator() {
}

void DynamicHairSimulator::ClearStrands() {
    m_particles.clear();
    m_constraints.clear();
    m_strandRootIndices.clear();
    m_strandSegmentCounts.clear();
}

void DynamicHairSimulator::CreateStrand(const glm::vec3& rootPos, const glm::vec3& dir, float strandLength, uint32_t segments) {
    if (segments < 1) return;

    uint32_t startIdx = static_cast<uint32_t>(m_particles.size());
    m_strandRootIndices.push_back(startIdx);
    m_strandSegmentCounts.push_back(segments);

    float segLength = strandLength / static_cast<float>(segments);
    glm::vec3 unitDir = (glm::length(dir) > 0.0001f) ? glm::normalize(dir) : glm::vec3(0.0f, -1.0f, 0.0f);

    for (uint32_t i = 0; i <= segments; ++i) {
        glm::vec3 pos = rootPos + unitDir * (segLength * static_cast<float>(i));
        XPBDParticle p;
        p.position = pos;
        p.prevPosition = pos;
        p.velocity = glm::vec3(0.0f);
        p.invMass = (i == 0) ? 0.0f : 1.0f; // Root pinned to scalp
        m_particles.push_back(p);
    }

    // Distance and bending constraints along strand
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t p1 = startIdx + i;
        uint32_t p2 = startIdx + i + 1;
        m_constraints.push_back({ p1, p2, segLength, 0.00001f, 0.0f });

        // Bending constraint (every 2 segments)
        if (i + 2 <= segments) {
            uint32_t p3 = startIdx + i + 2;
            m_constraints.push_back({ p1, p3, segLength * 2.0f, 0.001f, 0.0f });
        }
    }
}

void DynamicHairSimulator::Step(float dt, const DynamicHairSettings& settings) {
    if (!settings.enableHairSim || m_particles.empty() || dt <= 0.00001f) return;

    int substeps = std::max(1, settings.substeps);
    float dtSub = dt / static_cast<float>(substeps);

    for (int step = 0; step < substeps; ++step) {
        for (auto& p : m_particles) {
            if (p.invMass == 0.0f) continue;
            p.prevPosition = p.position;
            p.velocity += settings.gravity * dtSub;
            p.position += p.velocity * dtSub;
        }

        SolveStrands(dtSub);

        for (auto& p : m_particles) {
            if (p.invMass == 0.0f) continue;
            p.velocity = (p.position - p.prevPosition) / dtSub * settings.damping;
        }
    }
}

void DynamicHairSimulator::SolveStrands(float dtSub) {
    float dtSub2 = dtSub * dtSub;

    for (auto& c : m_constraints) {
        auto& p1 = m_particles[c.p1];
        auto& p2 = m_particles[c.p2];

        float wTotal = p1.invMass + p2.invMass;
        if (wTotal <= 0.00001f) continue;

        glm::vec3 diff = p1.position - p2.position;
        float dist = glm::length(diff);
        if (dist <= 0.00001f) continue;

        glm::vec3 n = diff / dist;
        float C = dist - c.restLength;

        float alphaTilde = c.compliance / dtSub2;
        float deltaLambda = (-C - alphaTilde * c.lambda) / (wTotal + alphaTilde);
        c.lambda += deltaLambda;

        glm::vec3 deltaP = n * deltaLambda;
        p1.position += deltaP * p1.invMass;
        p2.position -= deltaP * p2.invMass;
    }
}

} // namespace Physics

