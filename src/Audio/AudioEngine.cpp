#include "AudioEngine.h"
#include "Scene/Nodes/3D/AudioNode.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace Audio {

namespace {

// 16-bit mono RIFF WAVE from float samples, normalised so the loudest is at `peakLevel`
std::vector<uint8_t> EncodeMonoWav(const std::vector<float>& samples, uint32_t sampleRate, float peakLevel) {
    float peak = 1e-6f;
    for (float s : samples) peak = std::max(peak, std::abs(s));
    const uint32_t numSamples = static_cast<uint32_t>(samples.size());
    const uint32_t dataSize = numSamples * sizeof(int16_t);
    const uint32_t riffSize = 36 + dataSize;
    std::vector<uint8_t> wav(44 + dataSize, 0);
    const uint32_t fmtChunkSize = 16;
    const uint16_t audioFormat = 1;
    const uint16_t numChannels = 1;
    const uint32_t byteRate = sampleRate * sizeof(int16_t);
    const uint16_t blockAlign = sizeof(int16_t);
    const uint16_t bitsPerSample = 16;
    std::memcpy(wav.data() + 0, "RIFF", 4);
    std::memcpy(wav.data() + 4, &riffSize, 4);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    std::memcpy(wav.data() + 16, &fmtChunkSize, 4);
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    std::memcpy(wav.data() + 22, &numChannels, 2);
    std::memcpy(wav.data() + 24, &sampleRate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4);
    std::memcpy(wav.data() + 32, &blockAlign, 2);
    std::memcpy(wav.data() + 34, &bitsPerSample, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, 4);
    int16_t* pcm = reinterpret_cast<int16_t*>(wav.data() + 44);
    const float gain = peakLevel / peak;
    for (uint32_t i = 0; i < numSamples; ++i) {
        pcm[i] = static_cast<int16_t>(std::clamp(samples[i] * gain, -1.0f, 1.0f) * 32767.0f);
    }
    return wav;
}

struct NoiseSource {
    uint32_t state;
    float Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

} // namespace

AudioEngine& AudioEngine::Get() {
    static AudioEngine instance;
    return instance;
}

AudioEngine::~AudioEngine() {
    StopOutput();
}

AudioEngine::AudioEngine()
    : m_combDelayL1(1638, 0.0f),
      m_combDelayR1(1661, 0.0f) {
    m_buses["Master"] = { "Master", 1.0f, false, 0.0f };
    m_buses["SFX"] = { "SFX", 1.0f, false, 0.1f };
    m_buses["Music"] = { "Music", 0.85f, false, 0.0f };
    m_buses["Voice"] = { "Voice", 1.0f, false, 0.05f };
    m_buses["Ambient"] = { "Ambient", 0.9f, false, 0.25f };

    // Built-in procedural audio clips so users can test audio out-of-the-box
    auto wav440 = GenerateSineWaveWav(440.0f, 0.45f, 44100);
    LoadWavFromMemory("Sine_440Hz", wav440.data(), wav440.size());
    auto wav880 = GenerateSineWaveWav(880.0f, 0.35f, 44100);
    LoadWavFromMemory("Chime_880Hz", wav880.data(), wav880.size());
    auto wav220 = GenerateSineWaveWav(220.0f, 0.60f, 44100);
    LoadWavFromMemory("Bass_220Hz", wav220.data(), wav220.size());
}

bool AudioEngine::LoadWavFromMemory(const std::string& name, const uint8_t* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!data || size < 44) return false;
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t numChannels = 1;
    uint32_t sampleRate = 44100;
    uint16_t bitsPerSample = 16;
    const uint8_t* pcmData = nullptr;
    uint32_t pcmByteSize = 0;

