#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "Scene/Scene.h"
#include "Physics/Collision.h"

namespace Gameplay {

class CharacterController {
public:
    uint32_t targetNodeId{0};

    float moveSpeed{6.0f};
    float runSpeedMultiplier{1.8f};
    float jumpForce{6.5f};
    float gravity{18.0f};

    glm::vec3 velocity{0.0f};
    bool isGrounded{false};
    bool isRunning{false};

    void OnInit(Scene& scene);
    void OnUpdate(Scene& scene, float dt, const glm::vec3& inputDir, bool jumpPressed, bool runHeld, bool interactPressed, bool attackPressed = false, bool secondaryPressed = false);
    void OnFixedUpdate(Scene& scene, float fixedDt);
};

class ScriptEngine {
public:
    ScriptEngine();

    void InitForPlay(Scene& scene);
    void Stop(Scene& scene);
    void Update(Scene& scene, float dt, bool isGameWindowFocused = true);
    void FixedUpdate(Scene& scene, float fixedDt);
    void RenderHUD(Scene& scene);

    CharacterController& GetPlayerController() { return m_playerController; }

private:
    CharacterController m_playerController;
    float m_fixedTimeAccumulator{0.0f};
};

} // namespace Gameplay
