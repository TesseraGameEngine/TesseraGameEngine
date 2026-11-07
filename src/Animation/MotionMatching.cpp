#include "MotionMatching.h"
#include "IKSolver.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Animation {

void InertializerState::Update(float dt) {
    if (!active) return;

    elapsed += dt;
    if (elapsed >= transitionDuration) {
        active = false;
        posOffset = glm::vec3(0.0f);
        velOffset = glm::vec3(0.0f);
        rotOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rotVelOffset = glm::vec3(0.0f);
        return;
    }

    // Quintic decay curve: s(t) = (1 - t)^3 * (1 + 3t + 6t^2)
    float t = elapsed / transitionDuration;
    float decay = (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f + 3.0f * t + 6.0f * t * t);

    posOffset *= decay;
    rotOffset = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), rotOffset, decay);
}

glm::vec3 InertializerState::ApplyPositionOffset(const glm::vec3& inPos) const {
    if (!active) return inPos;
    return inPos + posOffset;
}

glm::quat InertializerState::ApplyRotationOffset(const glm::quat& inRot) const {
    if (!active) return inRot;
    return rotOffset * inRot;
}

void MotionDatabase::Clear() {
    m_frames.clear();
}

void MotionDatabase::AddFrame(const MotionMatchingFrame& frame) {
    m_frames.push_back(frame);
}

