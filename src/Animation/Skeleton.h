#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Animation/Animation.h"
#include "Utils/SimpleJson.h"

namespace Animation {

// A single Bone in a skeleton hierarchy
struct Bone {
    int id{-1};
    std::string name{""};
    int parentId{-1};
    std::vector<int> childrenIds;

    // Rest/Bind pose transforms (relative to parent)
    glm::vec3 bindPosition{0.0f};
    glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bindScale{1.0f};

    // Inverse bind matrix (world-space to bone-space in rest pose)
    glm::mat4 inverseBindMatrix{1.0f};

    // Current local pose animated transforms
    glm::vec3 currentPosition{0.0f};
    glm::quat currentRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 currentScale{1.0f};

    // Final skinning matrix: WorldBoneTransform * inverseBindMatrix
    glm::mat4 skinningMatrix{1.0f};
};

class Skeleton {
public:
    std::string name{"DefaultSkeleton"};
    std::vector<Bone> bones;
    std::map<std::string, int> boneNameToIndex;

    int AddBone(const std::string& boneName, int parentId = -1);
    int FindBoneIndex(const std::string& boneName) const;
    Bone* GetBone(int index);
    const Bone* GetBone(int index) const;

    void CalculateInverseBindMatrices();
    void EvaluateSkinningMatrices(std::vector<glm::mat4>& outMatrices);

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);
};

// Skeletal Bone animation keyframes
struct BoneKeyframe {
    float time{0.0f};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

class BoneTrack {
public:
    int boneId{-1};
    std::string boneName{""};
    std::vector<BoneKeyframe> keyframes;

    void Sample(float time, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) const;
    void AddKeyframe(float time, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);
};

class SkeletalAnimationClip {
public:
    std::string name{"SkeletalClip"};
    float duration{1.0f};
    float ticksPerSecond{30.0f};
    bool loop{true};
    std::vector<BoneTrack> boneTracks;

    BoneTrack* GetOrCreateTrack(int boneId, const std::string& boneName = "");
    const BoneTrack* GetTrack(int boneId) const;
    const BoneTrack* GetTrackByName(const std::string& boneName) const;

    void Apply(Skeleton& skeleton, float time) const;

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);
};

// =========================================================================
// Animation Tree & State Machine
// =========================================================================
enum class AnimTransitionMode {
    Immediate,
    Crossfade
};

struct AnimTransition {
    std::string targetState;
    float blendDuration{0.2f};
    std::string conditionParam; // boolean parameter name in anim tree
    bool conditionValue{true};
};

struct AnimState {
    std::string name;
    std::shared_ptr<SkeletalAnimationClip> clip;
    float playbackSpeed{1.0f};
    bool loop{true};
    std::vector<AnimTransition> transitions;
};

class AnimationTree {
public:
    AnimationTree();

    void AddState(const std::string& name, std::shared_ptr<SkeletalAnimationClip> clip, float speed = 1.0f, bool loop = true);
    void AddTransition(const std::string& fromState, const std::string& toState, float blendDuration = 0.2f,
                       const std::string& conditionParam = "", bool conditionValue = true);

    void SetParameterBool(const std::string& param, bool value);
    bool GetParameterBool(const std::string& param) const;

    void SetParameterFloat(const std::string& param, float value);
    float GetParameterFloat(const std::string& param) const;

    void SetCurrentState(const std::string& stateName, float blendDuration = 0.2f);
    const std::string& GetCurrentStateName() const { return m_currentStateName; }

    void Update(float dt, Skeleton& skeleton);

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);

private:
    std::map<std::string, AnimState> m_states;
    std::string m_currentStateName{""};
    std::string m_targetStateName{""};

    float m_currentTime{0.0f};
    float m_targetTime{0.0f};
    float m_blendWeight{0.0f};
    float m_blendDuration{0.2f};
    bool m_isBlending{false};

    std::map<std::string, bool> m_boolParams;
    std::map<std::string, float> m_floatParams;
};

} // namespace Animation

