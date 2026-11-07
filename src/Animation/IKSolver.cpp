#include "IKSolver.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Animation {

namespace {
glm::quat RotationBetweenVectors(const glm::vec3& from, const glm::vec3& to) {
    glm::vec3 u = glm::normalize(from);
    glm::vec3 v = glm::normalize(to);
    float cosTheta = glm::dot(u, v);
    if (cosTheta >= 0.9999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (cosTheta <= -0.9999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), u);
        if (glm::length(axis) < 1e-4f) {
            axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), u);
        }
        axis = glm::normalize(axis);
        return glm::angleAxis(3.14159265359f, axis);
    }
    glm::vec3 axis = glm::cross(u, v);
    float s = std::sqrt((1.0f + cosTheta) * 2.0f);
    float invs = 1.0f / s;
    return glm::normalize(glm::quat(s * 0.5f, axis.x * invs, axis.y * invs, axis.z * invs));
}
} // namespace

std::vector<glm::vec3> IKSolver::ComputeBoneWorldPositions(const Skeleton& skeleton) {
    size_t count = skeleton.bones.size();
    std::vector<glm::mat4> worldMats(count, glm::mat4(1.0f));
    std::vector<glm::vec3> worldPos(count, glm::vec3(0.0f));

    for (size_t i = 0; i < count; ++i) {
        const auto& bone = skeleton.bones[i];
        glm::mat4 T = glm::translate(glm::mat4(1.0f), bone.currentPosition);
        glm::mat4 R = glm::mat4_cast(bone.currentRotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), bone.currentScale);
        glm::mat4 local = T * R * S;

        if (bone.parentId >= 0 && bone.parentId < static_cast<int>(i)) {
            worldMats[i] = worldMats[bone.parentId] * local;
        } else {
            worldMats[i] = local;
        }
        worldPos[i] = glm::vec3(worldMats[i][3]);
    }
    return worldPos;
}