    size_t offset = 12;
    while (offset + 8 <= size) {
        char chunkId[4];
        std::memcpy(chunkId, data + offset, 4);
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, data + offset + 4, 4);
        offset += 8;
        if (offset + chunkSize > size) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::memcpy(&audioFormat, data + offset, 2);
            std::memcpy(&numChannels, data + offset + 2, 2);
            std::memcpy(&sampleRate, data + offset + 4, 4);
            std::memcpy(&bitsPerSample, data + offset + 14, 2);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            pcmData = data + offset;
            pcmByteSize = chunkSize;
            break;
        }
        offset += chunkSize + (chunkSize & 1u);
    }

    if (!pcmData || pcmByteSize == 0 || (audioFormat != 1 && audioFormat != 3)) {
        return false;
    }

    AudioClip clip;
    clip.name = name;
    clip.sampleRate = sampleRate;
    clip.channels = std::max<uint16_t>(1, numChannels);

    if (audioFormat == 1 && bitsPerSample == 16) {
        size_t numSamples = pcmByteSize / sizeof(int16_t);
        clip.samples.resize(numSamples);
        const int16_t* src = reinterpret_cast<const int16_t*>(pcmData);
        for (size_t i = 0; i < numSamples; ++i) {
            clip.samples[i] = static_cast<float>(src[i]) / 32768.0f;
        }
    } else if (audioFormat == 1 && bitsPerSample == 8) {
        clip.samples.resize(pcmByteSize);
        for (size_t i = 0; i < pcmByteSize; ++i) {
            clip.samples[i] = (static_cast<float>(pcmData[i]) - 128.0f) / 128.0f;
        }
    } else if (audioFormat == 3 && bitsPerSample == 32) {
        size_t numSamples = pcmByteSize / sizeof(float);
        clip.samples.resize(numSamples);
        std::memcpy(clip.samples.data(), pcmData, numSamples * sizeof(float));
    } else {
        return false;
    }

    m_clips[name] = std::move(clip);
    return true;
}

bool AudioEngine::LoadWavFromFile(const std::string& name, const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    std::streamsize sz = file.tellg();
    if (sz < 44) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), sz)) return false;
    return LoadWavFromMemory(name, bytes.data(), bytes.size());
}

std::vector<uint8_t> AudioEngine::GenerateSineWaveWav(float frequencyHz, float durationSec, uint32_t sampleRate) {
    uint32_t numSamples = static_cast<uint32_t>(std::max(1.0f, durationSec * sampleRate));
    uint32_t dataSize = numSamples * sizeof(int16_t);
    uint32_t riffSize = 36 + dataSize;

    std::vector<uint8_t> wav(44 + dataSize, 0);
    std::memcpy(wav.data() + 0, "RIFF", 4);
    std::memcpy(wav.data() + 4, &riffSize, 4);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);

    uint32_t fmtChunkSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1;
    uint32_t byteRate = sampleRate * sizeof(int16_t);
    uint16_t blockAlign = sizeof(int16_t);
    uint16_t bitsPerSample = 16;

    std::memcpy(wav.data() + 16, &fmtChunkSize, 4);
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    std::memcpy(wav.data() + 22, &numChannels, 2);
    std::memcpy(wav.data() + 24, &sampleRate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4);
    std::memcpy(wav.data() + 32, &blockAlign, 2);
    std::memcpy(wav.data() + 34, &bitsPerSample, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, 4);

    int16_t* pcm = reinterpret_cast<int16_t*>(wav.data() + 44);
    const float twoPiF = 2.0f * 3.14159265359f * frequencyHz;
    for (uint32_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float s = std::sin(twoPiF * t) * 0.8f;
        pcm[i] = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
    }
    return wav;
}

bool AudioEngine::StartOutput() {
#ifdef _WIN32
    if (m_outputRunning) return true;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 44100;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 44100 * 4;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    HWAVEOUT device = nullptr;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(event);
        return false;
    }
    m_waveOut = device;
    m_outputEvent = event;
    m_outputRunning = true;
    m_outputThread = std::thread([this]() { OutputLoop(); });
    return true;
#else
    return false;
#endif
}

void AudioEngine::StopOutput() {
#ifdef _WIN32
    if (!m_outputRunning) return;
    m_outputRunning = false;
    SetEvent(static_cast<HANDLE>(m_outputEvent));
    if (m_outputThread.joinable()) m_outputThread.join();
    waveOutClose(static_cast<HWAVEOUT>(m_waveOut));
    CloseHandle(static_cast<HANDLE>(m_outputEvent));
    m_waveOut = nullptr;
    m_outputEvent = nullptr;
#endif
}

