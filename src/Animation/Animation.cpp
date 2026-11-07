#include "Animation.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Animation {

glm::vec3 TransformTrack::Interpolate(const std::vector<Keyframe>& keys, float time, const glm::vec3& defaultValue) {
    if (keys.empty()) return defaultValue;
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            const auto& k0 = keys[i];
            const auto& k1 = keys[i + 1];

            float dt = k1.time - k0.time;
            if (dt <= 1e-6f) return k0.value;

            float factor = (time - k0.time) / dt;

            if (k0.interp == InterpolationType::Step) {
                return k0.value;
            } else if (k0.interp == InterpolationType::Smooth) {
                factor = factor * factor * (3.0f - 2.0f * factor);
            }
            return glm::mix(k0.value, k1.value, factor);
        }
    }
    return keys.back().value;
}

glm::vec3 TransformTrack::SamplePosition(float time) const {
    return Interpolate(positionKeys, time, glm::vec3(0.0f));
}

glm::vec3 TransformTrack::SampleRotation(float time) const {
    return Interpolate(rotationKeys, time, glm::vec3(0.0f));
}

glm::vec3 TransformTrack::SampleScale(float time) const {
    return Interpolate(scaleKeys, time, glm::vec3(1.0f));
}

void TransformTrack::AddPositionKey(float time, const glm::vec3& pos) {
    for (auto& k : positionKeys) {
        if (std::abs(k.time - time) < 0.01f) {
            k.value = pos;
            return;
        }
    }
    positionKeys.push_back(Keyframe{ time, pos, InterpolationType::Linear });
    std::sort(positionKeys.begin(), positionKeys.end(), [](const Keyframe& a, const Keyframe& b) {
        return a.time < b.time;
    });
}

void TransformTrack::AddRotationKey(float time, const glm::vec3& rot) {
    for (auto& k : rotationKeys) {
        if (std::abs(k.time - time) < 0.01f) {
            k.value = rot;
            return;
        }
    }
    rotationKeys.push_back(Keyframe{ time, rot, InterpolationType::Linear });
    std::sort(rotationKeys.begin(), rotationKeys.end(), [](const Keyframe& a, const Keyframe& b) {
        return a.time < b.time;
    });
}

void TransformTrack::AddScaleKey(float time, const glm::vec3& sc) {
    for (auto& k : scaleKeys) {
        if (std::abs(k.time - time) < 0.01f) {
            k.value = sc;
            return;
        }
    }
    scaleKeys.push_back(Keyframe{ time, sc, InterpolationType::Linear });
    std::sort(scaleKeys.begin(), scaleKeys.end(), [](const Keyframe& a, const Keyframe& b) {
        return a.time < b.time;
    });
}

JsonValue TransformTrack::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["nodeId"] = nodeId;

    auto serializeKeys = [](const std::vector<Keyframe>& keys) -> JsonValue {
        JsonValue arr = JsonValue::Array();
        for (const auto& k : keys) {
            JsonValue item = JsonValue::Object();
            item["time"] = k.time;
            JsonValue val = JsonValue::Array();
            val.Push(k.value.x); val.Push(k.value.y); val.Push(k.value.z);
            item["val"] = val;
            item["interp"] = static_cast<int>(k.interp);
            arr.Push(item);
        }
        return arr;
    };

    root["posKeys"] = serializeKeys(positionKeys);
    root["rotKeys"] = serializeKeys(rotationKeys);
    root["scaleKeys"] = serializeKeys(scaleKeys);
    return root;
}

void TransformTrack::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    nodeId = json["nodeId"].AsUInt(0);

    auto deserializeKeys = [](const JsonValue& arr, std::vector<Keyframe>& outKeys) {
        outKeys.clear();
        if (!arr.IsArray()) return;
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            const auto& item = arr[i];
            Keyframe k;
            k.time = item["time"].AsFloat(0.0f);
            if (item.HasKey("val") && item["val"].arrVal.size() >= 3) {
                k.value = glm::vec3(item["val"][0].AsFloat(), item["val"][1].AsFloat(), item["val"][2].AsFloat());
            }
            k.interp = static_cast<InterpolationType>(item["interp"].AsInt(0));
            outKeys.push_back(k);
        }
    };

    deserializeKeys(json["posKeys"], positionKeys);
    deserializeKeys(json["rotKeys"], rotationKeys);
    deserializeKeys(json["scaleKeys"], scaleKeys);
}