void MotionDatabase::BuildDefaultLocomotionDatabase() {
    m_frames.clear();

    // 1. Idle (Stationary pose)
    for (int i = 0; i < 10; ++i) {
        MotionMatchingFrame f;
        f.clipId = 0;
        f.clipName = "Idle";
        f.timestamp = i * 0.1f;
        f.trajectory[0] = { 0.2f, glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[1] = { 0.4f, glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[2] = { 0.6f, glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 1.0f) };
        f.featurePose.rootVelocity = glm::vec3(0.0f);
        f.featurePose.leftFootPos = glm::vec3(-0.15f, 0.0f, 0.0f);
        f.featurePose.rightFootPos = glm::vec3(0.15f, 0.0f, 0.0f);
        f.featurePose.leftFootVel = glm::vec3(0.0f);
        f.featurePose.rightFootVel = glm::vec3(0.0f);
        f.featurePose.hipsPos = glm::vec3(0.0f, 0.95f, 0.0f);
        m_frames.push_back(f);
    }

    // 2. Walk Forward (1.4 m/s)
    for (int i = 0; i < 20; ++i) {
        float t = i * 0.05f;
        float cycle = t * 2.0f * 3.14159f;
        MotionMatchingFrame f;
        f.clipId = 1;
        f.clipName = "WalkForward";
        f.timestamp = t;
        f.trajectory[0] = { 0.2f, glm::vec2(0.0f, 1.4f * 0.2f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[1] = { 0.4f, glm::vec2(0.0f, 1.4f * 0.4f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[2] = { 0.6f, glm::vec2(0.0f, 1.4f * 0.6f), glm::vec2(0.0f, 1.0f) };
        f.featurePose.rootVelocity = glm::vec3(0.0f, 0.0f, 1.4f);
        f.featurePose.leftFootPos = glm::vec3(-0.15f, std::max(0.0f, std::sin(cycle) * 0.15f), std::cos(cycle) * 0.4f);
        f.featurePose.rightFootPos = glm::vec3(0.15f, std::max(0.0f, -std::sin(cycle) * 0.15f), -std::cos(cycle) * 0.4f);
        f.featurePose.leftFootVel = glm::vec3(0.0f, 0.0f, -std::sin(cycle) * 1.4f);
        f.featurePose.rightFootVel = glm::vec3(0.0f, 0.0f, std::sin(cycle) * 1.4f);
        f.featurePose.hipsPos = glm::vec3(0.0f, 0.93f, 0.0f);
        m_frames.push_back(f);
    }

    // 3. Run Forward (4.2 m/s)
    for (int i = 0; i < 20; ++i) {
        float t = i * 0.04f;
        float cycle = t * 2.5f * 3.14159f;
        MotionMatchingFrame f;
        f.clipId = 2;
        f.clipName = "RunForward";
        f.timestamp = t;
        f.trajectory[0] = { 0.2f, glm::vec2(0.0f, 4.2f * 0.2f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[1] = { 0.4f, glm::vec2(0.0f, 4.2f * 0.4f), glm::vec2(0.0f, 1.0f) };
        f.trajectory[2] = { 0.6f, glm::vec2(0.0f, 4.2f * 0.6f), glm::vec2(0.0f, 1.0f) };
        f.featurePose.rootVelocity = glm::vec3(0.0f, 0.0f, 4.2f);
        f.featurePose.leftFootPos = glm::vec3(-0.18f, std::max(0.0f, std::sin(cycle) * 0.35f), std::cos(cycle) * 0.75f);
        f.featurePose.rightFootPos = glm::vec3(0.18f, std::max(0.0f, -std::sin(cycle) * 0.35f), -std::cos(cycle) * 0.75f);
        f.featurePose.leftFootVel = glm::vec3(0.0f, 0.0f, -std::sin(cycle) * 4.2f);
        f.featurePose.rightFootVel = glm::vec3(0.0f, 0.0f, std::sin(cycle) * 4.2f);
        f.featurePose.hipsPos = glm::vec3(0.0f, 0.88f, 0.0f);
        m_frames.push_back(f);
    }

    // 4. Sharp Left Turn 90 Degrees
    for (int i = 0; i < 15; ++i) {
        float t = i * 0.05f;
        MotionMatchingFrame f;
        f.clipId = 3;
        f.clipName = "TurnLeft90";
        f.timestamp = t;
        f.trajectory[0] = { 0.2f, glm::vec2(-0.8f * 0.2f, 0.8f * 0.2f), glm::normalize(glm::vec2(-0.7f, 0.7f)) };
        f.trajectory[1] = { 0.4f, glm::vec2(-1.5f * 0.4f, 0.5f * 0.4f), glm::normalize(glm::vec2(-1.0f, 0.0f)) };
        f.trajectory[2] = { 0.6f, glm::vec2(-2.0f * 0.6f, 0.0f), glm::normalize(glm::vec2(-1.0f, 0.0f)) };
        f.featurePose.rootVelocity = glm::vec3(-1.8f, 0.0f, 0.9f);
        f.featurePose.leftFootPos = glm::vec3(-0.3f, 0.0f, -0.2f);
        f.featurePose.rightFootPos = glm::vec3(0.2f, 0.1f, 0.3f);
        f.featurePose.leftFootVel = glm::vec3(-1.5f, 0.0f, 0.0f);
        f.featurePose.rightFootVel = glm::vec3(-0.5f, 0.0f, 0.8f);
        f.featurePose.hipsPos = glm::vec3(0.0f, 0.89f, 0.0f);
        m_frames.push_back(f);
    }
}

MotionMatcher& MotionMatcher::Get() {
    static MotionMatcher instance;
    return instance;
}

MotionMatcher::MotionMatcher() {
    Initialize();
}

void MotionMatcher::Initialize() {
    m_database = std::make_shared<MotionDatabase>();
    m_database->BuildDefaultLocomotionDatabase();
    m_currentMatchedIndex = 0;
}

int MotionMatcher::QueryBestFrame(const MotionFeaturePose& currentPose,
                                  const TrajectoryPoint desiredTrajectory[3],
                                  const MotionMatchingWeights& weights) const {
    if (!m_database || m_database->GetFrameCount() == 0) return 0;

    float bestCost = std::numeric_limits<float>::max();
    int bestIndex = 0;

    size_t frameCount = m_database->GetFrameCount();
    for (size_t i = 0; i < frameCount; ++i) {
        const auto& candidate = m_database->GetFrame(i);
        float cost = 0.0f;

        // 1. Trajectory Position & Direction Cost
        for (int p = 0; p < 3; ++p) {
            glm::vec2 posDiff = desiredTrajectory[p].position - candidate.trajectory[p].position;
            cost += glm::dot(posDiff, posDiff) * weights.trajectoryPosWeight;

            float dirDot = glm::dot(desiredTrajectory[p].direction, candidate.trajectory[p].direction);
            cost += (1.0f - std::max(-1.0f, std::min(1.0f, dirDot))) * weights.trajectoryDirWeight;
        }

        // 2. Pose Feature Cost (Feet position & velocity alignment)
        glm::vec3 leftFootDiff = currentPose.leftFootPos - candidate.featurePose.leftFootPos;
        glm::vec3 rightFootDiff = currentPose.rightFootPos - candidate.featurePose.rightFootPos;
        cost += glm::dot(leftFootDiff, leftFootDiff) * weights.footPosWeight;
        cost += glm::dot(rightFootDiff, rightFootDiff) * weights.footPosWeight;

        glm::vec3 leftVelDiff = currentPose.leftFootVel - candidate.featurePose.leftFootVel;
        glm::vec3 rightVelDiff = currentPose.rightFootVel - candidate.featurePose.rightFootVel;
        cost += glm::dot(leftVelDiff, leftVelDiff) * weights.footVelWeight;
        cost += glm::dot(rightVelDiff, rightVelDiff) * weights.footVelWeight;

        // 3. Root Velocity Cost
        glm::vec3 rootVelDiff = currentPose.rootVelocity - candidate.featurePose.rootVelocity;
        cost += glm::dot(rootVelDiff, rootVelDiff) * weights.rootVelWeight;

        if (cost < bestCost) {
            bestCost = cost;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

void MotionMatcher::TransitionTo(const MotionMatchingFrame& targetFrame,
                                 const glm::vec3& currentRootPos,
                                 const glm::quat& currentRootRot,
                                 float transitionDuration) {
    (void)targetFrame;
    m_inertializer.active = true;
    m_inertializer.transitionDuration = transitionDuration > 0.01f ? transitionDuration : 0.2f;
    m_inertializer.elapsed = 0.0f;
    m_inertializer.posOffset = currentRootPos;
    m_inertializer.rotOffset = currentRootRot;
}

void MotionMatcher::Update(float dt) {
    m_inertializer.Update(dt);
}

static glm::quat RotationBetweenVectors(glm::vec3 start, glm::vec3 dest) {
    start = glm::normalize(start);
    dest = glm::normalize(dest);
    float cosTheta = glm::dot(start, dest);
    glm::vec3 rotationAxis;
    if (cosTheta < -1.0f + 0.001f) {
        rotationAxis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), start);
        if (glm::dot(rotationAxis, rotationAxis) < 0.01f) {
            rotationAxis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), start);
        }
        rotationAxis = glm::normalize(rotationAxis);
        return glm::angleAxis(glm::radians(180.0f), rotationAxis);
    }
    rotationAxis = glm::cross(start, dest);
    float s = std::sqrt((1.0f + cosTheta) * 2.0f);
    float invs = 1.0f / s;
    return glm::quat(s * 0.5f, rotationAxis.x * invs, rotationAxis.y * invs, rotationAxis.z * invs);
}

bool FullBodyIKSolver::Solve(Skeleton& skeleton,
                             int pelvisBoneIdx,
                             int leftThighIdx, int leftCalfIdx, int leftFootIdx,
                             int rightThighIdx, int rightCalfIdx, int rightFootIdx,
                             int spineBoneIdx, int headBoneIdx,
                             const FullBodyIKTargets& targets,
                             float pelvisMaxDrop) {
    auto& bones = skeleton.bones;
    int boneCount = static_cast<int>(bones.size());
    if (pelvisBoneIdx < 0 || pelvisBoneIdx >= boneCount) return false;

    // 1. Pelvis Adjustment
    if (targets.enablePelvisAdjustment) {
        float minFootHeight = std::min(targets.leftFootGroundHeight, targets.rightFootGroundHeight);
        float pelvisDrop = std::max(-pelvisMaxDrop, std::min(0.0f, minFootHeight));
        bones[pelvisBoneIdx].currentPosition.y += pelvisDrop;
    }

    // 2. Foot Placement via TwoBoneIK
    if (targets.enableFootPlacement) {
        if (leftThighIdx >= 0 && leftCalfIdx >= 0 && leftFootIdx >= 0 &&
            leftThighIdx < boneCount && leftCalfIdx < boneCount && leftFootIdx < boneCount) {
            
            glm::vec3 leftTargetPos = bones[leftFootIdx].currentPosition;
            leftTargetPos.y = targets.leftFootGroundHeight;
            glm::vec3 leftPoleVector(0.0f, 0.0f, 1.0f); // Forward bend
            IKSolver::SolveTwoBoneIK(skeleton, leftThighIdx, leftCalfIdx, leftFootIdx, leftTargetPos, leftPoleVector, 1.0f);

            // Align foot rotation to terrain surface normal
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::quat alignRot = RotationBetweenVectors(up, targets.leftFootNormal);
            bones[leftFootIdx].currentRotation = alignRot * bones[leftFootIdx].currentRotation;
        }

        if (rightThighIdx >= 0 && rightCalfIdx >= 0 && rightFootIdx >= 0 &&
            rightThighIdx < boneCount && rightCalfIdx < boneCount && rightFootIdx < boneCount) {

            glm::vec3 rightTargetPos = bones[rightFootIdx].currentPosition;
            rightTargetPos.y = targets.rightFootGroundHeight;
            glm::vec3 rightPoleVector(0.0f, 0.0f, 1.0f);
            IKSolver::SolveTwoBoneIK(skeleton, rightThighIdx, rightCalfIdx, rightFootIdx, rightTargetPos, rightPoleVector, 1.0f);

            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::quat alignRot = RotationBetweenVectors(up, targets.rightFootNormal);
            bones[rightFootIdx].currentRotation = alignRot * bones[rightFootIdx].currentRotation;
        }
    }

    // 3. Head & Spine Look-At
    if (targets.enableLookAt && targets.lookAtWeight > 0.001f) {
        if (headBoneIdx >= 0 && headBoneIdx < boneCount) {
            glm::vec3 headPos = bones[headBoneIdx].currentPosition;
            glm::vec3 toTarget = glm::normalize(targets.lookAtTargetWorld - headPos);
            glm::vec3 forward(0.0f, 0.0f, 1.0f);
            glm::quat lookRot = RotationBetweenVectors(forward, toTarget);
            bones[headBoneIdx].currentRotation = glm::slerp(bones[headBoneIdx].currentRotation, lookRot, targets.lookAtWeight);
        }
        if (spineBoneIdx >= 0 && spineBoneIdx < boneCount) {
            glm::vec3 spinePos = bones[spineBoneIdx].currentPosition;
            glm::vec3 toTarget = glm::normalize(targets.lookAtTargetWorld - spinePos);
            glm::vec3 forward(0.0f, 0.0f, 1.0f);
            glm::quat lookRot = RotationBetweenVectors(forward, toTarget);
            bones[spineBoneIdx].currentRotation = glm::slerp(bones[spineBoneIdx].currentRotation, lookRot, targets.lookAtWeight * 0.35f);
        }
    }

    return true;
}

} // namespace Animation
