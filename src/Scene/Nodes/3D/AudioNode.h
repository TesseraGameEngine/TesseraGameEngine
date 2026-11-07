#pragma once

#include "Scene/Node.h"
#include <string>
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

enum class AudioAttenuationModel {
    InverseDistance = 0,
    Linear = 1,
    Exponential = 2
};

class AudioNode : public Node {
public:
    AudioNode(const std::string& name = "Audio Source", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    // Computes 3D distance attenuation and stereo left/right panning relative to a listener
    void ComputeSpatialGainAndPan(const Scene& scene,
                                  const glm::vec3& listenerPos,
                                  const glm::vec3& listenerRight,
                                  float& outLeftGain,
                                  float& outRightGain) const;

    // Checks if a 3D position is inside this node's reverb zone and returns wet mix weight [0, 1]
    float EvaluateReverbZoneWeight(const Scene& scene, const glm::vec3& listenerPos) const;

    std::string audioClipPath{""};
    std::string busName{"SFX"}; // "Master", "SFX", "Music", "Voice", "Ambient"

    bool is3DSpatial{true};
    float volume{1.0f};
    float pitch{1.0f};
    bool loop{false};
    bool playOnAwake{true};
    bool isPlaying{false};

    // 3D Spatial Attenuation
    AudioAttenuationModel attenuationModel{AudioAttenuationModel::InverseDistance};
    float minDistance{1.0f};
    float maxDistance{30.0f};
    float rolloffFactor{1.0f};
    float dopplerScale{1.0f};

    // Reverb Zone configuration
    bool isReverbZone{false};
    float reverbInnerRadius{3.0f};
    float reverbOuterRadius{12.0f};
    float reverbRoomSize{0.65f};
    float reverbDecayTime{1.8f};
    float reverbDamping{0.4f};
    float reverbWetMix{0.35f};
};