// Four buffers of 1024 frames (about 23 ms each) stay queued: as the device finishes one, it is
// mixed again from the playing voices and sent back
void AudioEngine::OutputLoop() {
#ifdef _WIN32
    constexpr int kBuffers = 4;
    constexpr size_t kFrames = 1024;
    HWAVEOUT device = static_cast<HWAVEOUT>(m_waveOut);
    std::vector<int16_t> data[kBuffers];
    WAVEHDR headers[kBuffers]{};
    for (int i = 0; i < kBuffers; ++i) {
        data[i].assign(kFrames * 2, 0);
        headers[i].lpData = reinterpret_cast<LPSTR>(data[i].data());
        headers[i].dwBufferLength = static_cast<DWORD>(kFrames * 2 * sizeof(int16_t));
        waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
        headers[i].dwFlags |= WHDR_DONE; // free to fill
    }
    std::vector<float> mixed;
    while (m_outputRunning) {
        bool queued = false;
        for (int i = 0; i < kBuffers; ++i) {
            if (!(headers[i].dwFlags & WHDR_DONE)) continue;
            MixOutputBuffer(nullptr, mixed, kFrames, 44100);
            for (size_t s = 0; s < kFrames * 2; ++s) {
                data[i][s] = static_cast<int16_t>(std::clamp(mixed[s], -1.0f, 1.0f) * 32767.0f);
            }
            headers[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(device, &headers[i], sizeof(WAVEHDR));
            queued = true;
        }
        if (!queued) WaitForSingleObject(static_cast<HANDLE>(m_outputEvent), 50);
    }
    waveOutReset(device);
    for (int i = 0; i < kBuffers; ++i) waveOutUnprepareHeader(device, &headers[i], sizeof(WAVEHDR));
#endif
}

std::vector<uint8_t> AudioEngine::GenerateSurfWav(uint32_t seed, bool underwater, float durationSec, uint32_t sampleRate) {
    // Made a little longer than the loop, then the extra is crossfaded over the start so the loop
    // has no seam
    const size_t loopSamples = static_cast<size_t>(std::max(1.0f, durationSec) * sampleRate);
    const size_t fade = sampleRate / 2;
    std::vector<float> raw(loopSamples + fade);
    NoiseSource noise{ seed * 2654435761u + 1u };
    float brown = 0.0f, low = 0.0f, mid = 0.0f, hissLow = 0.0f;
    const float dt = 1.0f / static_cast<float>(sampleRate);
    // Swells: the wash rises and falls with the waves, a few periods per loop
    const float period1 = durationSec / 2.0f, period2 = durationSec / 3.0f;
    for (size_t i = 0; i < raw.size(); ++i) {
        const float t = static_cast<float>(i) * dt;
        const float white = noise.Next();
        brown = std::clamp(brown * 0.997f + white * 0.05f, -1.0f, 1.0f);
        low += (brown - low) * 0.04f;
        mid += (white - mid) * 0.12f;
        hissLow += (white - hissLow) * 0.5f;
        const float hiss = white - hissLow; // the fizz of breaking foam
        const float swell1 = std::pow(std::max(0.0f, std::sin(6.2831853f * t / period1)), 2.0f);
        const float swell2 = std::pow(std::max(0.0f, std::sin(6.2831853f * t / period2 + 1.7f)), 2.0f);
        const float swell = 0.3f + 0.7f * std::max(swell1, swell2 * 0.8f);
        if (underwater) {
            raw[i] = low * 3.0f * (0.6f + 0.4f * swell);
        } else {
            raw[i] = (low * 2.0f + mid * 0.5f) * (0.4f + 0.6f * swell) + hiss * 0.25f * swell * swell;
        }
    }
    std::vector<float> loop(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(loopSamples));
    for (size_t i = 0; i < fade; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(fade);
        loop[i] = loop[i] * w + raw[loopSamples + i] * (1.0f - w);
    }
    return EncodeMonoWav(loop, sampleRate, underwater ? 0.6f : 0.7f);
}

std::vector<uint8_t> AudioEngine::GenerateCrashWav(uint32_t seed, float durationSec, uint32_t sampleRate) {
    const size_t count = static_cast<size_t>(std::max(0.1f, durationSec) * sampleRate);
    std::vector<float> samples(count);
    NoiseSource noise{ seed * 747796405u + 7u };
    float lowpass = 0.0f, thump = 0.0f;
    const float dt = 1.0f / static_cast<float>(sampleRate);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) * dt;
        const float white = noise.Next();
        // The roar darkens as the water falls back (the filter closes), over a low thump
        const float cutoff = 0.08f + 0.6f * std::exp(-t / 0.35f);
        lowpass += (white - lowpass) * cutoff;
        thump += (white - thump) * 0.02f;
        const float envelope = std::min(t / 0.015f, 1.0f) * std::exp(-t / 0.5f);
        samples[i] = (lowpass * 0.9f + thump * 4.0f * std::exp(-t / 0.15f)) * envelope;
    }
    return EncodeMonoWav(samples, sampleRate, 0.85f);
}

