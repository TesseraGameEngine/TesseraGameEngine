#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace Core {
namespace LWC {

// 64-bit double precision spatial vector
using WorldVector = glm::dvec3;

// Large World Coordinate Transform (64-bit translation + 32-bit rotation/scale)
struct LargeWorldTransform {
    WorldVector position{0.0, 0.0, 0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::dmat4 ToDoubleMatrix() const {
        glm::dmat4 m = glm::translate(glm::dmat4(1.0), position);
        m = m * glm::dmat4(glm::mat4_cast(rotation));
        m = glm::scale(m, glm::dvec3(scale));
        return m;
    }
};

// Camera-Relative rendering coordinator for Large World Coordinates
class CameraRelativeCoordinator {
public:
    static CameraRelativeCoordinator& Get();

    void SetCameraWorldPosition(const WorldVector& camPos) { m_cameraPosition = camPos; }
    const WorldVector& GetCameraWorldPosition() const { return m_cameraPosition; }

    // Computes 32-bit camera-relative model matrix from 64-bit world transform
    glm::mat4 ComputeCameraRelativeModelMatrix(const LargeWorldTransform& transform) const;

    // Isolates the rotation-only view matrix (strips 32-bit world translation from view matrix)
    static glm::mat4 ExtractRotationOnlyViewMatrix(const glm::mat4& viewMatrix);

    // Verifies mathematical equivalence between relative and absolute projection
    static bool VerifyPrecisionIntegrity(const WorldVector& camPos, const WorldVector& objPos);

    // Calculates floating point precision error delta (in meters) for a given distance
    static double EstimateFloatingPointStepAtDistance(double distanceMeters);

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

private:
    CameraRelativeCoordinator() = default;
    ~CameraRelativeCoordinator() = default;

    WorldVector m_cameraPosition{0.0, 0.0, 0.0};
    bool m_enabled{true};
};

} // namespace LWC
} // namespace Core

