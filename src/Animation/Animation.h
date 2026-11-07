#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include "Scene/Scene.h"
#include "Utils/SimpleJson.h"

namespace Animation {

enum class InterpolationType {
    Linear,
    Step,
    Smooth
};

struct Keyframe {
    float time{0.0f}; // Time in seconds
    glm::vec3 value{0.0f};
    InterpolationType interp{InterpolationType::Linear};
};

class TransformTrack {
public:
    uint32_t nodeId{0};
    std::vector<Keyframe> positionKeys;
    std::vector<Keyframe> rotationKeys;
    std::vector<Keyframe> scaleKeys;

    glm::vec3 SamplePosition(float time) const;
    glm::vec3 SampleRotation(float time) const;
    glm::vec3 SampleScale(float time) const;

    void AddPositionKey(float time, const glm::vec3& pos);
    void AddRotationKey(float time, const glm::vec3& rot);
    void AddScaleKey(float time, const glm::vec3& sc);

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);

private:
    static glm::vec3 Interpolate(const std::vector<Keyframe>& keys, float time, const glm::vec3& defaultValue);
};

class AnimationClip {
public:
    std::string name{"AnimationClip"};
    float duration{5.0f};
    bool loop{true};
    std::vector<TransformTrack> tracks;

    TransformTrack* GetOrCreateTrack(uint32_t nodeId);
    const TransformTrack* GetTrack(uint32_t nodeId) const;

    void Apply(Scene& scene, float time) const;

    void AddKeyframe(uint32_t nodeId, float time, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& sc);

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& json);
};

class AnimationPlayer {
public:
    AnimationPlayer();

    void SetClip(std::shared_ptr<AnimationClip> clip);
    std::shared_ptr<AnimationClip> GetClip() const { return m_clip; }

    void Play() { m_isPlaying = true; }
    void Pause() { m_isPlaying = false; }
    void Stop(Scene& scene);
    void Seek(Scene& scene, float time);

    void Update(Scene& scene, float dt);

    float GetPlaybackTime() const { return m_currentTime; }
    void SetPlaybackTime(float t) { m_currentTime = t; }

    float GetDuration() const { return m_clip ? m_clip->duration : 5.0f; }
    bool IsPlaying() const { return m_isPlaying; }

    float playbackSpeed{1.0f};

private:
    std::shared_ptr<AnimationClip> m_clip;
    float m_currentTime{0.0f};
    bool m_isPlaying{false};
};

} // namespace Animation
