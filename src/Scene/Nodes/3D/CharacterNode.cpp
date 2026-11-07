#include "CharacterNode.h"
#include "Scene/Scene.h"
#include "Physics/Collision.h"
#include "UI/NodeInspector.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

CharacterNode::CharacterNode(const std::string& nodeName, uint32_t pId)
    : MeshNode(ShapeType::Cube, nodeName.empty() ? "Character NPC" : nodeName, pId)
{
    nodeType = NodeType::Character;
    name = nodeName.empty() ? "Character NPC" : nodeName;
    characterName = name;
    color = glm::vec4(0.30f, 0.70f, 0.95f, 1.0f);
    scale = glm::vec3(1.0f, 1.8f, 1.0f); // Humanoid proportion default
}

std::shared_ptr<Node> CharacterNode::Clone() const {
    return std::make_shared<CharacterNode>(*this);
}

void CharacterNode::Update(float dt, const Scene& scene) {
    if (dt <= 0.0f) return;

    if (isPatrolling && !waypoints.empty()) {
        if (currentWaypointIndex < 0 || currentWaypointIndex >= static_cast<int>(waypoints.size())) {
            currentWaypointIndex = 0;
        }

        glm::vec3 target = waypoints[currentWaypointIndex];
        glm::vec3 diff = target - position;
        glm::vec3 planarDiff(diff.x, 0.0f, diff.z);
        float dist = glm::length(planarDiff);

        if (dist <= waypointTolerance) {
            // Reached waypoint: advance to next or loop
            if (currentWaypointIndex + 1 < static_cast<int>(waypoints.size())) {
                currentWaypointIndex++;
            } else if (loopWaypoints) {
                currentWaypointIndex = 0;
            } else {
                isPatrolling = false;
                currentAnimState = idleAnimClip;
            }
        } else {
            // Steer yaw smoothly towards destination
            glm::vec3 dir = planarDiff / dist;
            float targetYaw = glm::degrees(atan2f(dir.x, dir.z));
            float diffAngle = targetYaw - rotation.y;
            while (diffAngle < -180.0f) diffAngle += 360.0f;
            while (diffAngle > 180.0f) diffAngle -= 360.0f;
            rotation.y += diffAngle * std::clamp(turnSpeed * dt, 0.0f, 1.0f);

            // Move forward
            position += dir * moveSpeed * dt;
            currentAnimState = walkAnimClip;
        }
    } else {
        currentAnimState = idleAnimClip;
    }

    if (applyGroundClamping) {
        float halfHeight = (type == ShapeType::Custom && boundsMin.y < -0.001f)
                           ? (-boundsMin.y * scale.y)
                           : (scale.y * 0.5f);
        float effectiveOffset = groundOffset > 0.0001f ? groundOffset : halfHeight;

        float rayStart = position.y + halfHeight + 0.5f;
        glm::vec3 rayOrigin(position.x, rayStart, position.z);
        float groundY = -999999.0f;
        float hitY = 0.0f;
        auto hit = Physics::RaycastDown(scene, rayOrigin, 1000.0f, id, &hitY);
        if (hit.hit) {
            groundY = hitY;
        } else if (clampToWorldFloor) {
            groundY = 0.0f;
        }

        if (groundY > -900000.0f) {
            float targetY = groundY + effectiveOffset;

            if (position.y > targetY + 0.001f) {
                verticalVelocity -= 9.81f * dt;
                position.y += verticalVelocity * dt;

                if (position.y <= targetY) {
                    position.y = targetY;
                    verticalVelocity = 0.0f;
                    isGrounded = true;
                } else {
                    isGrounded = false;
                }
            } else {
                position.y = targetY;
                verticalVelocity = 0.0f;
                isGrounded = true;
            }
        } else {
            verticalVelocity -= 9.81f * dt;
            position.y += verticalVelocity * dt;
            isGrounded = false;
        }
    }
}

void CharacterNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    MeshNode::RenderInspectorProperties(scene, state);

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Character NPC & Dialogue Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        char nameBuf[128];
        strncpy(nameBuf, characterName.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::Text("Character Name:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##CharacterName", nameBuf, sizeof(nameBuf))) {
            characterName = nameBuf;
        }

        char diagBuf[512];
        strncpy(diagBuf, dialogueText.c_str(), sizeof(diagBuf));
        diagBuf[sizeof(diagBuf) - 1] = '\0';
        ImGui::Text("Dialogue Prompt:");
        if (ImGui::InputTextMultiline("##DialoguePrompt", diagBuf, sizeof(diagBuf), ImVec2(-1.0f, 60))) {
            dialogueText = diagBuf;
        }

        ImGui::Text("Interaction Radius:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##InteractionRadius", &interactionRadius, 0.5f, 20.0f, "%.1f m");

        ImGui::Text("Patrol Move Speed:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##MoveSpeed", &moveSpeed, 0.0f, 20.0f, "%.1f m/s");

        ImGui::Text("Turn Speed (Yaw):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##TurnSpeed", &turnSpeed, 0.5f, 20.0f, "%.1f");

        ImGui::Text("Waypoint Reach Dist:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##WaypointTolerance", &waypointTolerance, 0.1f, 5.0f, "%.2f m");

        ImGui::Checkbox("Is Patrolling", &isPatrolling);
        ImGui::SameLine();
        ImGui::Checkbox("Loop Waypoints", &loopWaypoints);

        ImGui::Checkbox("Ground Raycast Clamping", &applyGroundClamping);
        if (applyGroundClamping) {
            ImGui::Checkbox("Clamp to World Floor (Y=0)", &clampToWorldFloor);
            ImGui::Text("Ground Height Offset:");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##GroundOffset", &groundOffset, 0.05f, -5.0f, 5.0f);
            if (std::abs(groundOffset) < 0.001f) {
                float autoFeet = (type == ShapeType::Custom && boundsMin.y < -0.001f) ? (-boundsMin.y * scale.y) : (scale.y * 0.5f);
                ImGui::TextDisabled("Auto feet contact: %.2f m", autoFeet);
            }
        }

        char idleBuf[64];
        strncpy(idleBuf, idleAnimClip.c_str(), sizeof(idleBuf));
        idleBuf[sizeof(idleBuf) - 1] = '\0';
        ImGui::Text("Idle Anim Clip:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##IdleAnimClip", idleBuf, sizeof(idleBuf))) {
            idleAnimClip = idleBuf;
        }

        char walkBuf[64];
        strncpy(walkBuf, walkAnimClip.c_str(), sizeof(walkBuf));
        walkBuf[sizeof(walkBuf) - 1] = '\0';
        ImGui::Text("Walk Anim Clip:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##WalkAnimClip", walkBuf, sizeof(walkBuf))) {
            walkAnimClip = walkBuf;
        }

        if (!animClipNames.empty()) {
            if (ImGui::BeginCombo("Select Idle Clip", idleAnimClip.c_str())) {
                for (const auto& clip : animClipNames) {
                    bool isSelected = (clip == idleAnimClip);
                    if (ImGui::Selectable(clip.c_str(), isSelected)) {
                        idleAnimClip = clip;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Select Walk Clip", walkAnimClip.c_str())) {
                for (const auto& clip : animClipNames) {
                    bool isSelected = (clip == walkAnimClip);
                    if (ImGui::Selectable(clip.c_str(), isSelected)) {
                        walkAnimClip = clip;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Patrol Waypoints (%zu):", waypoints.size());

        if (ImGui::Button("Add Current Position as Waypoint")) {
            waypoints.push_back(position);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All##WP")) {
            waypoints.clear();
            currentWaypointIndex = 0;
        }

        for (size_t i = 0; i < waypoints.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            bool isCurrent = (static_cast<int>(i) == currentWaypointIndex);
            if (isCurrent) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "-> [%zu]", i);
            } else {
                ImGui::Text("   [%zu]", i);
            }
            ImGui::SameLine();
            ImGui::DragFloat3("##Pos", glm::value_ptr(waypoints[i]), 0.1f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X##Del")) {
                waypoints.erase(waypoints.begin() + i);
                if (currentWaypointIndex >= static_cast<int>(waypoints.size())) {
                    currentWaypointIndex = 0;
                }
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
}

