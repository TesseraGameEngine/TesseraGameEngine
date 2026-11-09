#pragma once
#include "MeshNode.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

class CharacterNode : public MeshNode {
public:
    CharacterNode(const std::string& name = "Character Node", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    // Update patrol movement, yaw steering, and ground clamping
    void Update(float dt, const Scene& scene);

    // Waypoint patrol settings
    std::vector<glm::vec3> waypoints;
    int currentWaypointIndex{0};
    float moveSpeed{2.5f};
    float turnSpeed{6.0f};
    float waypointTolerance{0.5f};
    bool isPatrolling{true};
    bool loopWaypoints{true};

    // NPC Interaction & Dialogue
    std::string characterName{"NPC"};
    std::string dialogueText{"Greetings, adventurer!"};
    float interactionRadius{3.5f};
    bool isInteracting{false};

    // Animation states
    std::string idleAnimClip{"Idle"};
    std::string walkAnimClip{"Walk"};

    // Ground raycast clamping & physical contact
    bool applyGroundClamping{true};
    float groundOffset{0.0f}; // 0.0 = automatically computed from half-height for perfect feet contact
    bool clampToWorldFloor{true}; // Stops on world grid (Y=0) if no floor plane is placed
    float verticalVelocity{0.0f};
    bool isGrounded{false};

    // Motion Matching & Full-Body IK Locomotion
    bool useMotionMatching{false};
    glm::vec3 desiredVelocity{0.0f};
    int currentMatchedFrameIndex{0};
    glm::vec3 m_lastPos{0.0f};
};

