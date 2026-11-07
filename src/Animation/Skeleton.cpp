#include "Skeleton.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <functional>
#include <iostream>

namespace Animation {

// =========================================================================
// Skeleton
// =========================================================================
int Skeleton::AddBone(const std::string& boneName, int parentId) {
    int idx = static_cast<int>(bones.size());
    Bone b;
    b.id = idx;
    b.name = boneName;
    b.parentId = parentId;
    bones.push_back(b);
    boneNameToIndex[boneName] = idx;

    if (parentId >= 0 && parentId < static_cast<int>(bones.size())) {
        bones[parentId].childrenIds.push_back(idx);
    }
    return idx;
}

int Skeleton::FindBoneIndex(const std::string& boneName) const {
    auto it = boneNameToIndex.find(boneName);
    if (it != boneNameToIndex.end()) return it->second;
    return -1;
}

Bone* Skeleton::GetBone(int index) {
    if (index >= 0 && index < static_cast<int>(bones.size())) return &bones[index];
    return nullptr;
}

const Bone* Skeleton::GetBone(int index) const {
    if (index >= 0 && index < static_cast<int>(bones.size())) return &bones[index];
    return nullptr;
}

void Skeleton::CalculateInverseBindMatrices() {
    std::vector<glm::mat4> worldBind(bones.size(), glm::mat4(1.0f));
    std::vector<bool> computed(bones.size(), false);

    std::function<void(int)> computeWorld = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(bones.size()) || computed[idx]) return;
        const auto& bone = bones[idx];
        glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.bindPosition) *
                          glm::mat4_cast(bone.bindRotation) *
                          glm::scale(glm::mat4(1.0f), bone.bindScale);

        if (bone.parentId >= 0 && bone.parentId < static_cast<int>(bones.size()) && bone.parentId != idx) {
            computeWorld(bone.parentId);
            worldBind[idx] = worldBind[bone.parentId] * local;
        } else {
            worldBind[idx] = local;
        }
        computed[idx] = true;
    };

    for (size_t i = 0; i < bones.size(); ++i) {
        computeWorld(static_cast<int>(i));
        bones[i].inverseBindMatrix = glm::inverse(worldBind[i]);
    }
}

void Skeleton::EvaluateSkinningMatrices(std::vector<glm::mat4>& outMatrices) {
    outMatrices.resize(bones.size(), glm::mat4(1.0f));
    std::vector<glm::mat4> worldAnim(bones.size(), glm::mat4(1.0f));
    std::vector<bool> computed(bones.size(), false);

    std::function<void(int)> computeWorld = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(bones.size()) || computed[idx]) return;
        const auto& bone = bones[idx];
        glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.currentPosition) *
                          glm::mat4_cast(bone.currentRotation) *
                          glm::scale(glm::mat4(1.0f), bone.currentScale);

        if (bone.parentId >= 0 && bone.parentId < static_cast<int>(bones.size()) && bone.parentId != idx) {
            computeWorld(bone.parentId);
            worldAnim[idx] = worldAnim[bone.parentId] * local;
        } else {
            worldAnim[idx] = local;
        }
        computed[idx] = true;
    };

    for (size_t i = 0; i < bones.size(); ++i) {
        computeWorld(static_cast<int>(i));
        bones[i].skinningMatrix = worldAnim[i] * bones[i].inverseBindMatrix;
        outMatrices[i] = bones[i].skinningMatrix;
    }
}

JsonValue Skeleton::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["name"] = name;
    JsonValue boneArr = JsonValue::Array();

    for (const auto& b : bones) {
        JsonValue bObj = JsonValue::Object();
        bObj["id"] = b.id;
        bObj["name"] = b.name;
        bObj["parentId"] = b.parentId;

        JsonValue pos = JsonValue::Array();
        pos.Push(b.bindPosition.x); pos.Push(b.bindPosition.y); pos.Push(b.bindPosition.z);
        bObj["bindPosition"] = pos;

        JsonValue rot = JsonValue::Array();
        rot.Push(b.bindRotation.x); rot.Push(b.bindRotation.y); rot.Push(b.bindRotation.z); rot.Push(b.bindRotation.w);
        bObj["bindRotation"] = rot;

        boneArr.Push(bObj);
    }
    root["bones"] = boneArr;
    return root;
}

