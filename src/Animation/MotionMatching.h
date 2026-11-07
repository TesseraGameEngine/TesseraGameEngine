#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Animation/Skeleton.h"

namespace Animation {

// 2D Future trajectory sample relative to character root
struct TrajectoryPoint {
    float timeOffset{0.2f};       // Seconds into future (+0.2s, +0.4s, +0.6s)
    glm::vec2 position{0.0f};     // XZ offset relative to root
    glm::vec2 direction{0.0f, 1.0f}; // Facing vector
};

// Key bone features extracted for pose distance matching
struct MotionFeaturePose {
    glm::vec3 rootVelocity{0.0f};
    glm::vec3 leftFootPos{0.0f};
    glm::vec3 leftFootVel{0.0f};
    glm::vec3 rightFootPos{0.0f};
    glm::vec3 rightFootVel{0.0f};
    glm::vec3 hipsPos{0.0f};
};

struct MotionMatchingFrame {
    uint32_t clipId{0};
    std::string clipName{""};
    float timestamp{0.0f};
    TrajectoryPoint trajectory[3]; // +0.2s, +0.4s, +0.6s
    MotionFeaturePose featurePose;
};

struct MotionMatchingWeights {
    float trajectoryPosWeight{1.0f};
    float trajectoryDirWeight{1.5f};
    float footPosWeight{0.75f};
    float footVelWeight{0.5f};
    float rootVelWeight{1.0f};
};

// Inertialization transition state for zero-pop continuous blending
struct InertializerState {
    bool active{false};
    float transitionDuration{0.2f};
    float elapsed{0.0f};
    glm::vec3 posOffset{0.0f};
    glm::vec3 velOffset{0.0f};
    glm::quat rotOffset{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 rotVelOffset{0.0f};

    void Update(float dt);
    glm::vec3 ApplyPositionOffset(const glm::vec3& inPos) const;
    glm::quat ApplyRotationOffset(const glm::quat& inRot) const;
};

class MotionDatabase {
public:
    void Clear();
    void AddFrame(const MotionMatchingFrame& frame);
    size_t GetFrameCount() const { return m_frames.size(); }
    const MotionMatchingFrame& GetFrame(size_t index) const { return m_frames[index]; }

    // Generates a default procedural locomotion database (Idle, Walk, Run, Plant, SharpTurn)
    void BuildDefaultLocomotionDatabase();

private:
    std::vector<MotionMatchingFrame> m_frames;
};

class MotionMatcher {
public:
    static MotionMatcher& Get();

    void Initialize();
    void SetDatabase(std::shared_ptr<MotionDatabase> db) { m_database = db; }
    std::shared_ptr<MotionDatabase> GetDatabase() { return m_database; }

    // Evaluates current pose and user stick input to query best candidate frame
    int QueryBestFrame(const MotionFeaturePose& currentPose,
                       const TrajectoryPoint desiredTrajectory[3],
                       const MotionMatchingWeights& weights = MotionMatchingWeights()) const;

    // Trigger inertialized transition to target frame
    void TransitionTo(const MotionMatchingFrame& targetFrame,
                      const glm::vec3& currentRootPos,
                      const glm::quat& currentRootRot,
                      float transitionDuration = 0.2f);

    void Update(float dt);

    const InertializerState& GetInertializer() const { return m_inertializer; }
    int GetCurrentMatchedIndex() const { return m_currentMatchedIndex; }

private:
    MotionMatcher();
    std::shared_ptr<MotionDatabase> m_database;
    InertializerState m_inertializer;
    int m_currentMatchedIndex{0};
};

// Procedural Full-Body IK (Pelvis height adjustment, terrain foot conform, and look-at)
struct FullBodyIKTargets {
    bool enablePelvisAdjustment{true};
    bool enableFootPlacement{true};
    bool enableLookAt{true};

    float leftFootGroundHeight{0.0f};
    float rightFootGroundHeight{0.0f};
    glm::vec3 leftFootNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 rightFootNormal{0.0f, 1.0f, 0.0f};

    glm::vec3 lookAtTargetWorld{0.0f};
    float lookAtWeight{1.0f};
};

class FullBodyIKSolver {
public:
    static bool Solve(Skeleton& skeleton,
                      int pelvisBoneIdx,
                      int leftThighIdx, int leftCalfIdx, int leftFootIdx,
                      int rightThighIdx, int rightCalfIdx, int rightFootIdx,
                      int spineBoneIdx, int headBoneIdx,
                      const FullBodyIKTargets& targets,
                      float pelvisMaxDrop = 0.5f);
};

} // namespace Animation

