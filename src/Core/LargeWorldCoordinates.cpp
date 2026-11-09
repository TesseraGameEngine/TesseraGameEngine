#include "LargeWorldCoordinates.h"
#include <cmath>
#include <limits>

namespace Core {
namespace LWC {

CameraRelativeCoordinator& CameraRelativeCoordinator::Get() {
    static CameraRelativeCoordinator instance;
    return instance;
}

glm::mat4 CameraRelativeCoordinator::ComputeCameraRelativeModelMatrix(const LargeWorldTransform& transform) const {
    if (!m_enabled) {
        // Fallback: standard 32-bit absolute matrix
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(transform.position));
        m = m * glm::mat4_cast(transform.rotation);
        m = glm::scale(m, transform.scale);
        return m;
    }

    // 64-bit camera-relative subtraction
    WorldVector relPos = transform.position - m_cameraPosition;

    // Convert high-precision relative offset to 32-bit float for GPU vertex processing
    glm::vec3 relPosF = glm::vec3(relPos);

    glm::mat4 m = glm::translate(glm::mat4(1.0f), relPosF);
    m = m * glm::mat4_cast(transform.rotation);
    m = glm::scale(m, transform.scale);
    return m;
}

glm::mat4 CameraRelativeCoordinator::ComputeCameraRelativeModelMatrix(const glm::mat4& modelMatrix, const glm::dvec3& worldOffset) const {
    if (!m_enabled) {
        if (worldOffset == glm::dvec3(0.0)) return modelMatrix;
        return glm::translate(glm::mat4(1.0f), glm::vec3(worldOffset)) * modelMatrix;
    }
    glm::dvec3 objWorldPos = worldOffset + glm::dvec3(modelMatrix[3]);
    glm::dvec3 relPos = objWorldPos - m_cameraPosition;
    glm::mat4 relModel = modelMatrix;
    relModel[3] = glm::vec4(glm::vec3(relPos), 1.0f);
    return relModel;
}

glm::mat4 CameraRelativeCoordinator::ExtractRotationOnlyViewMatrix(const glm::mat4& viewMatrix) {
    // Upper-left 3x3 orientation components only; zero translation in column 3
    glm::mat4 rotOnly = glm::mat4(glm::mat3(viewMatrix));
    rotOnly[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return rotOnly;
}

double CameraRelativeCoordinator::EstimateFloatingPointStepAtDistance(double distanceMeters) {
    // Standard IEEE 754 single precision float (24 bit mantissa) machine epsilon ~ 1.19e-7
    float distF = static_cast<float>(distanceMeters);
    float nextF = std::nextafter(distF, std::numeric_limits<float>::infinity());
    return static_cast<double>(nextF - distF);
}

bool CameraRelativeCoordinator::VerifyPrecisionIntegrity(const WorldVector& camPos, const WorldVector& objPos) {
    LargeWorldTransform objTransform;
    objTransform.position = objPos;

    CameraRelativeCoordinator coord;
    coord.SetCameraWorldPosition(camPos);
    glm::mat4 relModel = coord.ComputeCameraRelativeModelMatrix(objTransform);

    // Compute expected distance
    WorldVector diff = objPos - camPos;
    glm::vec3 relTranslation(relModel[3]);

    float error = glm::distance(glm::vec3(diff), relTranslation);
    return error < 1e-4f;
}

} // namespace LWC
} // namespace Core
