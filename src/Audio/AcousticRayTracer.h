#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Audio {

enum class WallMaterialType {
    Concrete, // High transmission loss (~35 dB), high reflection
    Wood,     // Medium transmission loss (~15 dB), warm absorption
    Glass,    // Medium-high transmission loss (~22 dB), bright reflection
    Curtain   // Low transmission loss (~5 dB), high absorption
};

struct AcousticObstacle {
    glm::vec3 boxMin{-1.0f};
    glm::vec3 boxMax{1.0f};
    WallMaterialType material{WallMaterialType::Concrete};
};

struct AcousticPortal {
    uint32_t portalId{0};
    std::string name{"Doorway"};
    glm::vec3 center{0.0f};
    glm::vec2 size{1.0f, 2.0f}; // Width, Height
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    float openFraction{1.0f}; // 0.0 = closed/sealed, 1.0 = fully open
    int roomAId{0};
    int roomBId{1};
};

struct RayTracedAcousticResult {
    bool hasDirectLineOfSight{true};
    bool isPortaled{false};
    float portalAperture{1.0f};         // [0, 1] aperture of connecting doorway
    float transmissionLossDB{0.0f};    // Direct wall penetration attenuation
    float diffractionAttenuation{1.0f}; // [0, 1] amplitude factor
    float lowPassCutoffHz{20000.0f};    // Low-pass filter frequency (Hz)
    glm::vec3 virtualEmitterPos{0.0f};  // Perceived sound origin (e.g. at portal door)
    float effectiveDistance{0.0f};      // Total path length
    float perceivedGain{1.0f};          // Final composite linear gain
};

class AcousticRayTracer {
public:
    static AcousticRayTracer& Get();

    void Clear();
    void AddObstacle(const glm::vec3& boxMin, const glm::vec3& boxMax, WallMaterialType material = WallMaterialType::Concrete);
    void AddPortal(const AcousticPortal& portal);
    void SetPortalOpenFraction(uint32_t portalId, float openFraction);

    // Traces acoustic rays, evaluates edge diffraction, and calculates door portaling
    RayTracedAcousticResult TracePath(const glm::vec3& emitterPos, const glm::vec3& listenerPos) const;

    const std::vector<AcousticObstacle>& GetObstacles() const { return m_obstacles; }
    const std::vector<AcousticPortal>& GetPortals() const { return m_portals; }

private:
    AcousticRayTracer();

    bool IntersectRayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                          const glm::vec3& boxMin, const glm::vec3& boxMax, float& tOut) const;

    std::vector<AcousticObstacle> m_obstacles;
    std::vector<AcousticPortal> m_portals;
};

} // namespace Audio
