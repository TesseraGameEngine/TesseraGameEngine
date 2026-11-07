#include "Core/InputManager.h"
#include "GameplayScripts.h"
#include "Scripting/LuaScriptSystem.h"
#include "Scene/Nodes/3D/CharacterNode.h"
#include "Physics/Collision.h"
#include <iostream>
#include <algorithm>

namespace Gameplay {

void CharacterController::OnInit(Scene& scene) {
    // When a brand new game starts, there are no default controls;
    // all gameplay movement and controls are explicitly defined by the developer via scripts.
    targetNodeId = 0;
    velocity = glm::vec3(0.0f);
    isGrounded = true;
}

void CharacterController::OnUpdate(Scene& scene, float dt, const glm::vec3& inputDir,
                                   bool jumpPressed, bool runHeld, bool interactPressed,
                                   bool attackPressed, bool secondaryPressed) {
    if (targetNodeId == 0) return;
    auto node = scene.GetNodeById(targetNodeId);
    if (!node) return;

    isRunning = runHeld && (glm::length(inputDir) > 0.01f);

    // Horizontal movement calculation with Run / Sprint speed
    float speed = moveSpeed;
    if (runHeld) {
        speed *= runSpeedMultiplier;
    }
    glm::vec3 targetVel = inputDir * speed;
    velocity.x = targetVel.x;
    velocity.z = targetVel.z;

    // Jump
    if (jumpPressed && isGrounded) {
        velocity.y = jumpForce;
        isGrounded = false;
    }

    // Apply movement
    node->position += velocity * dt;
    node->MarkLocallyActive();
}

void CharacterController::OnFixedUpdate(Scene& scene, float fixedDt) {
    if (targetNodeId == 0) return;
    auto node = scene.GetNodeById(targetNodeId);
    if (!node) return;

    // Apply gravity
    velocity.y -= gravity * fixedDt;

    // Ground raycast check & physical solid contact clamping
    float halfHeight = Physics::GetNodeHalfHeight(*node);
    bool hasGround = false;
    float groundY = -999999.0f;
    float hitY = 0.0f;
    glm::vec3 rayOrigin(node->position.x, node->position.y + halfHeight + 0.5f, node->position.z);
    auto groundHit = Physics::RaycastDown(scene, rayOrigin, 1000.0f, node->id, &hitY);
    if (groundHit.hit) {
        groundY = hitY;
        hasGround = true;
    } else {
        auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
        if (charNode && charNode->clampToWorldFloor) {
            groundY = 0.0f;
            hasGround = true;
        }
    }

    if (hasGround) {
        float targetY = groundY + halfHeight;
        if (node->position.y <= targetY) {
            node->position.y = targetY;
            velocity.y = 0.0f;
            isGrounded = true;
        } else {
            isGrounded = false;
        }
    } else {
        isGrounded = false;
    }
}

ScriptEngine::ScriptEngine() = default;

void ScriptEngine::InitForPlay(Scene& scene) {
    // 1. Initialize fresh Lua runtime
    Scripting::LuaScriptSystem::Get().Shutdown();
    Scripting::LuaScriptSystem::Get().SetInputActive(true);
    Scripting::LuaScriptSystem::Get().Init();

    // 2. Initialize CharacterController
    m_playerController.OnInit(scene);
    m_fixedTimeAccumulator = 0.0f;

    // 3. Initialize any node scripts in the scene
    for (const auto& node : scene.GetNodes()) {
        if (!node->scriptPath.empty()) {
            Scripting::LuaScriptSystem::Get().InitNodeScript(*node, scene);
        }
    }

    std::cout << "[ScriptEngine] Initialized Play Mode with Lua scripting support." << std::endl;
}

void ScriptEngine::Stop(Scene& scene) {
    Scripting::LuaScriptSystem::Get().Shutdown();
    Scripting::LuaScriptSystem::Get().SetInputActive(true);
}

void ScriptEngine::FixedUpdate(Scene& scene, float fixedDt) {
    m_playerController.OnFixedUpdate(scene, fixedDt);
}

void ScriptEngine::Update(Scene& scene, float dt, bool isGameWindowFocused) {
    Scripting::LuaScriptSystem::Get().SetInputActive(isGameWindowFocused);
    glm::vec3 moveDir(0.0f);
    bool jump = false;
    bool run = false;
    bool interact = false;
    bool attack = false;
    bool secondary = false;

    if (isGameWindowFocused) {
        auto& input = Core::InputManager::Get();

        // Locomotion based on developer-configured bindings
        if (input.IsActionDown("MoveForward"))   moveDir.z -= 1.0f;
        if (input.IsActionDown("MoveBackward"))  moveDir.z += 1.0f;
        if (input.IsActionDown("MoveLeft"))      moveDir.x -= 1.0f;
        if (input.IsActionDown("MoveRight"))     moveDir.x += 1.0f;

        if (glm::length(moveDir) > 0.01f) {
            moveDir = glm::normalize(moveDir);
        }

        jump = input.IsActionPressed("Jump");
        run = input.IsActionDown("Run");
        interact = input.IsActionPressed("Interact");
        attack = input.IsActionPressed("Attack");
        secondary = input.IsActionPressed("SecondaryAction");
    }

    // Fixed update accumulator
    m_fixedTimeAccumulator += dt;
    float fixedDt = 1.0f / 60.0f;
    while (m_fixedTimeAccumulator >= fixedDt) {
        FixedUpdate(scene, fixedDt);
        m_fixedTimeAccumulator -= fixedDt;
    }

    m_playerController.OnUpdate(scene, dt, moveDir, jump, run, interact, attack, secondary);

    // Update Character NPCs (Patrol, Steering, Animations, Raycast Clamping)
    for (const auto& node : scene.GetNodes()) {
        if (node->nodeType == NodeType::Character) {
            auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
            if (charNode) {
                charNode->Update(dt, scene);
            }
        }
    }

    // Run active Lua scripts attached to scene nodes
    for (const auto& node : scene.GetNodes()) {
        if (!node->scriptPath.empty()) {
            Scripting::LuaScriptSystem::Get().UpdateNodeScript(*node, scene, dt);
        }
    }
}

void ScriptEngine::RenderHUD(Scene& scene) {
    // Game UI is entirely defined by 2D UI nodes in the scene and game scripts,
    // not hardcoded C++ engine overlays.
}

} // namespace Gameplay