TwoBoneIKResult IKSolver::SolveTwoBoneIKPositions(const glm::vec3& rootPos,
                                                  const glm::vec3& midPos,
                                                  const glm::vec3& endPos,
                                                  const glm::vec3& targetPos,
                                                  const glm::vec3& poleVector,
                                                  float weight) {
    TwoBoneIKResult res;
    res.rootWorldPos = rootPos;

    float lenA = glm::length(midPos - rootPos);
    float lenB = glm::length(endPos - midPos);
    if (lenA < 1e-5f || lenB < 1e-5f) {
        res.midWorldPos = midPos;
        res.endWorldPos = endPos;
        return res;
    }

    glm::vec3 toTarget = targetPos - rootPos;
    float distToTarget = glm::length(toTarget);
    float maxReach = lenA + lenB - 1e-4f;
    float minReach = std::abs(lenA - lenB) + 1e-4f;

    res.reached = (distToTarget <= maxReach);
    float clampedDist = std::clamp(distToTarget, minReach, maxReach);

    glm::vec3 dirToTarget = (distToTarget > 1e-5f)
        ? (toTarget / distToTarget)
        : glm::normalize(endPos - rootPos);

    // Law of Cosines for angle at root (alpha)
    float cosAlpha = std::clamp(
        (lenA * lenA + clampedDist * clampedDist - lenB * lenB) / (2.0f * lenA * clampedDist),
        -1.0f, 1.0f);
    float sinAlpha = std::sqrt(std::max(0.0f, 1.0f - cosAlpha * cosAlpha));

    // Determine bend plane using poleVector
    glm::vec3 toPole = poleVector - rootPos;
    glm::vec3 bendDir = toPole - dirToTarget * glm::dot(toPole, dirToTarget);
    if (glm::length(bendDir) < 1e-5f) {
        bendDir = (midPos - rootPos) - dirToTarget * glm::dot(midPos - rootPos, dirToTarget);
        if (glm::length(bendDir) < 1e-5f) {
            bendDir = (std::abs(dirToTarget.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            bendDir = glm::normalize(bendDir - dirToTarget * glm::dot(bendDir, dirToTarget));
        } else {
            bendDir = glm::normalize(bendDir);
        }
    } else {
        bendDir = glm::normalize(bendDir);
    }

    glm::vec3 solvedMid = rootPos + dirToTarget * (lenA * cosAlpha) + bendDir * (lenA * sinAlpha);
    glm::vec3 solvedEnd = rootPos + dirToTarget * clampedDist;

    float w = glm::clamp(weight, 0.0f, 1.0f);
    res.midWorldPos = glm::mix(midPos, solvedMid, w);
    res.endWorldPos = glm::mix(endPos, solvedEnd, w);

    glm::vec3 origUpper = midPos - rootPos;
    glm::vec3 newUpper = res.midWorldPos - rootPos;
    res.rootLocalRotation = RotationBetweenVectors(origUpper, newUpper);

    glm::vec3 origLower = endPos - midPos;
    glm::vec3 newLower = res.endWorldPos - res.midWorldPos;
    res.midLocalRotation = RotationBetweenVectors(origLower, newLower);

    return res;
}

bool IKSolver::SolveTwoBoneIK(Skeleton& skeleton,
                              int rootBoneIdx,
                              int midBoneIdx,
                              int endBoneIdx,
                              const glm::vec3& targetWorldPos,
                              const glm::vec3& poleVector,
                              float weight) {
    int count = static_cast<int>(skeleton.bones.size());
    if (rootBoneIdx < 0 || rootBoneIdx >= count ||
        midBoneIdx < 0 || midBoneIdx >= count ||
        endBoneIdx < 0 || endBoneIdx >= count) {
        return false;
    }

    auto worldPositions = ComputeBoneWorldPositions(skeleton);
    TwoBoneIKResult res = SolveTwoBoneIKPositions(
        worldPositions[rootBoneIdx],
        worldPositions[midBoneIdx],
        worldPositions[endBoneIdx],
        targetWorldPos,
        poleVector,
        weight
    );

    glm::quat oldRootRot = skeleton.bones[rootBoneIdx].currentRotation;
    glm::quat oldMidRot = skeleton.bones[midBoneIdx].currentRotation;

    glm::quat newRootRot = glm::normalize(res.rootLocalRotation * oldRootRot);
    glm::quat newMidRot = glm::normalize(glm::inverse(newRootRot) * res.midLocalRotation * oldRootRot * oldMidRot);

    skeleton.bones[rootBoneIdx].currentRotation = newRootRot;
    skeleton.bones[midBoneIdx].currentRotation = newMidRot;

    return true;
}

bool IKSolver::SolveFABRIKPositions(std::vector<glm::vec3>& chainPositions,
                                    const glm::vec3& targetPos,
                                    float tolerance,
                                    int maxIterations,
                                    float weight) {
    if (chainPositions.size() < 2) return false;

    size_t n = chainPositions.size();
    std::vector<glm::vec3> origPositions = chainPositions;
    std::vector<float> lengths(n - 1, 0.0f);
    float totalLength = 0.0f;

    for (size_t i = 0; i < n - 1; ++i) {
        lengths[i] = glm::length(chainPositions[i + 1] - chainPositions[i]);
        totalLength += lengths[i];
    }

    glm::vec3 rootPos = chainPositions[0];
    float distToTarget = glm::length(targetPos - rootPos);

    bool reached = false;
    if (distToTarget >= totalLength) {
        // Target unreachable: stretch chain straight toward target
        glm::vec3 dir = (distToTarget > 1e-5f) ? ((targetPos - rootPos) / distToTarget) : glm::vec3(0.0f, 1.0f, 0.0f);
        for (size_t i = 0; i < n - 1; ++i) {
            chainPositions[i + 1] = chainPositions[i] + dir * lengths[i];
        }
    } else {
        for (int iter = 0; iter < maxIterations; ++iter) {
            if (glm::length(chainPositions[n - 1] - targetPos) <= tolerance) {
                reached = true;
                break;
            }
            // Stage 1: Forward reaching (end-effector to root)
            chainPositions[n - 1] = targetPos;
            for (int i = static_cast<int>(n) - 2; i >= 0; --i) {
                glm::vec3 diff = chainPositions[i] - chainPositions[i + 1];
                float d = glm::length(diff);
                glm::vec3 dir = (d > 1e-5f) ? (diff / d) : glm::vec3(0.0f, 1.0f, 0.0f);
                chainPositions[i] = chainPositions[i + 1] + dir * lengths[i];
            }

            // Stage 2: Backward reaching (root to end-effector)
            chainPositions[0] = rootPos;
            for (size_t i = 0; i < n - 1; ++i) {
                glm::vec3 diff = chainPositions[i + 1] - chainPositions[i];
                float d = glm::length(diff);
                glm::vec3 dir = (d > 1e-5f) ? (diff / d) : glm::vec3(0.0f, 1.0f, 0.0f);
                chainPositions[i + 1] = chainPositions[i] + dir * lengths[i];
            }
        }
        reached = (glm::length(chainPositions[n - 1] - targetPos) <= tolerance * 5.0f);
    }

    float w = glm::clamp(weight, 0.0f, 1.0f);
    if (w < 1.0f) {
        for (size_t i = 0; i < n; ++i) {
            chainPositions[i] = glm::mix(origPositions[i], chainPositions[i], w);
        }
    }
    return reached;
}

bool IKSolver::SolveFABRIK(Skeleton& skeleton,
                           const std::vector<int>& chainBoneIndices,
                           const glm::vec3& targetWorldPos,
                           float tolerance,
                           int maxIterations,
                           float weight) {
    if (chainBoneIndices.size() < 2) return false;
    int boneCount = static_cast<int>(skeleton.bones.size());

    auto worldPositions = ComputeBoneWorldPositions(skeleton);
    std::vector<glm::vec3> chainPos;
    chainPos.reserve(chainBoneIndices.size());

    for (int idx : chainBoneIndices) {
        if (idx < 0 || idx >= boneCount) return false;
        chainPos.push_back(worldPositions[idx]);
    }

    std::vector<glm::vec3> origChainPos = chainPos;
    bool reached = SolveFABRIKPositions(chainPos, targetWorldPos, tolerance, maxIterations, weight);

    for (size_t i = 0; i + 1 < chainBoneIndices.size(); ++i) {
        int bIdx = chainBoneIndices[i];
        int nextIdx = chainBoneIndices[i + 1];
        glm::vec3 oldDir = origChainPos[i + 1] - origChainPos[i];
        glm::vec3 newDir = chainPos[i + 1] - chainPos[i];
        if (glm::length(oldDir) > 1e-5f && glm::length(newDir) > 1e-5f) {
            glm::quat deltaRot = RotationBetweenVectors(oldDir, newDir);
            skeleton.bones[bIdx].currentRotation = deltaRot * skeleton.bones[bIdx].currentRotation;
        }
        skeleton.bones[nextIdx].currentPosition = chainPos[i + 1] - chainPos[i];
    }

    return reached;
}

} // namespace Animation