void Skeleton::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    name = json["name"].AsString("Skeleton");
    bones.clear();
    boneNameToIndex.clear();

    if (json.HasKey("bones") && json["bones"].IsArray()) {
        const auto& arr = json["bones"];
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            const auto& item = arr[i];
            Bone b;
            b.id = item["id"].AsInt(static_cast<int>(i));
            b.name = item["name"].AsString("Bone");
            b.parentId = item["parentId"].AsInt(-1);

            if (item.HasKey("bindPosition") && item["bindPosition"].arrVal.size() >= 3) {
                b.bindPosition = glm::vec3(item["bindPosition"][0].AsFloat(), item["bindPosition"][1].AsFloat(), item["bindPosition"][2].AsFloat());
            }
            if (item.HasKey("bindRotation") && item["bindRotation"].arrVal.size() >= 4) {
                b.bindRotation = glm::quat(item["bindRotation"][3].AsFloat(), item["bindRotation"][0].AsFloat(), item["bindRotation"][1].AsFloat(), item["bindRotation"][2].AsFloat());
            }
            b.currentPosition = b.bindPosition;
            b.currentRotation = b.bindRotation;
            b.currentScale = glm::vec3(1.0f);

            boneNameToIndex[b.name] = b.id;
            bones.push_back(b);
        }
    }
    CalculateInverseBindMatrices();
}

// =========================================================================
// BoneTrack
// =========================================================================
void BoneTrack::AddKeyframe(float time, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    keyframes.push_back({time, pos, rot, scale});
    std::sort(keyframes.begin(), keyframes.end(), [](const BoneKeyframe& a, const BoneKeyframe& b) {
        return a.time < b.time;
    });
}

void BoneTrack::Sample(float time, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) const {
    if (keyframes.empty()) return;

    if (time <= keyframes.front().time) {
        outPos = keyframes.front().position;
        outRot = keyframes.front().rotation;
        outScale = keyframes.front().scale;
        return;
    }

    if (time >= keyframes.back().time) {
        outPos = keyframes.back().position;
        outRot = keyframes.back().rotation;
        outScale = keyframes.back().scale;
        return;
    }

    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            float dt = keyframes[i + 1].time - keyframes[i].time;
            float factor = (dt > 1e-6f) ? (time - keyframes[i].time) / dt : 0.0f;

            outPos = glm::mix(keyframes[i].position, keyframes[i + 1].position, factor);
            outRot = glm::slerp(keyframes[i].rotation, keyframes[i + 1].rotation, factor);
            outScale = glm::mix(keyframes[i].scale, keyframes[i + 1].scale, factor);
            return;
        }
    }
}

JsonValue BoneTrack::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["boneId"] = boneId;
    root["boneName"] = boneName;

    JsonValue kArr = JsonValue::Array();
    for (const auto& k : keyframes) {
        JsonValue kObj = JsonValue::Object();
        kObj["time"] = k.time;

        JsonValue pos = JsonValue::Array();
        pos.Push(k.position.x); pos.Push(k.position.y); pos.Push(k.position.z);
        kObj["pos"] = pos;

        JsonValue rot = JsonValue::Array();
        rot.Push(k.rotation.x); rot.Push(k.rotation.y); rot.Push(k.rotation.z); rot.Push(k.rotation.w);
        kObj["rot"] = rot;

        JsonValue sc = JsonValue::Array();
        sc.Push(k.scale.x); sc.Push(k.scale.y); sc.Push(k.scale.z);
        kObj["scale"] = sc;

        kArr.Push(kObj);
    }
    root["keyframes"] = kArr;
    return root;
}