std::vector<uint8_t> AudioEngine::GenerateThunderWav(uint32_t seed, float closeness, float durationSec, uint32_t sampleRate) {
    const uint32_t numSamples = static_cast<uint32_t>(std::max(1.0f, durationSec * sampleRate));
    uint32_t state = seed * 747796405u + 2891336453u;
    auto random = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    };
    closeness = std::clamp(closeness, 0.0f, 1.0f);

    // The rumble swells a few times as sound arrives from different parts of the bolt
    struct Swell { float time, width, gain; };
    Swell swells[5];
    for (Swell& s : swells) s = { 0.2f + random() * durationSec * 0.55f, 0.25f + random() * 0.9f, 0.4f + random() * 0.6f };

    std::vector<float> out(numSamples, 0.0f);
    float brown = 0.0f, low = 0.0f, lower = 0.0f, crackLow = 0.0f;
    const float dt = 1.0f / static_cast<float>(sampleRate);
    float peak = 1e-6f;
    for (uint32_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) * dt;
        const float white = random() * 2.0f - 1.0f;
        // Brown noise, then two low-pass stages: the deep rumble
        brown = std::clamp(brown * 0.998f + white * 0.06f, -1.0f, 1.0f);
        low += (brown - low) * 0.08f;
        lower += (low - lower) * 0.05f;
        float envelope = std::min(t / 0.08f, 1.0f) * std::exp(-t / (durationSec * 0.35f));
        float swell = 0.35f;
        for (const Swell& s : swells) {
            const float x = (t - s.time) / s.width;
            swell += s.gain * std::exp(-x * x);
        }
        float sample = lower * 6.0f * envelope * swell;
        // Near strikes: a sharp crack in the first fraction of a second
        crackLow += (white - crackLow) * 0.45f;
        sample += crackLow * closeness * 1.2f * std::exp(-t / 0.09f) * std::min(t / 0.004f, 1.0f);
        out[i] = sample;
        peak = std::max(peak, std::abs(sample));
    }

    const uint32_t dataSize = numSamples * sizeof(int16_t);
    const uint32_t riffSize = 36 + dataSize;
    std::vector<uint8_t> wav(44 + dataSize, 0);
    const uint32_t fmtChunkSize = 16;
    const uint16_t audioFormat = 1; // PCM
    const uint16_t numChannels = 1;
    const uint32_t byteRate = sampleRate * sizeof(int16_t);
    const uint16_t blockAlign = sizeof(int16_t);
    const uint16_t bitsPerSample = 16;
    std::memcpy(wav.data() + 0, "RIFF", 4);
    std::memcpy(wav.data() + 4, &riffSize, 4);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    std::memcpy(wav.data() + 16, &fmtChunkSize, 4);
    std::memcpy(wav.data() + 20, &audioFormat, 2);
    std::memcpy(wav.data() + 22, &numChannels, 2);
    std::memcpy(wav.data() + 24, &sampleRate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4);
    std::memcpy(wav.data() + 32, &blockAlign, 2);
    std::memcpy(wav.data() + 34, &bitsPerSample, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, 4);
    int16_t* pcm = reinterpret_cast<int16_t*>(wav.data() + 44);
    const float gain = 0.9f / peak;
    for (uint32_t i = 0; i < numSamples; ++i) {
        pcm[i] = static_cast<int16_t>(std::clamp(out[i] * gain, -1.0f, 1.0f) * 32767.0f);
    }
    return wav;
}

const AudioClip* AudioEngine::GetClip(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_clips.find(name);
    return (it != m_clips.end()) ? &it->second : nullptr;
}

void AudioEngine::SetBusVolume(const std::string& busName, float volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_buses[busName].name = busName;
    m_buses[busName].volume = std::clamp(volume, 0.0f, 2.0f);
}

float AudioEngine::GetBusVolume(const std::string& busName) const {
    auto it = m_buses.find(busName);
    return (it != m_buses.end()) ? it->second.volume : 1.0f;
}

void AudioEngine::SetBusMuted(const std::string& busName, bool muted) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_buses[busName].name = busName;
    m_buses[busName].muted = muted;
}

