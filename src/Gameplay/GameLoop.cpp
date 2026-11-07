#include "GameLoop.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include "Scene/Nodes/3D/AudioNode.h"
#include "Audio/AudioEngine.h"
#include "Scripting/LuaScriptSystem.h"
#include "Monitoring/GameProfiler.h"
#include "Scene/OceanWaves.h"
#include "Video/VideoPlayer.h"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <map>

namespace Gameplay {

GameLoop::GameLoop() = default;

void GameLoop::Play(Scene& scene) {
    if (m_mode == EngineMode::Edit) {

        std::cout << "[GameLoop] Starting simulation (Play Mode)... Initializing isolated play scene." << std::endl;
        if (m_cutscenePlayer.IsPlaying()) m_cutscenePlayer.Stop(scene, false);
        m_playScene = scene.Clone();
        m_mode = EngineMode::Play;
        Time::Reset();

        m_pointerLocked = false;
        m_cameraVerticalVelocity = 0.0f;
        m_isCameraGrounded = true;
        if (auto cam = m_playScene->GetActiveCamera()) {
            m_cameraGroundY = (cam->position.y > 0.1f) ? cam->position.y : 1.6f;
        } else {
            m_cameraGroundY = 1.6f;
        }

        m_scriptEngine.InitForPlay(*m_playScene);
        m_animPlayer.Play();
        m_cutscenePlayer.ResetTriggers(*m_playScene);
        Scripting::LuaScriptSystem::Get().SetCutscenePlayer(&m_cutscenePlayer);
        m_animPlayer.Play();

        // Initialize JoltPhysics world for Play Mode
        m_physicsWorld.Shutdown();
        Scripting::LuaScriptSystem::Get().SetPhysicsWorld(nullptr);
        Scripting::LuaScriptSystem::Get().SetCutscenePlayer(nullptr);
        m_physicsWorld.Initialize();
        m_physicsWorld.BuildFromScene(*m_playScene);
        m_physicsWorld.AddTerrainBodies(*m_playScene);
        Scripting::LuaScriptSystem::Get().SetPhysicsWorld(&m_physicsWorld);
        Scripting::LuaScriptSystem::Get().SetCutscenePlayer(&m_cutscenePlayer);
        Scripting::LuaScriptSystem::Get().SetPhysicsWorld(&m_physicsWorld);

        // Apply initial velocities & create Jolt joints/constraints configured on nodes
        for (const auto& node : m_playScene->GetNodes()) {
            if (!node) continue;
            if (node->rigidBodyType == RigidBodyType::Dynamic && m_physicsWorld.HasBodyForNode(node->id)) {
                if (glm::length(node->linearVelocity) > 0.0001f) {
                    m_physicsWorld.SetLinearVelocity(node->id, node->linearVelocity);
                }
                if (glm::length(node->angularVelocity) > 0.0001f) {
                    m_physicsWorld.SetAngularVelocity(node->id, node->angularVelocity);
                }
            }
            if (node->constraintType > 0 && node->constraintTargetNodeId != 0) {
                if (!m_physicsWorld.HasBodyForNode(node->id)) {
                    m_physicsWorld.CreateBodyForNode(*m_playScene, node);
                }
                if (auto targetNode = m_playScene->GetNodeById(node->constraintTargetNodeId)) {
                    if (!m_physicsWorld.HasBodyForNode(targetNode->id)) {
                        m_physicsWorld.CreateBodyForNode(*m_playScene, targetNode);
                    }
                }
                glm::vec3 anchor = node->GetWorldPosition(*m_playScene);
                if (node->constraintType == 1) {
                    m_physicsWorld.AddFixedJoint(node->id, node->constraintTargetNodeId);
                } else if (node->constraintType == 2) {
                    m_physicsWorld.AddHingeJoint(node->id, node->constraintTargetNodeId, anchor);
                } else if (node->constraintType == 3 || node->constraintType == 4) {
                    m_physicsWorld.AddDistanceJoint(node->id, node->constraintTargetNodeId);
                } else {
                    m_physicsWorld.AddSwingTwistJoint(node->id, node->constraintTargetNodeId, anchor);
                }
            }
        }

        // Trigger PlayOnAwake AudioNodes
        glm::vec3 listenerPos(0.0f, 1.5f, 5.0f);
        if (auto cam = m_playScene->GetActiveCamera()) {
            listenerPos = cam->GetWorldPosition(*m_playScene);
        }
        for (const auto& node : m_playScene->GetNodes()) {
            if (!node || node->nodeType != NodeType::AudioSource) continue;
            auto audioNode = std::dynamic_pointer_cast<AudioNode>(node);
            if (audioNode && audioNode->playOnAwake) {
                float lGain = audioNode->volume, rGain = audioNode->volume;
                audioNode->ComputeSpatialGainAndPan(*m_playScene, listenerPos, glm::vec3(1.0f, 0.0f, 0.0f), lGain, rGain);
                Audio::AudioEngine::Get().TriggerHardwarePlayback(
                    audioNode->audioClipPath.empty() ? "Sine_440Hz" : audioNode->audioClipPath,
                    audioNode->busName, lGain, rGain, audioNode->pitch, audioNode->loop,
                    audioNode->isReverbZone ? audioNode->reverbWetMix : 0.0f,
                    audioNode->reverbRoomSize, audioNode->reverbDamping
                );
                break; // Play primary awake audio source on hardware device
            }
        }
    } else if (m_mode == EngineMode::Pause) {
        Resume();
    }
}

void GameLoop::Pause() {
    if (m_mode == EngineMode::Play) {
        m_mode = EngineMode::Pause;
        m_animPlayer.Pause();
        std::cout << "[GameLoop] Simulation Paused." << std::endl;
    } else if (m_mode == EngineMode::Pause) {
        Resume();
    }
}

void GameLoop::Resume() {
    if (m_mode == EngineMode::Pause) {
        m_mode = EngineMode::Play;
        m_animPlayer.Play();
        std::cout << "[GameLoop] Simulation Resumed." << std::endl;
    }
}

void GameLoop::Stop(Scene& scene) {
    if (m_mode != EngineMode::Edit) {
        std::cout << "[GameLoop] Stopping simulation... Releasing play scene." << std::endl;
        m_mode = EngineMode::Edit;
        Audio::AudioEngine::Get().StopHardwarePlayback();
        m_physicsWorld.Shutdown();
        Scripting::LuaScriptSystem::Get().SetPhysicsWorld(nullptr);
        Scripting::LuaScriptSystem::Get().SetCutscenePlayer(nullptr);
        m_pointerLocked = false;
        m_cameraVerticalVelocity = 0.0f;
        m_isCameraGrounded = true;
        if (m_playScene) {
            m_cutscenePlayer.Stop(*m_playScene, false);
            m_animPlayer.Stop(*m_playScene);
            m_scriptEngine.Stop(*m_playScene);
            m_playScene = nullptr;
        }
        Monitoring::GameProfiler::Get().Reset();
        Monitoring::GameProfiler::Get().SetGameRunning(false);
        std::cout << "[GameLoop] Returned to edit state. Edit scene pristine." << std::endl;
    }
}

void GameLoop::Update(Scene& scene, float rawDeltaTime, bool isGameWindowFocused) {
    Time::deltaTime = rawDeltaTime * Time::timeScale;

    // Also update ParticleNodes in Edit Mode so users can see particles live in the 3D Viewport!
    if (m_mode == EngineMode::Edit) {

        if (m_cutscenePlayer.IsPlaying()) {
            m_cutscenePlayer.Update(scene, rawDeltaTime);
            m_animPlayer.Play();
            m_animPlayer.Update(scene, rawDeltaTime);
            if (ImGui::GetCurrentContext() != nullptr && m_cutscenePlayer.IsSkippable()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                    m_cutscenePlayer.Skip(scene);
                }
            }
        }
        for (const auto& node : scene.GetNodes()) {
            if (node && node->nodeType == NodeType::ParticleEmitter) {
                auto pNode = std::dynamic_pointer_cast<ParticleNode>(node);
                // Also when not emitting: bursts and the last particles still move and fade
                if (pNode) {
                    pNode->Update(rawDeltaTime, scene);
                }
            }
        }
        Video::VideoSystem::Get().Update(scene, rawDeltaTime, false);
        if (ImGui::GetCurrentContext() != nullptr && Video::VideoSystem::Get().IsCutawayPlaying()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                Video::VideoSystem::Get().SkipCutaway();
            }
        }
        if (false) {
            if (false) {
                if (false) {
                }
            }
        }
    }

    if (m_mode == EngineMode::Play && m_playScene) {
        Time::time += Time::deltaTime;
        Time::frameCount++;

        // Step JoltPhysics simulation and sync updated transforms back to playScene nodes
        Monitoring::GameProfiler::Get().BeginStage("Physics & Collisions (Jolt)");
        // Floating bodies follow the ocean surface under them
        for (const auto& node : m_playScene->GetNodes()) {
            if (!node || !node->floatsOnWater || node->rigidBodyType != RigidBodyType::Dynamic) continue;
            if (!m_physicsWorld.HasBodyForNode(node->id)) continue;
            const glm::vec3 position = m_physicsWorld.GetPosition(node->id);
            auto ocean = OceanWaves::FindOceanAt(*m_playScene, position.x, position.z);
            if (!ocean) continue;
            const auto surface = OceanWaves::SampleSurface(*ocean, *m_playScene, position.x, position.z, OceanWaves::Time());
            m_physicsWorld.ApplyBuoyancy(node->id, glm::vec3(position.x, surface.height, position.z), surface.normal,
                                         node->buoyancy, surface.velocity, Time::deltaTime);
        }
        m_physicsWorld.Step(*m_playScene, Time::deltaTime);
        // Bodies that started touching: each side's script hears about the other
        for (const auto& [a, b] : m_physicsWorld.TakeNewContacts()) {
            auto nodeA = m_playScene->GetNodeById(a);
            auto nodeB = m_playScene->GetNodeById(b);
            if (nodeA && !nodeA->scriptPath.empty()) Scripting::LuaScriptSystem::Get().CollisionNodeScript(*nodeA, *m_playScene, b);
            if (nodeB && !nodeB->scriptPath.empty()) Scripting::LuaScriptSystem::Get().CollisionNodeScript(*nodeB, *m_playScene, a);
        }
        Monitoring::GameProfiler::Get().EndStage("Physics & Collisions (Jolt)");

        // Step Particle Systems in Play Mode
        Monitoring::GameProfiler::Get().BeginStage("VFX & Particle Simulation");
        for (const auto& node : m_playScene->GetNodes()) {
            if (node && node->nodeType == NodeType::ParticleEmitter) {
                auto pNode = std::dynamic_pointer_cast<ParticleNode>(node);
                if (pNode) {
                    pNode->Update(Time::deltaTime, *m_playScene);
                }
            }
        }
        Monitoring::GameProfiler::Get().EndStage("VFX & Particle Simulation");

        // Advance animations strictly on the isolated play scene
        Monitoring::GameProfiler::Get().BeginStage("Animation & IK Evaluation");
        m_animPlayer.Update(*m_playScene, Time::deltaTime);
        Monitoring::GameProfiler::Get().EndStage("Animation & IK Evaluation");

        // Update scripts, physics fixed update, character movement strictly on the isolated play scene
        Monitoring::GameProfiler::Get().BeginStage("Game Scripts & Logic (Lua)");
        m_scriptEngine.Update(*m_playScene, Time::deltaTime, isGameWindowFocused && !m_cutscenePlayer.IsPlaying());
        Monitoring::GameProfiler::Get().EndStage("Game Scripts & Logic (Lua)");

        Video::VideoSystem::Get().Update(*m_playScene, Time::deltaTime, true);
        if (ImGui::GetCurrentContext() != nullptr && isGameWindowFocused && Video::VideoSystem::Get().IsCutawayPlaying()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                Video::VideoSystem::Get().SkipCutaway();
            }
        }
        // Check proximity/location cutscene triggers and advance active cinematic cutscene
        m_cutscenePlayer.CheckLocationTriggers(*m_playScene);
        if (m_cutscenePlayer.IsPlaying()) {
            m_cutscenePlayer.Update(*m_playScene, Time::deltaTime);
            if (ImGui::GetCurrentContext() != nullptr && isGameWindowFocused && m_cutscenePlayer.IsSkippable()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                    m_cutscenePlayer.Skip(*m_playScene);
                }
            }
        }
        if (false) {
            Monitoring::GameProfiler::Get().BeginStage("Game Scripts & Logic (Lua)");
        }
        Monitoring::GameProfiler::Get().EndStage("Game Scripts & Logic (Lua)");

        // Update 3D spatial audio listener from the active Play Scene camera
        if (auto cam = m_playScene->GetActiveCamera()) {
            glm::mat4 camWorld = cam->GetWorldTransformMatrix(*m_playScene);
            glm::vec3 camPos = glm::vec3(camWorld[3]);
            glm::vec3 camFwd = glm::length(glm::vec3(camWorld[2])) > 0.0001f ? -glm::normalize(glm::vec3(camWorld[2])) : glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 camUp = glm::length(glm::vec3(camWorld[1])) > 0.0001f ? glm::normalize(glm::vec3(camWorld[1])) : glm::vec3(0.0f, 1.0f, 0.0f);
            Audio::AudioEngine::Get().SetListener(camPos, camFwd, camUp);
        }

        // ------------------------------------------------------------------------
        // Built-in Player / Spectator Controller (WASD + Mouse Look)
        // Ensures identical out-of-the-box controls across Web, Standalone, and Editor Player
        // ------------------------------------------------------------------------
        if (!m_cutscenePlayer.IsPlaying() && m_cameraControllerEnabled && ImGui::GetCurrentContext() != nullptr && isGameWindowFocused) {
            auto cam = m_playScene->GetActiveCamera();
            if (!cam) {
                for (const auto& node : m_playScene->GetNodes()) {
                    if (node && node->nodeType == NodeType::Camera) {
                        cam = node;
                        break;
                    }
                }
            }
            if (!cam) {
                cam = m_playScene->AddCameraNode("MainCamera");
                cam->position = glm::vec3(0.0f, 2.0f, 5.0f);
            }

            // If camera has a custom user script attached, user's script takes full control
            if (cam && cam->scriptPath.empty()) {
                ImGuiIO& io = ImGui::GetIO();

                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_pointerLocked = false;
                }

                bool isLooking = false;
                if (m_isStandaloneMode) {
                    isLooking = m_pointerLocked || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left);
                } else {
                    isLooking = m_pointerLocked || ImGui::IsMouseDown(ImGuiMouseButton_Right);
                }

                if (isLooking) {
                    float sens = 0.18f; // ~0.0022 rad/px in degrees, matches WebGL runtime
                    if (std::abs(io.MouseDelta.x) < 200.0f && std::abs(io.MouseDelta.y) < 200.0f) {
                        cam->rotation.y -= io.MouseDelta.x * sens;
                        cam->rotation.x = std::clamp(cam->rotation.x - io.MouseDelta.y * sens, -80.0f, 80.0f);
                        cam->rotation.z = 0.0f;
                    }
                }

                // WASD Navigation
                bool isShift = io.KeyShift || ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
                float speed = isShift ? 10.0f : 5.0f;

                float yawRad = glm::radians(cam->rotation.y);
                glm::vec3 forward(-std::sin(yawRad), 0.0f, -std::cos(yawRad));
                glm::vec3 right(std::cos(yawRad), 0.0f, -std::sin(yawRad));

                glm::vec3 moveDir(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) moveDir += forward;
                if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) moveDir -= forward;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) moveDir += right;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) moveDir -= right;

                float len = glm::length(moveDir);
                if (len > 0.001f) {
                    moveDir = glm::normalize(moveDir);
                    cam->position += moveDir * (speed * Time::deltaTime);
                }

                // Jump & Gravity
                if (ImGui::IsKeyPressed(ImGuiKey_Space) && m_isCameraGrounded) {
                    m_cameraVerticalVelocity = 6.0f;
                    m_isCameraGrounded = false;
                }

                if (!m_isCameraGrounded) {
                    m_cameraVerticalVelocity -= 18.0f * Time::deltaTime;
                    cam->position.y += m_cameraVerticalVelocity * Time::deltaTime;
                    if (cam->position.y <= m_cameraGroundY) {
                        cam->position.y = m_cameraGroundY;
                        m_cameraVerticalVelocity = 0.0f;
                        m_isCameraGrounded = true;
                    }
                }
            }
        }
    }
}

void GameLoop::RenderHUD(Scene& scene) {
    if (m_mode != EngineMode::Edit && m_playScene) {
        m_scriptEngine.RenderHUD(*m_playScene);
    }
}

} // namespace Gameplay
