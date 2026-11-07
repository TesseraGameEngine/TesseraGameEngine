#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <glm/glm.hpp>

class Scene;
class Node;
class VulkanPipeline;

namespace Video {

enum class VideoChannelPreset : int {
    ExternalFileOrAtlas = 0,
    CinemaFilmReel      = 1,
    TVNewsBroadcast     = 2,
    SecurityCCTVFeed    = 3,
    SmartphoneMobileOS  = 4,
    RetroCRTColorBars   = 5,
    SciFiRadarTelemetry = 6
};

inline const char* VideoChannelPresetToString(int preset) {
    switch (static_cast<VideoChannelPreset>(preset)) {
        case VideoChannelPreset::ExternalFileOrAtlas: return "External Video File / GIF / Atlas / Sequence";
        case VideoChannelPreset::CinemaFilmReel:      return "Ch 1: Cinema 35mm Countdown & Feature Reel";
        case VideoChannelPreset::TVNewsBroadcast:     return "Ch 2: Live TV News & Weather Broadcast";
        case VideoChannelPreset::SecurityCCTVFeed:    return "Ch 3: CCTV Security Camera Surveillance";
        case VideoChannelPreset::SmartphoneMobileOS:  return "Ch 4: Smartphone Mobile Call & Feed (9:16)";
        case VideoChannelPreset::RetroCRTColorBars:   return "Ch 5: Retro CRT Color Bars & VHS Static";
        case VideoChannelPreset::SciFiRadarTelemetry: return "Ch 6: Sci-Fi Radar & Oscilloscope Telemetry";
        default:                                      return "Ch 1: Cinema 35mm Countdown & Feature Reel";
    }
}

struct DecodedVideoClip {
    std::string sourcePath;
    uint32_t width{0};
    uint32_t height{0};
    int frameCount{0};
    float fps{24.0f};
    std::vector<std::vector<uint8_t>> frames; // RGBA8 per frame
    bool valid{false};
};

struct VideoRuntimeState {
    uint32_t width{256};
    uint32_t height{144};
    std::vector<uint8_t> rgbaPixels;
    glm::vec3 averageColor{0.2f, 0.25f, 0.3f};
    int lastRenderedFrameIndex{-1};
    int lastPreset{-1};
    std::string lastSourcePath;
    double lastUploadTime{0.0};
    bool dirty{true};
    uint32_t activeAudioVoice{0};
};

struct CutawayVideoState {
    bool active{false};
    int channelPreset{1};
    std::string sourcePath{""};
    float currentTime{0.0f};
    float duration{8.0f};
    bool skippable{true};
    bool pictureInPicture{false}; // false = full-screen cutaway, true = corner PiP transmission
    std::string title{"INCOMING VIDEO TRANSMISSION"};
    uint32_t width{256};
    uint32_t height{144};
    std::vector<uint8_t> rgbaPixels;
};

class VideoSystem {
public:
    static VideoSystem& Get();

    // Advances all active 3D video screen nodes (TVs, Cinema screens, Phones) and 2D UIVideoNodes
    void Update(Scene& scene, float dt, bool isPlayMode);

    using TextureUploadCallback = void(*)(const std::string& key, const unsigned char* rgba, uint32_t width, uint32_t height, void* userData);
    void UploadTextures(Scene& scene, double currentTimeSec, TextureUploadCallback callback, void* userData);

    // Renders a procedural or decoded RGBA8 frame for any node or cutaway
    static void RenderFrameRGBA(int channelPreset,
                                const std::string& sourcePath,
                                float timeSec,
                                float fps,
                                int atlasCols,
                                int atlasRows,
                                float scanlineStrength,
                                uint32_t outWidth,
                                uint32_t outHeight,
                                std::vector<uint8_t>& outRgba,
                                glm::vec3& outAverageColor);

    // Returns the runtime texture key for a video-enabled node ("@video_<id>")
    static std::string GetRuntimeTextureKey(uint32_t nodeId);

    // Returns the current average RGB color of a video screen node (used for dynamic VXGI / room glow)
    glm::vec3 GetAverageScreenColor(uint32_t nodeId) const;

    // Access the latest CPU RGBA8 frame buffer for a node (used by 2D UIVideoNode software/ImGui drawing)
    const VideoRuntimeState* GetNodeVideoState(uint32_t nodeId) const;

    // Full-screen or Picture-in-Picture cutaway video controls
    void StartCutaway(int channelPreset, const std::string& sourcePath, float durationSec = 8.0f,
                      bool skippable = true, bool pictureInPicture = false,
                      const std::string& title = "VIDEO CUTAWAY");
    void StopCutaway();
    bool SkipCutaway();
    bool IsCutawayPlaying() const { return m_cutaway.active; }
    const CutawayVideoState& GetCutawayState() const { return m_cutaway; }

    // Decodes an animated GIF, sprite-sheet atlas, frame directory, or video file (via ffmpeg if available)
    const DecodedVideoClip* GetOrDecodeClip(const std::string& path, int atlasCols, int atlasRows, float defaultFps);
    void ClearCache();

private:
    VideoSystem() = default;
    std::map<uint32_t, VideoRuntimeState> m_nodeStates;
    std::map<std::string, DecodedVideoClip> m_clipCache;
    CutawayVideoState m_cutaway;
};

} // namespace Video