bool AudioEngine::IsBusMuted(const std::string& busName) const {
    auto it = m_buses.find(busName);
    return (it != m_buses.end()) ? it->second.muted : false;
}

uint32_t AudioEngine::Play2D(const std::string& clipName,
                             const std::string& busName,
                             float volume,
                             float pitch,
                             bool loop) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_clips.find(clipName) == m_clips.end()) return 0;
    ActiveVoice v;
    v.id = m_nextVoiceId++;
    v.clipName = clipName;
    v.busName = busName;
    v.volume = std::max(0.0f, volume);
    v.pitch = std::clamp(pitch, 0.1f, 4.0f);
    v.loop = loop;
    v.is3D = false;
    v.playing = true;
    m_voices.push_back(v);
    return v.id;
}

uint32_t AudioEngine::Play3D(const std::string& clipName,
                             const glm::vec3& worldPos,
                             const std::string& busName,
                             float volume,
                             float minDistance,
                             float maxDistance,
                             bool loop) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_clips.find(clipName) == m_clips.end()) return 0;
    ActiveVoice v;
    v.id = m_nextVoiceId++;
    v.clipName = clipName;
    v.busName = busName;
    v.volume = std::max(0.0f, volume);
    v.pitch = 1.0f;
    v.loop = loop;
    v.is3D = true;
    v.position = worldPos;
    v.minDistance = std::max(0.1f, minDistance);
    v.maxDistance = std::max(v.minDistance + 0.1f, maxDistance);
    v.playing = true;
    m_voices.push_back(v);
    return v.id;
}

void AudioEngine::StopVoice(uint32_t voiceId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& v : m_voices) {
        if (v.id == voiceId) v.playing = false;
    }
}

void AudioEngine::StopAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_voices.clear();
}

bool AudioEngine::IsVoicePlaying(uint32_t voiceId) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& v : m_voices) {
        if (v.id == voiceId) return v.playing;
    }
    return false;
}

void AudioEngine::SetVoiceVolume(uint32_t voiceId, float volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& v : m_voices) {
        if (v.id == voiceId) v.volume = std::max(0.0f, volume);
    }
}

void AudioEngine::SetVoicePosition(uint32_t voiceId, const glm::vec3& worldPos) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& v : m_voices) {
        if (v.id == voiceId) v.position = worldPos;
    }
}

size_t AudioEngine::GetActiveVoiceCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& v : m_voices) {
        if (v.playing) ++count;
    }
    return count;
}

void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_listenerPos = position;
    m_listenerForward = (glm::length(forward) > 1e-5f) ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    m_listenerUp = (glm::length(up) > 1e-5f) ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(m_listenerForward, m_listenerUp);
    m_listenerRight = (glm::length(right) > 1e-5f) ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
}

void AudioEngine::ApplyReverbDSP(std::vector<float>& ioStereoInterleaved,
                                 float wetMix,
                                 float roomSize,
                                 float damping) {
    wetMix = std::clamp(wetMix, 0.0f, 1.0f);
    if (wetMix <= 1e-4f || ioStereoInterleaved.size() < 2) return;

    float feedback = std::clamp(roomSize * 0.85f, 0.1f, 0.92f);
    float damp = std::clamp(damping, 0.0f, 0.95f);
    float lpStateL = 0.0f;
    float lpStateR = 0.0f;

    size_t frames = ioStereoInterleaved.size() / 2;
    for (size_t i = 0; i < frames; ++i) {
        float dryL = ioStereoInterleaved[i * 2 + 0];
        float dryR = ioStereoInterleaved[i * 2 + 1];

        size_t idxL = m_combIndex % m_combDelayL1.size();
        size_t idxR = m_combIndex % m_combDelayR1.size();

        float delayedL = m_combDelayL1[idxL];
        float delayedR = m_combDelayR1[idxR];

        lpStateL = delayedL * (1.0f - damp) + lpStateL * damp;
        lpStateR = delayedR * (1.0f - damp) + lpStateR * damp;

        m_combDelayL1[idxL] = dryL + lpStateL * feedback;
        m_combDelayR1[idxR] = dryR + lpStateR * feedback;
        ++m_combIndex;

        ioStereoInterleaved[i * 2 + 0] = glm::mix(dryL, dryL + delayedL * 0.6f, wetMix);
        ioStereoInterleaved[i * 2 + 1] = glm::mix(dryR, dryR + delayedR * 0.6f, wetMix);
    }
}

