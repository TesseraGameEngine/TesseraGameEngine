#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera();

    void Rotate(float deltaYaw, float deltaPitch);
    void Pan(float deltaX, float deltaY);
    void Zoom(float deltaZoom);

    void SetViewportSize(float width, float height);
    void Reset();
    void SetPresetView(float yaw, float pitch, float distance = 5.0f);
    void SetTarget(const glm::vec3& target);
    void FocusOn(const glm::vec3& target, float distance = -1.0f);

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;
    glm::vec3 GetPosition() const;
    glm::vec3 GetTarget() const { return m_target; }
    bool WorldToScreen(const glm::vec3& worldPos, const glm::vec2& viewportPos, const glm::vec2& viewportSize, glm::vec2& outScreenPos) const;

    float GetFov() const { return m_fov; }
    float GetNearZ() const { return m_nearZ; }
    float GetFarZ() const { return m_farZ; }
    float GetDistance() const { return m_distance; }
    float GetYaw() const { return m_yaw; }
    float GetPitch() const { return m_pitch; }
    void SetViewParams(float yaw, float pitch, float distance, const glm::vec3& target) {
        m_yaw = yaw;
        m_pitch = pitch;
        m_distance = distance;
        m_target = target;
        UpdateVectors();
    }

private:
    void UpdateVectors();

    glm::vec3 m_target{0.0f, 0.0f, 0.0f};
    float m_distance{5.0f};
    float m_yaw{45.0f};     // degrees
    float m_pitch{25.0f};   // degrees

    float m_fov{45.0f};     // degrees
    float m_aspectRatio{16.0f / 9.0f};
    float m_nearZ{0.1f};
    float m_farZ{100.0f};

    glm::vec3 m_position{0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
};

