#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Animation/Skeleton.h"

namespace Animation {

struct TwoBoneIKResult {
    glm::vec3 rootWorldPos{0.0f};
    glm::vec3 midWorldPos{0.0f};
    glm::vec3 endWorldPos{0.0f};
    glm::quat rootLocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat midLocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool reached{false};
};

class IKSolver {
public:
    // Analytic Two-Bone IK Solver (e.g. Thigh -> Calf -> Foot, or UpperArm -> Forearm -> Hand)
    // Computes exact joint bend angle via Law of Cosines and aligns bend plane with poleVector
    static TwoBoneIKResult SolveTwoBoneIKPositions(const glm::vec3& rootPos,
                                                   const glm::vec3& midPos,
                                                   const glm::vec3& endPos,
                                                   const glm::vec3& targetPos,
                                                   const glm::vec3& poleVector,
                                                   float weight = 1.0f);

    // Applies Analytic Two-Bone IK directly onto three bones in an Animation::Skeleton
    static bool SolveTwoBoneIK(Skeleton& skeleton,
                               int rootBoneIdx,
                               int midBoneIdx,
                               int endBoneIdx,
                               const glm::vec3& targetWorldPos,
                               const glm::vec3& poleVector,
                               float weight = 1.0f);

    // Forward And Backward Reaching Inverse Kinematics (FABRIK) for N-joint chains
    static bool SolveFABRIKPositions(std::vector<glm::vec3>& chainPositions,
                                     const glm::vec3& targetPos,
                                     float tolerance = 0.001f,
                                     int maxIterations = 16,
                                     float weight = 1.0f);

    // Solves FABRIK on a chain of bone indices in a Skeleton and updates their transforms
    static bool SolveFABRIK(Skeleton& skeleton,
                            const std::vector<int>& chainBoneIndices,
                            const glm::vec3& targetWorldPos,
                            float tolerance = 0.001f,
                            int maxIterations = 16,
                            float weight = 1.0f);

    // Helper to compute world position of every bone in a skeleton from current local transforms
    static std::vector<glm::vec3> ComputeBoneWorldPositions(const Skeleton& skeleton);
};

} // namespace Animation