void AudioEngine::MixOutputBuffer(const Scene* scene,
                                  std::vector<float>& outStereoInterleaved,
                                  size_t frameCount,
                                  uint32_t sampleRate) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    outStereoInterleaved.assign(frameCount * 2, 0.0f);
    // Finished voices leave the list
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const ActiveVoice& v) { return !v.playing; }), m_voices.end());
    if (frameCount == 0) return;

    bool masterMuted = IsBusMuted("Master");
    float masterVol = masterMuted ? 0.0f : GetBusVolume("Master");
    if (masterVol <= 0.0f) return;

    for (auto& v : m_voices) {
        if (!v.playing) continue;
        const AudioClip* clip = GetClip(v.clipName);
        if (!clip || clip->samples.empty()) {
            v.playing = false;
            continue;
        }

        if (IsBusMuted(v.busName)) continue;
        float busVol = GetBusVolume(v.busName);

        float leftGain = v.volume * busVol * masterVol;
        float rightGain = v.volume * busVol * masterVol;

        if (v.is3D) {
            glm::vec3 toSrc = v.position - m_listenerPos;
            float dist = glm::length(toSrc);
            float atten = 1.0f;
            if (dist >= v.maxDistance) {
                atten = 0.0f;
            } else if (dist > v.minDistance) {
                atten = v.minDistance / (v.minDistance + (dist - v.minDistance));
            }
            float pan = (dist > 1e-4f) ? glm::clamp(glm::dot(toSrc / dist, m_listenerRight), -1.0f, 1.0f) : 0.0f;
            float angle = (pan + 1.0f) * 0.25f * 3.14159265359f;
            leftGain *= atten * std::cos(angle);
            rightGain *= atten * std::sin(angle);
        }

        double step = (static_cast<double>(clip->sampleRate) / static_cast<double>(std::max(1u, sampleRate))) * v.pitch;
        size_t totalFrames = clip->samples.size() / std::max<uint16_t>(1, clip->channels);

        for (size_t f = 0; f < frameCount; ++f) {
            size_t idx = static_cast<size_t>(v.sampleCursor);
            if (idx >= totalFrames) {
                if (v.loop) {
                    v.sampleCursor = std::fmod(v.sampleCursor, static_cast<double>(totalFrames));
                    idx = static_cast<size_t>(v.sampleCursor);
                } else {
                    v.playing = false;
                    break;
                }
            }

            float sampleL = clip->samples[idx * clip->channels];
            float sampleR = (clip->channels > 1) ? clip->samples[idx * clip->channels + 1] : sampleL;

            outStereoInterleaved[f * 2 + 0] += sampleL * leftGain;
            outStereoInterleaved[f * 2 + 1] += sampleR * rightGain;
            v.sampleCursor += step;
        }
    }

    // Evaluate Scene Reverb Zones if a Scene is provided
    if (scene) {
        float maxReverbWet = 0.0f;
        float roomSize = 0.65f;
        float damping = 0.4f;

        for (const auto& node : scene->GetNodes()) {
            if (!node || node->nodeType != NodeType::AudioSource) continue;
            auto audioNode = std::dynamic_pointer_cast<AudioNode>(node);
            if (audioNode && audioNode->isReverbZone) {
                float w = audioNode->EvaluateReverbZoneWeight(*scene, m_listenerPos);
                if (w > maxReverbWet) {
                    maxReverbWet = w;
                    roomSize = audioNode->reverbRoomSize;
                    damping = audioNode->reverbDamping;
                }
            }
        }

        if (maxReverbWet > 0.0f) {
            ApplyReverbDSP(outStereoInterleaved, maxReverbWet, roomSize, damping);
        }
    }
}

