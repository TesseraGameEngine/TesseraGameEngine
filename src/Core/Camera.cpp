#include "Camera.h"
#include <algorithm>

Camera::Camera() {
    UpdateVectors();
}

void Camera::Rotate(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch += deltaPitch;

    // Constrain pitch to avoid flipping
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    UpdateVectors();
}

void Camera::Pan(float deltaX, float deltaY) {
    // Calculate right and up vectors relative to camera
    glm::vec3 forward = glm::normalize(m_target - m_position);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    float panSpeed = m_distance * 0.002f;
    m_target += (-right * deltaX + up * deltaY) * panSpeed;

    UpdateVectors();
}

void Camera::Zoom(float deltaZoom) {
    m_distance -= deltaZoom * (m_distance * 0.1f);
    m_distance = std::clamp(m_distance, 0.5f, 100.0f);

    UpdateVectors();
}

void Camera::SetViewportSize(float width, float height) {
    if (height > 0.0f) {
        m_aspectRatio = width / height;
    }
}

void Camera::Reset() {
    m_target = glm::vec3(0.0f, 0.0f, 0.0f);
    m_distance = 5.0f;
    m_yaw = 45.0f;
    m_pitch = 25.0f;
    UpdateVectors();
}

void Camera::SetPresetView(float yaw, float pitch, float distance) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    if (distance > 0.0f) {
        m_distance = distance;
    }
    UpdateVectors();
}

void Camera::SetTarget(const glm::vec3& target) {
    m_target = target;
    UpdateVectors();
}

void Camera::FocusOn(const glm::vec3& target, float distance) {
    m_target = target;
    if (distance > 0.0f) {
        m_distance = distance;
    }
    UpdateVectors();
}

void Camera::UpdateVectors() {
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    float x = m_distance * cosf(pitchRad) * sinf(yawRad);
    float y = m_distance * sinf(pitchRad);
    float z = m_distance * cosf(pitchRad) * cosf(yawRad);

    m_position = m_target + glm::vec3(x, y, z);
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(m_position, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearZ, m_farZ);
    // Vulkan clip space Y inverted
    proj[1][1] *= -1.0f;
    return proj;
}

glm::vec3 Camera::GetPosition() const {
    return m_position;
}
bool Camera::WorldToScreen(const glm::vec3& worldPos, const glm::vec2& viewportPos, const glm::vec2& viewportSize, glm::vec2& outScreenPos) const {
    glm::mat4 view = GetViewMatrix();
    glm::mat4 proj = GetProjectionMatrix();
    glm::vec4 clipPos = proj * (view * glm::vec4(worldPos, 1.0f));

    if (clipPos.w <= 0.0001f) {
        return false;
    }

    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    // With Vulkan inverted Y in projection: -1 is top, +1 is bottom
    outScreenPos.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
    outScreenPos.y = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;

    return (ndc.z >= 0.0f && ndc.z <= 1.0f);
}