void BoneTrack::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    boneId = json["boneId"].AsInt(-1);
    boneName = json["boneName"].AsString("");
    keyframes.clear();

    if (json.HasKey("keyframes") && json["keyframes"].IsArray()) {
        const auto& arr = json["keyframes"];
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            const auto& item = arr[i];
            BoneKeyframe k;
            k.time = item["time"].AsFloat(0.0f);
            if (item.HasKey("pos") && item["pos"].arrVal.size() >= 3) {
                k.position = glm::vec3(item["pos"][0].AsFloat(), item["pos"][1].AsFloat(), item["pos"][2].AsFloat());
            }
            if (item.HasKey("rot") && item["rot"].arrVal.size() >= 4) {
                k.rotation = glm::quat(item["rot"][3].AsFloat(), item["rot"][0].AsFloat(), item["rot"][1].AsFloat(), item["rot"][2].AsFloat());
            }
            if (item.HasKey("scale") && item["scale"].arrVal.size() >= 3) {
                k.scale = glm::vec3(item["scale"][0].AsFloat(1.0f), item["scale"][1].AsFloat(1.0f), item["scale"][2].AsFloat(1.0f));
            }
            keyframes.push_back(k);
        }
    }
}

// =========================================================================
// SkeletalAnimationClip
// =========================================================================
BoneTrack* SkeletalAnimationClip::GetOrCreateTrack(int boneId, const std::string& boneName) {
    for (auto& track : boneTracks) {
        if (track.boneId == boneId || (!boneName.empty() && track.boneName == boneName)) {
            return &track;
        }
    }
    BoneTrack newTrack;
    newTrack.boneId = boneId;
    newTrack.boneName = boneName;
    boneTracks.push_back(newTrack);
    return &boneTracks.back();
}

const BoneTrack* SkeletalAnimationClip::GetTrack(int boneId) const {
    for (const auto& track : boneTracks) {
        if (track.boneId == boneId) return &track;
    }
    return nullptr;
}

const BoneTrack* SkeletalAnimationClip::GetTrackByName(const std::string& boneName) const {
    for (const auto& track : boneTracks) {
        if (track.boneName == boneName) return &track;
    }
    return nullptr;
}

void SkeletalAnimationClip::Apply(Skeleton& skeleton, float time) const {
    float evalTime = time;
    if (loop && duration > 0.0f) {
        evalTime = std::fmod(time, duration);
        if (evalTime < 0.0f) evalTime += duration;
    } else {
        evalTime = std::clamp(time, 0.0f, duration);
    }

    for (const auto& track : boneTracks) {
        Bone* bone = nullptr;
        if (track.boneId >= 0 && track.boneId < static_cast<int>(skeleton.bones.size())) {
            bone = &skeleton.bones[track.boneId];
        } else if (!track.boneName.empty()) {
            int idx = skeleton.FindBoneIndex(track.boneName);
            if (idx >= 0) bone = &skeleton.bones[idx];
        }

        if (bone) {
            track.Sample(evalTime, bone->currentPosition, bone->currentRotation, bone->currentScale);
        }
    }
}

JsonValue SkeletalAnimationClip::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["name"] = name;
    root["duration"] = duration;
    root["loop"] = loop;

    JsonValue tArr = JsonValue::Array();
    for (const auto& t : boneTracks) {
        tArr.Push(t.Serialize());
    }
    root["tracks"] = tArr;
    return root;
}

void SkeletalAnimationClip::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    name = json["name"].AsString("SkeletalClip");
    duration = json["duration"].AsFloat(1.0f);
    loop = json["loop"].AsBool(true);
    boneTracks.clear();

    if (json.HasKey("tracks") && json["tracks"].IsArray()) {
        const auto& arr = json["tracks"];
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            BoneTrack t;
            t.Deserialize(arr[i]);
            boneTracks.push_back(t);
        }
    }
}

// =========================================================================
// AnimationTree
// =========================================================================
AnimationTree::AnimationTree() = default;

void AnimationTree::AddState(const std::string& name, std::shared_ptr<SkeletalAnimationClip> clip, float speed, bool loop) {
    AnimState state;
    state.name = name;
    state.clip = clip;
    state.playbackSpeed = speed;
    state.loop = loop;
    m_states[name] = state;

    if (m_currentStateName.empty()) {
        m_currentStateName = name;
    }
}

void AnimationTree::AddTransition(const std::string& fromState, const std::string& toState, float blendDuration,
                                  const std::string& conditionParam, bool conditionValue) {
    auto it = m_states.find(fromState);
    if (it != m_states.end()) {
        AnimTransition t;
        t.targetState = toState;
        t.blendDuration = blendDuration;
        t.conditionParam = conditionParam;
        t.conditionValue = conditionValue;
        it->second.transitions.push_back(t);
    }
}

