#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include "Scene/Scene.h"

namespace Audio {

struct AudioClip {
    std::string name;
    uint32_t sampleRate{44100};
    uint16_t channels{1};
    std::vector<float> samples; // Normalized float PCM in [-1.0, 1.0]
};

struct AudioBus {
    std::string name{"SFX"};
    float volume{1.0f};
    bool muted{false};
    float reverbSend{0.0f};
};

struct ActiveVoice {
    uint32_t id{0};
    std::string clipName;
    std::string busName{"SFX"};
    float volume{1.0f};
    float pitch{1.0f};
    bool loop{false};
    bool is3D{false};
    glm::vec3 position{0.0f};
    float minDistance{1.0f};
    float maxDistance{30.0f};
    double sampleCursor{0.0};
    bool playing{true};
};

class AudioEngine {
public:
    static AudioEngine& Get();
    AudioEngine();
    ~AudioEngine();

    // Mixed speaker output: a thread keeps a few short buffers queued on the sound device, each
    // mixed from every playing voice (Play2D / Play3D), so any number of sounds overlap.
    // False when there is no sound device.
    bool StartOutput();
    void StopOutput();
    bool IsOutputRunning() const { return m_outputRunning; }

    // Hardware speaker playback (Windows Multimedia waveOut / PlaySound)
    bool TriggerHardwarePlayback(const std::string& clipNameOrPath,
                                 const std::string& busName = "SFX",
                                 float leftGain = 1.0f,
                                 float rightGain = 1.0f,
                                 float pitch = 1.0f,
                                 bool loop = false,
                                 float reverbWet = 0.0f,
                                 float reverbRoomSize = 0.65f,
                                 float reverbDamping = 0.4f);
    void StopHardwarePlayback();
    const std::map<std::string, AudioClip>& GetClips() const { return m_clips; }

    // 1. WAV Audio Clip Loading & Procedural Generation
    bool LoadWavFromFile(const std::string& name, const std::string& filepath);
    bool LoadWavFromMemory(const std::string& name, const uint8_t* data, size_t size);
    static std::vector<uint8_t> GenerateSineWaveWav(float frequencyHz = 440.0f,
                                                    float durationSec = 0.25f,
                                                    uint32_t sampleRate = 44100);
    // Thunder: a crack (for near strikes) then a long rolling rumble, different for every seed.
    // closeness: 0 = far away (only a low rumble) .. 1 = overhead (a sharp crack first)
    static std::vector<uint8_t> GenerateThunderWav(uint32_t seed, float closeness,
                                                   float durationSec = 6.0f, uint32_t sampleRate = 22050);
    // Ocean: the wash of surf (a seamless loop, swelling with the waves), the same heard under
    // water (a deep muffled rumble), and a wave crashing on a rock
    static std::vector<uint8_t> GenerateSurfWav(uint32_t seed, bool underwater, float durationSec = 12.0f, uint32_t sampleRate = 22050);
    static std::vector<uint8_t> GenerateCrashWav(uint32_t seed, float durationSec = 1.8f, uint32_t sampleRate = 22050);
    const AudioClip* GetClip(const std::string& name) const;

    // 2. Audio Mixing Buses ("Master", "SFX", "Music", "Voice", "Ambient")
    void SetBusVolume(const std::string& busName, float volume);
    float GetBusVolume(const std::string& busName) const;
    void SetBusMuted(const std::string& busName, bool muted);
    bool IsBusMuted(const std::string& busName) const;
    const std::map<std::string, AudioBus>& GetBuses() const { return m_buses; }

    // 3. 2D & 3D Playback
    uint32_t Play2D(const std::string& clipName,
                    const std::string& busName = "SFX",
                    float volume = 1.0f,
                    float pitch = 1.0f,
                    bool loop = false);

    uint32_t Play3D(const std::string& clipName,
                    const glm::vec3& worldPos,
                    const std::string& busName = "SFX",
                    float volume = 1.0f,
                    float minDistance = 1.0f,
                    float maxDistance = 30.0f,
                    bool loop = false);

    void StopVoice(uint32_t voiceId);
    void StopAll();
    size_t GetActiveVoiceCount() const;
    bool IsVoicePlaying(uint32_t voiceId) const;
    void SetVoiceVolume(uint32_t voiceId, float volume);
    void SetVoicePosition(uint32_t voiceId, const glm::vec3& worldPos);

    // 4. Listener & DSP Mixer (with 3D Spatial Attenuation & Reverb Zones)
    void SetListener(const glm::vec3& position,
                     const glm::vec3& forward = glm::vec3(0.0f, 0.0f, -1.0f),
                     const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

    // Mixes active voices + Scene AudioNodes + Scene Reverb Zones into stereo interleaved float buffer (L, R, L, R...)
    void MixOutputBuffer(const Scene* scene,
                         std::vector<float>& outStereoInterleaved,
                         size_t frameCount,
                         uint32_t sampleRate = 44100);

    // Schroeder Comb + Allpass Reverb DSP processor
    void ApplyReverbDSP(std::vector<float>& ioStereoInterleaved,
                        float wetMix,
                        float roomSize = 0.65f,
                        float damping = 0.4f);

private:
    std::map<std::string, AudioClip> m_clips;
    std::map<std::string, AudioBus> m_buses;
    std::vector<ActiveVoice> m_voices;
    uint32_t m_nextVoiceId{1};

    glm::vec3 m_listenerPos{0.0f};
    glm::vec3 m_listenerForward{0.0f, 0.0f, -1.0f};
    glm::vec3 m_listenerUp{0.0f, 1.0f, 0.0f};
    glm::vec3 m_listenerRight{1.0f, 0.0f, 0.0f};

    // Delay lines for Schroeder reverb
    std::vector<float> m_combDelayL1;
    std::vector<float> m_combDelayR1;
    size_t m_combIndex{0};
    std::vector<uint8_t> m_activeHardwareWavBuffer;

    // Clips, voices, buses and the listener are shared with the output thread
    mutable std::recursive_mutex m_mutex;
    std::thread m_outputThread;
    std::atomic<bool> m_outputRunning{false};
    void* m_waveOut{nullptr};     // HWAVEOUT
    void* m_outputEvent{nullptr}; // signalled when the device finishes a buffer
    void OutputLoop();
};

} // namespace Audio

