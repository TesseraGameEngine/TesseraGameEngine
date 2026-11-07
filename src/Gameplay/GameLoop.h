#pragma once

#include <memory>
#include "Scene/Scene.h"
#include "GameplayScripts.h"
#include "Animation/Animation.h"
#include "Animation/Cutscene.h"
#include "Physics/PhysicsWorld.h"
#include "Time.h"

namespace Gameplay {

enum class EngineMode {
    Edit,
    Play,
    Pause
};

class GameLoop {
public:
    GameLoop();
    ~GameLoop() = default;

    EngineMode GetMode() const { return m_mode; }
    bool IsPlaying() const { return m_mode == EngineMode::Play; }
    bool IsPaused() const { return m_mode == EngineMode::Pause; }
    bool IsEditing() const { return m_mode == EngineMode::Edit; }

    void Play(Scene& scene);
    void Pause();
    void Resume();
    void Stop(Scene& scene);

    void Update(Scene& scene, float rawDeltaTime, bool isGameWindowFocused = true);

    Scene& GetActivePlayScene(Scene& fallbackEditScene) {
        if (m_mode != EngineMode::Edit && m_playScene) {
            return *m_playScene;
        }
        return fallbackEditScene;
    }

    std::shared_ptr<Scene> GetPlayScene() const { return m_playScene; }
    void RenderHUD(Scene& scene);

    ScriptEngine& GetScriptEngine() { return m_scriptEngine; }
    Animation::AnimationPlayer& GetAnimationPlayer() { return m_animPlayer; }
    Animation::CutscenePlayer& GetCutscenePlayer() { return m_cutscenePlayer; }
    const Animation::CutscenePlayer& GetCutscenePlayer() const { return m_cutscenePlayer; }
    Physics::PhysicsWorld& GetPhysicsWorld() { return m_physicsWorld; }

    bool IsPointerLocked() const { return m_pointerLocked; }
    void SetPointerLocked(bool locked) { m_pointerLocked = locked; }
    bool IsStandaloneMode() const { return m_isStandaloneMode; }
    void SetStandaloneMode(bool standalone) { m_isStandaloneMode = standalone; }

    bool IsDefaultCameraControllerEnabled() const { return m_cameraControllerEnabled; }
    void SetDefaultCameraControllerEnabled(bool enabled) { m_cameraControllerEnabled = enabled; }

private:
    EngineMode m_mode{EngineMode::Edit};
    std::shared_ptr<Scene> m_playScene{nullptr};

    ScriptEngine m_scriptEngine;
    Animation::AnimationPlayer m_animPlayer;
    Animation::CutscenePlayer m_cutscenePlayer;
    Physics::PhysicsWorld m_physicsWorld;

    // Built-in spectator / player controller state (matching Web export)
    bool m_cameraControllerEnabled{false}; // Default is false: brand new games start with no default controls; all controls defined by dev
    bool m_isStandaloneMode{false};
    bool m_pointerLocked{false};
    float m_cameraVerticalVelocity{0.0f};
    bool m_isCameraGrounded{true};
    float m_cameraGroundY{1.6f};
};

} // namespace Gameplay
