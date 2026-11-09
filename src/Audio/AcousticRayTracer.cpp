#include "AcousticRayTracer.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace Audio {

AcousticRayTracer& AcousticRayTracer::Get() {
    static AcousticRayTracer instance;
    return instance;
}

AcousticRayTracer::AcousticRayTracer() {
}

void AcousticRayTracer::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_obstacles.clear();
    m_portals.clear();
}

void AcousticRayTracer::AddObstacle(const glm::vec3& boxMin, const glm::vec3& boxMax, WallMaterialType material) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_obstacles.push_back({ boxMin, boxMax, material });
}

void AcousticRayTracer::SetObstacles(std::vector<AcousticObstacle> obstacles) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_obstacles = std::move(obstacles);
}

size_t AcousticRayTracer::GetObstacleCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_obstacles.size();
}

std::vector<AcousticPortal> AcousticRayTracer::GetPortals() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_portals;
}

void AcousticRayTracer::AddPortal(const AcousticPortal& portal) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_portals.push_back(portal);
}

void AcousticRayTracer::SetPortalOpenFraction(uint32_t portalId, float openFraction) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_portals) {
        if (p.portalId == portalId) {
            p.openFraction = glm::clamp(openFraction, 0.0f, 1.0f);
            break;
        }
    }
}

bool AcousticRayTracer::IntersectRayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                         const glm::vec3& boxMin, const glm::vec3& boxMax, float& tOut) const {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-6f) {
            if (rayOrigin[i] < boxMin[i] || rayOrigin[i] > boxMax[i]) return false;
        } else {
            float ood = 1.0f / rayDir[i];
            float t1 = (boxMin[i] - rayOrigin[i]) * ood;
            float t2 = (boxMax[i] - rayOrigin[i]) * ood;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }

    tOut = tmin;
    return true;
}

RayTracedAcousticResult AcousticRayTracer::TracePath(const glm::vec3& emitterPos, const glm::vec3& listenerPos) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    RayTracedAcousticResult result;
    result.hasDirectLineOfSight = true;
    result.isPortaled = false;
    result.virtualEmitterPos = emitterPos;

    glm::vec3 toListener = listenerPos - emitterPos;
    float directDist = glm::length(toListener);
    result.effectiveDistance = directDist;

    if (directDist <= 0.0001f) {
        return result;
    }

    glm::vec3 rayDir = toListener / directDist;

    // Check direct line of sight intersection with obstacles
    bool hitObstacle = false;
    WallMaterialType hitMaterial = WallMaterialType::Concrete;

    for (const auto& obs : m_obstacles) {
        float tHit = 0.0f;
        if (IntersectRayAABB(emitterPos, rayDir, obs.boxMin, obs.boxMax, tHit)) {
            if (tHit > 0.001f && tHit < directDist) {
                hitObstacle = true;
                hitMaterial = obs.material;
                break;
            }
        }
    }

    if (!hitObstacle) {
        result.hasDirectLineOfSight = true;
        result.transmissionLossDB = 0.0f;
        result.diffractionAttenuation = 1.0f;
        result.lowPassCutoffHz = 20000.0f;
        result.perceivedGain = 1.0f;
        return result;
    }

    result.hasDirectLineOfSight = false;

    // Line of sight is occluded! Check if there is an acoustic portal (door/window)
    const AcousticPortal* bestPortal = nullptr;
    float shortestPortaledDist = std::numeric_limits<float>::max();

    for (const auto& portal : m_portals) {
        if (portal.openFraction > 0.01f) {
            float d1 = glm::length(emitterPos - portal.center);
            float d2 = glm::length(listenerPos - portal.center);
            float totalD = d1 + d2;
            if (totalD < shortestPortaledDist) {
                shortestPortaledDist = totalD;
                bestPortal = &portal;
            }
        }
    }

    if (bestPortal) {
        // Sound portaling active (Huygens' Principle: doorway becomes virtual emitter)
        result.isPortaled = true;
        result.virtualEmitterPos = bestPortal->center;
        result.effectiveDistance = shortestPortaledDist;

        // Aperture diffraction: open door lets high frequencies through, narrow gap muffles
        result.portalAperture = bestPortal->openFraction;
        result.diffractionAttenuation = 0.2f + 0.8f * bestPortal->openFraction;
        result.lowPassCutoffHz = 600.0f + 19400.0f * (bestPortal->openFraction * bestPortal->openFraction);
        result.transmissionLossDB = (1.0f - bestPortal->openFraction) * 20.0f;

        float dbLinear = std::pow(10.0f, -result.transmissionLossDB / 20.0f);
        result.perceivedGain = result.diffractionAttenuation * dbLinear;
    } else {
        // Direct solid wall penetration & edge diffraction
        float lossDB = 35.0f;
        float cutoff = 500.0f;

        switch (hitMaterial) {
            case WallMaterialType::Concrete: lossDB = 35.0f; cutoff = 450.0f; break;
            case WallMaterialType::Wood:     lossDB = 15.0f; cutoff = 1200.0f; break;
            case WallMaterialType::Glass:    lossDB = 22.0f; cutoff = 800.0f; break;
            case WallMaterialType::Curtain:  lossDB = 6.0f;  cutoff = 3500.0f; break;
        }

        result.transmissionLossDB = lossDB;
        result.diffractionAttenuation = 0.25f; // Solid obstruction shadow zone
        result.lowPassCutoffHz = cutoff;

        float dbLinear = std::pow(10.0f, -lossDB / 20.0f);
        result.perceivedGain = dbLinear * result.diffractionAttenuation;
    }

    return result;
}

} // namespace Audio
