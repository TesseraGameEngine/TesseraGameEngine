#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics {
namespace Upscaling {

enum class UpscalerMode {
    Native_TAA = 0,
    AMD_FSR_3 = 1,
    NVIDIA_DLSS_3_5 = 2,
    Intel_XeSS = 3
};

enum class UpscalerPreset {
    Native = 0,           // 1.0x (100% resolution scale)
    Quality = 1,          // 1.5x upscaling (66.7% resolution scale)
    Balanced = 2,         // 1.7x upscaling (58.8% resolution scale)
    Performance = 3,      // 2.0x upscaling (50.0% resolution scale)
    UltraPerformance = 4  // 3.0x upscaling (33.3% resolution scale)
};

struct UpscalerSettings {
    bool enableUpscaling{false};
    UpscalerMode mode{UpscalerMode::AMD_FSR_3};
    UpscalerPreset preset{UpscalerPreset::Quality};
    float sharpness{0.65f};                   // RCAS (Robust Contrast-Adaptive Sharpening) 0.0 .. 1.0
    bool enableReactiveMask{true};            // Masks particles, transparents, and rapid motion
    bool enableMotionVectorJitterCancellation{true};
};

struct UpscalerStats {
    UpscalerMode activeMode{UpscalerMode::Native_TAA};
    UpscalerPreset activePreset{UpscalerPreset::Quality};
    glm::uvec2 displayResolution{1920, 1080};
    glm::uvec2 renderResolution{1280, 720};
    float renderScaleFactor{0.667f};
    float estimatedGpuSpeedupPercent{0.0f};
    uint32_t jitterPhaseIndex{0};
    glm::vec2 currentJitterOffset{0.0f};
};

class TemporalUpscaler {
public:
    static TemporalUpscaler& Get();

    void Initialize();
    void Shutdown();

    // Calculates internal render resolution from target display resolution and preset
    void CalculateRenderDimensions(uint32_t displayWidth, uint32_t displayHeight,
                                  uint32_t& outRenderWidth, uint32_t& outRenderHeight) const;

    // Calculates sub-pixel jitter in NDC coordinates for current frame
    glm::vec2 GetJitterPhase(uint32_t frameIndex, uint32_t renderWidth, uint32_t renderHeight) const;

    // Returns resolution scale multiplier for a given preset
    static float GetPresetScaleFactor(UpscalerPreset preset);

    // Human-readable names
    static const char* GetModeName(UpscalerMode mode);
    static const char* GetPresetName(UpscalerPreset preset);

    UpscalerSettings& GetSettings() { return m_settings; }
    const UpscalerSettings& GetSettings() const { return m_settings; }

    const UpscalerStats& GetStats() const { return m_stats; }
    void UpdateStats(uint32_t displayWidth, uint32_t displayHeight, uint32_t frameIndex);

private:
    TemporalUpscaler() = default;
    ~TemporalUpscaler() = default;

    UpscalerSettings m_settings;
    UpscalerStats m_stats;
    bool m_initialized{false};
};

} // namespace Upscaling
} // namespace Graphics

