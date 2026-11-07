#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "Utils/SimpleJson.h"

class Scene;

namespace Animation {

enum class CutsceneColorFilter : int {
    None = 0,
    Sepia = 1,
    BlackAndWhite = 2,
    NoirHighContrast = 3,
    VintageWarm = 4,
    ColdMoonlight = 5,
    DreamyBloom = 6
};

inline const char* CutsceneColorFilterToString(CutsceneColorFilter filter) {
    switch (filter) {
        case CutsceneColorFilter::None:             return "None (Scene Default)";
        case CutsceneColorFilter::Sepia:            return "Sepia Tone";
        case CutsceneColorFilter::BlackAndWhite:    return "Black & White (Monochrome)";
        case CutsceneColorFilter::NoirHighContrast: return "Film Noir (High-Contrast B&W)";
        case CutsceneColorFilter::VintageWarm:      return "Vintage 35mm Warm";
        case CutsceneColorFilter::ColdMoonlight:    return "Cold Moonlight / Thriller";
        case CutsceneColorFilter::DreamyBloom:      return "Dreamy Flashback (Soft Bloom)";
        default:                                    return "None (Scene Default)";
    }
}

struct CutsceneCameraShot {
    float startTime{0.0f};
    float duration{3.0f};
    glm::vec3 startPos{0.0f, 2.2f, 6.0f};
    glm::vec3 endPos{0.0f, 1.8f, 3.2f};
    glm::vec3 startLookAt{0.0f, 1.0f, 0.0f};
    glm::vec3 endLookAt{0.0f, 1.2f, 0.0f};
    std::string lookAtNodeName{""}; // Optional: dynamically track a node/character in the scene
    glm::vec3 lookAtOffset{0.0f, 1.2f, 0.0f};
    float startFov{55.0f};
    float endFov{42.0f};
    bool smoothBlend{true};

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& j);
};

struct CutsceneActorAction {
    std::string actorNodeName{""};
    float startTime{0.0f};
    float duration{3.0f};
    bool useCurrentStartPos{true}; // Smoothly walk from wherever the character was when triggered
    glm::vec3 startPos{0.0f, 0.0f, 0.0f};
    glm::vec3 endPos{0.0f, 0.0f, 0.0f};
    std::string animState{"Walk"};
    std::string endAnimState{"Idle"};
    bool faceMovementDirection{true};

    // Runtime-only captured start position when useCurrentStartPos is true
    glm::vec3 runtimeCapturedStartPos{0.0f};
    bool runtimeStartCaptured{false};

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& j);
};

struct CutsceneSubtitleCue {
    float startTime{0.0f};
    float duration{3.0f};
    std::string speaker{"Narrator"};
    std::string text{""};
    std::string audioClipPath{""}; // Optional .wav path or clip name; empty = silent
    float audioVolume{0.85f};
    float synthFrequencyHz{0.0f};  // Optional procedural tone cue if > 0 and audioClipPath is empty

    // Runtime-only flag to avoid re-triggering audio every frame
    bool runtimeTriggered{false};

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& j);
};

struct CutsceneSequence {
    std::string name{"CinematicCutscene"};
    float duration{6.0f};
    bool skippable{true};
    bool silentAudio{false}; // True = mute all cutscene audio cues (silent film / silent scene mode)
    bool restoreCameraOnEnd{true};
    float letterboxAmount{0.12f}; // Fraction of screen height per bar (0.12 = 2.39:1 widescreen)

    // Camera & Post-Processing Effects
    CutsceneColorFilter colorFilter{CutsceneColorFilter::None};
    bool overrideVignette{true};
    float vignetteAmount{0.45f};
    float filmGrainAmount{0.0f};
    float chromaticAberrationAmount{0.0f};
    bool enableDof{false};
    bool autoFocusOnLookAt{true};
    float dofFocusDistance{5.0f};
    float dofAperture{0.35f};

    // Location / Proximity Trigger ("character goes to a location and an animated scene plays")
    bool triggerOnLocation{false};
    std::string triggerActorName{"*"}; // "*" = any CharacterNode or player camera, or specific node name
    glm::vec3 triggerPosition{0.0f, 0.0f, 0.0f};
    float triggerRadius{2.5f};
    bool triggerOnce{true};
    bool runtimeHasTriggered{false};

    std::vector<CutsceneCameraShot> cameraShots;
    std::vector<CutsceneActorAction> actorActions;
    std::vector<CutsceneSubtitleCue> subtitleCues;

    JsonValue Serialize() const;
    void Deserialize(const JsonValue& j);
};

} // namespace Animation

#include "Scene/Scene.h"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace Animation {

class CutscenePlayer {
public:
    CutscenePlayer() = default;
    ~CutscenePlayer() = default;

    // Start playing a cutscene by name from scene.GetCutscenes()
    bool Start(Scene& scene, const std::string& cutsceneName);

    // Start playing a specific CutsceneSequence definition on the scene
    bool StartSequence(Scene& scene, CutsceneSequence& sequence);

    // Advance active cutscene playback (camera shots, character movement/animations, subtitles, audio, post-FX)
    void Update(Scene& scene, float dt);

    // Skip the currently playing cutscene (if skippable or force=true): fast-forwards actors to end positions
    bool Skip(Scene& scene, bool force = false);

    // Stop the cutscene and restore original SceneEnvironment & camera state
    void Stop(Scene& scene, bool applyFinalActorStates = true);

    // Check all scene cutscenes with triggerOnLocation == true and start if a matching character enters triggerRadius
    bool CheckLocationTriggers(Scene& scene);

    // Reset runtime trigger flags (e.g., when entering Play mode)
    void ResetTriggers(Scene& scene);

    bool IsPlaying() const { return m_isPlaying; }
    bool IsSkippable() const { return m_isPlaying && m_activeSequence && m_activeSequence->skippable; }
    float GetPlaybackTime() const { return m_currentTime; }
    float GetCurrentTime() const { return m_currentTime; }
    float GetDuration() const { return m_activeSequence ? m_activeSequence->duration : 0.0f; }
    const std::string& GetActiveCutsceneName() const { return m_activeCutsceneName; }
    const CutsceneSequence* GetActiveSequence() const { return m_activeSequence; }
    const CutsceneSubtitleCue* GetActiveSubtitle() const { return m_activeSubtitle; }
    float GetLetterboxBarRatio() const;

private:
    void ApplyEnvironmentEffects(Scene& scene, float currentFocusDist);
    void RestoreEnvironmentAndCamera(Scene& scene);
    void ApplyFinalActorStates(Scene& scene);

    bool m_isPlaying{false};
    float m_currentTime{0.0f};
    std::string m_activeCutsceneName{""};
    CutsceneSequence* m_activeSequence{nullptr};
    const CutsceneSubtitleCue* m_activeSubtitle{nullptr};

    // Snapshot of pre-cutscene environment & camera to restore on finish/skip
    bool m_hasEnvironmentSnapshot{false};
    SceneEnvironment m_savedEnvironment{};

    bool m_hasCameraSnapshot{false};
    uint32_t m_savedCameraNodeId{0};
    glm::vec3 m_savedCameraPos{0.0f};
    glm::vec3 m_savedCameraRot{0.0f};
    float m_savedCameraFov{60.0f};
    bool m_createdTempCamera{false};

    // Saved patrol states for CharacterNodes participating in the cutscene
    struct SavedCharacterState {
        uint32_t nodeId{0};
        bool wasPatrolling{false};
    };
    std::vector<SavedCharacterState> m_savedCharStates;
    std::vector<uint32_t> m_activeAudioVoices;
};

} // namespace Animation