void AnimationTree::SetParameterBool(const std::string& param, bool value) {
    m_boolParams[param] = value;
}

bool AnimationTree::GetParameterBool(const std::string& param) const {
    auto it = m_boolParams.find(param);
    if (it != m_boolParams.end()) return it->second;
    return false;
}

void AnimationTree::SetParameterFloat(const std::string& param, float value) {
    m_floatParams[param] = value;
}

float AnimationTree::GetParameterFloat(const std::string& param) const {
    auto it = m_floatParams.find(param);
    if (it != m_floatParams.end()) return it->second;
    return 0.0f;
}

void AnimationTree::SetCurrentState(const std::string& stateName, float blendDuration) {
    if (m_currentStateName == stateName || m_states.find(stateName) == m_states.end()) return;

    if (blendDuration > 0.0f) {
        m_targetStateName = stateName;
        m_targetTime = 0.0f;
        m_blendDuration = blendDuration;
        m_blendWeight = 0.0f;
        m_isBlending = true;
    } else {
        m_currentStateName = stateName;
        m_currentTime = 0.0f;
        m_isBlending = false;
    }
}

void AnimationTree::Update(float dt, Skeleton& skeleton) {
    if (m_currentStateName.empty() || m_states.empty()) return;

    auto curIt = m_states.find(m_currentStateName);
    if (curIt == m_states.end()) return;

    // Check automatic state transitions via boolean parameters
    if (!m_isBlending) {
        for (const auto& trans : curIt->second.transitions) {
            if (!trans.conditionParam.empty()) {
                if (GetParameterBool(trans.conditionParam) == trans.conditionValue) {
                    SetCurrentState(trans.targetState, trans.blendDuration);
                    break;
                }
            }
        }
    }

    // Advance current state time
    m_currentTime += dt * curIt->second.playbackSpeed;

    if (!m_isBlending) {
        // Single state playback
        if (curIt->second.clip) {
            curIt->second.clip->Apply(skeleton, m_currentTime);
        }
    } else {
        // Crossfade blending between current and target state
        auto targetIt = m_states.find(m_targetStateName);
        if (targetIt != m_states.end() && targetIt->second.clip) {
            m_targetTime += dt * targetIt->second.playbackSpeed;
            m_blendWeight += (dt / (std::max)(m_blendDuration, 1e-4f));

            if (m_blendWeight >= 1.0f) {
                // Blend finished: transition completes
                m_currentStateName = m_targetStateName;
                m_currentTime = m_targetTime;
                m_targetStateName = "";
                m_isBlending = false;
                m_blendWeight = 0.0f;
                targetIt->second.clip->Apply(skeleton, m_currentTime);
            } else {
                // Apply source pose
                if (curIt->second.clip) {
                    curIt->second.clip->Apply(skeleton, m_currentTime);
                }

                // Sample target pose and slerp bone transforms
                std::vector<glm::vec3> targetPos(skeleton.bones.size());
                std::vector<glm::quat> targetRot(skeleton.bones.size());
                std::vector<glm::vec3> targetScale(skeleton.bones.size());

                for (size_t b = 0; b < skeleton.bones.size(); ++b) {
                    targetPos[b] = skeleton.bones[b].currentPosition;
                    targetRot[b] = skeleton.bones[b].currentRotation;
                    targetScale[b] = skeleton.bones[b].currentScale;
                }

                targetIt->second.clip->Apply(skeleton, m_targetTime);

                for (size_t b = 0; b < skeleton.bones.size(); ++b) {
                    skeleton.bones[b].currentPosition = glm::mix(targetPos[b], skeleton.bones[b].currentPosition, m_blendWeight);
                    skeleton.bones[b].currentRotation = glm::slerp(targetRot[b], skeleton.bones[b].currentRotation, m_blendWeight);
                    skeleton.bones[b].currentScale = glm::mix(targetScale[b], skeleton.bones[b].currentScale, m_blendWeight);
                }
            }
        } else {
            m_isBlending = false;
        }
    }
}

JsonValue AnimationTree::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["currentState"] = m_currentStateName;
    return root;
}

void AnimationTree::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    m_currentStateName = json["currentState"].AsString("");
}

} // namespace Animation