TransformTrack* AnimationClip::GetOrCreateTrack(uint32_t nodeId) {
    for (auto& track : tracks) {
        if (track.nodeId == nodeId) return &track;
    }
    TransformTrack newTrack;
    newTrack.nodeId = nodeId;
    tracks.push_back(newTrack);
    return &tracks.back();
}

const TransformTrack* AnimationClip::GetTrack(uint32_t nodeId) const {
    for (const auto& track : tracks) {
        if (track.nodeId == nodeId) return &track;
    }
    return nullptr;
}

void AnimationClip::Apply(Scene& scene, float time) const {
    if (tracks.empty()) return;

    float evalTime = time;
    if (duration > 0.0f) {
        if (loop) {
            evalTime = std::fmod(time, duration);
            if (evalTime < 0.0f) evalTime += duration;
        } else {
            evalTime = std::clamp(time, 0.0f, duration);
        }
    }

    for (const auto& track : tracks) {
        auto node = scene.GetNodeById(track.nodeId);
        if (!node) continue;

        if (!track.positionKeys.empty()) {
            node->position = track.SamplePosition(evalTime);
        }
        if (!track.rotationKeys.empty()) {
            node->rotation = track.SampleRotation(evalTime);
        }
        if (!track.scaleKeys.empty()) {
            node->scale = track.SampleScale(evalTime);
        }
    }
}

void AnimationClip::AddKeyframe(uint32_t nodeId, float time, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& sc) {
    auto track = GetOrCreateTrack(nodeId);
    track->AddPositionKey(time, pos);
    track->AddRotationKey(time, rot);
    track->AddScaleKey(time, sc);
    if (time > duration) {
        duration = time + 0.5f;
    }
}

JsonValue AnimationClip::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["name"] = name;
    root["duration"] = duration;
    root["loop"] = loop;

    JsonValue trackArr = JsonValue::Array();
    for (const auto& track : tracks) {
        trackArr.Push(track.Serialize());
    }
    root["tracks"] = trackArr;
    return root;
}

void AnimationClip::Deserialize(const JsonValue& json) {
    if (!json.IsObject()) return;
    name = json["name"].AsString("AnimationClip");
    duration = json["duration"].AsFloat(5.0f);
    loop = json["loop"].AsBool(true);

    tracks.clear();
    if (json.HasKey("tracks") && json["tracks"].IsArray()) {
        const auto& arr = json["tracks"];
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            TransformTrack track;
            track.Deserialize(arr[i]);
            tracks.push_back(track);
        }
    }
}

AnimationPlayer::AnimationPlayer() {
    // Default sample animation clip
    m_clip = std::make_shared<AnimationClip>();
    m_clip->name = "Sample Animation";
    m_clip->duration = 4.0f;
    m_clip->loop = true;
}

void AnimationPlayer::SetClip(std::shared_ptr<AnimationClip> clip) {
    m_clip = clip;
    m_currentTime = 0.0f;
}

void AnimationPlayer::Stop(Scene& scene) {
    m_isPlaying = false;
    m_currentTime = 0.0f;
    if (m_clip) {
        m_clip->Apply(scene, 0.0f);
    }
}

void AnimationPlayer::Seek(Scene& scene, float time) {
    m_currentTime = time;
    if (m_clip) {
        m_clip->Apply(scene, m_currentTime);
    }
}

void AnimationPlayer::Update(Scene& scene, float dt) {
    if (!m_clip || !m_isPlaying) return;

    m_currentTime += dt * playbackSpeed;
    if (m_clip->duration > 0.0f) {
        if (m_clip->loop) {
            if (m_currentTime > m_clip->duration) {
                m_currentTime = std::fmod(m_currentTime, m_clip->duration);
            }
        } else {
            if (m_currentTime >= m_clip->duration) {
                m_currentTime = m_clip->duration;
                m_isPlaying = false;
            }
        }
    }
    m_clip->Apply(scene, m_currentTime);
}

} // namespace Animation