bool AudioEngine::TriggerHardwarePlayback(const std::string& clipNameOrPath,
                                          const std::string& busName,
                                          float leftGain,
                                          float rightGain,
                                          float pitch,
                                          bool loop,
                                          float reverbWet,
                                          float reverbRoomSize,
                                          float reverbDamping) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string key = clipNameOrPath.empty() ? "Sine_440Hz" : clipNameOrPath;
    if (m_clips.find(key) == m_clips.end()) {
        if (std::filesystem::exists(key)) {
            LoadWavFromFile(key, key);
        }
    }
    const AudioClip* clip = GetClip(key);
    if (!clip) {
        clip = GetClip("Sine_440Hz");
    }
    if (!clip || clip->samples.empty()) return false;

    if (IsBusMuted("Master") || IsBusMuted(busName)) return false;
    float masterVol = GetBusVolume("Master");
    float busVol = GetBusVolume(busName);
    float gainL = std::clamp(leftGain * masterVol * busVol, 0.0f, 2.0f);
    float gainR = std::clamp(rightGain * masterVol * busVol, 0.0f, 2.0f);

    double step = std::clamp(static_cast<double>(pitch), 0.25, 3.0);
    size_t totalInputFrames = clip->samples.size() / std::max<uint16_t>(1, clip->channels);
    size_t outFrames = static_cast<size_t>(static_cast<double>(totalInputFrames) / step);
    if (reverbWet > 0.01f) {
        outFrames += clip->sampleRate / 3; // Add tail for reverb decay
    }
    if (outFrames == 0) return false;

    std::vector<float> stereoFloat(outFrames * 2, 0.0f);
    double cursor = 0.0;
    for (size_t f = 0; f < outFrames; ++f) {
        size_t idx = static_cast<size_t>(cursor);
        if (idx < totalInputFrames) {
            float sL = clip->samples[idx * clip->channels];
            float sR = (clip->channels > 1) ? clip->samples[idx * clip->channels + 1] : sL;
            // Apply smooth envelope fade out at very end if synthesized
            float env = 1.0f;
            if (idx + 600 > totalInputFrames && totalInputFrames > 600) {
                env = static_cast<float>(totalInputFrames - idx) / 600.0f;
            }
            stereoFloat[f * 2 + 0] = sL * gainL * env;
            stereoFloat[f * 2 + 1] = sR * gainR * env;
        }
        cursor += step;
    }

    if (reverbWet > 0.005f) {
        ApplyReverbDSP(stereoFloat, reverbWet, reverbRoomSize, reverbDamping);
    }

    // Encode stereo 16-bit PCM RIFF WAVE into m_activeHardwareWavBuffer
    uint32_t sampleRate = clip->sampleRate;
    uint16_t numChannels = 2;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = static_cast<uint32_t>(outFrames * numChannels * sizeof(int16_t));
    uint32_t riffSize = 36 + dataSize;

    m_activeHardwareWavBuffer.assign(44 + dataSize, 0);
    std::memcpy(m_activeHardwareWavBuffer.data() + 0, "RIFF", 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 4, &riffSize, 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 8, "WAVE", 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 12, "fmt ", 4);

    uint32_t fmtChunkSize = 16;
    uint16_t audioFormat = 1;
    uint32_t byteRate = sampleRate * numChannels * sizeof(int16_t);
    uint16_t blockAlign = numChannels * sizeof(int16_t);

    std::memcpy(m_activeHardwareWavBuffer.data() + 16, &fmtChunkSize, 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 20, &audioFormat, 2);
    std::memcpy(m_activeHardwareWavBuffer.data() + 22, &numChannels, 2);
    std::memcpy(m_activeHardwareWavBuffer.data() + 24, &sampleRate, 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 28, &byteRate, 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 32, &blockAlign, 2);
    std::memcpy(m_activeHardwareWavBuffer.data() + 34, &bitsPerSample, 2);
    std::memcpy(m_activeHardwareWavBuffer.data() + 36, "data", 4);
    std::memcpy(m_activeHardwareWavBuffer.data() + 40, &dataSize, 4);

    int16_t* pcmOut = reinterpret_cast<int16_t*>(m_activeHardwareWavBuffer.data() + 44);
    for (size_t i = 0; i < outFrames * 2; ++i) {
        pcmOut[i] = static_cast<int16_t>(std::clamp(stereoFloat[i], -1.0f, 1.0f) * 32767.0f);
    }

#ifdef _WIN32
    DWORD flags = SND_MEMORY | SND_ASYNC | SND_NODEFAULT;
    if (loop) flags |= SND_LOOP;
    PlaySoundA(reinterpret_cast<LPCSTR>(m_activeHardwareWavBuffer.data()), NULL, flags);
#endif
    return true;
}

void AudioEngine::StopHardwarePlayback() {
#ifdef _WIN32
    PlaySoundA(NULL, NULL, 0);
#endif
    StopAll();
}

} // namespace Audio

