#include "TemporalUpscaler.h"
#include <algorithm>
#include <cmath>

namespace Graphics {
namespace Upscaling {

TemporalUpscaler& TemporalUpscaler::Get() {
    static TemporalUpscaler instance;
    return instance;
}

void TemporalUpscaler::Initialize() {
    m_initialized = true;
    m_stats = UpscalerStats{};
}

void TemporalUpscaler::Shutdown() {
    m_initialized = false;
}

float TemporalUpscaler::GetPresetScaleFactor(UpscalerPreset preset) {
    switch (preset) {
        case UpscalerPreset::Native: return 1.0f;
        case UpscalerPreset::Quality: return 0.667f;
        case UpscalerPreset::Balanced: return 0.588f;
        case UpscalerPreset::Performance: return 0.500f;
        case UpscalerPreset::UltraPerformance: return 0.333f;
        default: return 1.0f;
    }
}

void TemporalUpscaler::CalculateRenderDimensions(
    uint32_t displayWidth, uint32_t displayHeight,
    uint32_t& outRenderWidth, uint32_t& outRenderHeight) const
{
    if (!m_settings.enableUpscaling || m_settings.preset == UpscalerPreset::Native) {
        outRenderWidth = displayWidth;
        outRenderHeight = displayHeight;
        return;
    }

    float factor = GetPresetScaleFactor(m_settings.preset);
    uint32_t w = static_cast<uint32_t>(std::round(displayWidth * factor));
    uint32_t h = static_cast<uint32_t>(std::round(displayHeight * factor));

    // Align to multiple of 2 for clean mipmapping and quad alignment
    outRenderWidth = std::max(64u, (w + 1u) & ~1u);
    outRenderHeight = std::max(64u, (h + 1u) & ~1u);
}

// Low-discrepancy Halton radical inverse
static float HaltonSequence(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

glm::vec2 TemporalUpscaler::GetJitterPhase(uint32_t frameIndex, uint32_t renderWidth, uint32_t renderHeight) const {
    if (renderWidth == 0 || renderHeight == 0) {
        return glm::vec2(0.0f);
    }

    // Standard 16-phase Halton(2, 3) sequence
    const uint32_t kPhaseCount = 16;
    uint32_t phase = (frameIndex % kPhaseCount) + 1;

    float jx = HaltonSequence(phase, 2) - 0.5f;
    float jy = HaltonSequence(phase, 3) - 0.5f;

    // Convert pixel offset to NDC projection space [-1, 1]
    return glm::vec2((jx * 2.0f) / static_cast<float>(renderWidth),
                     (jy * 2.0f) / static_cast<float>(renderHeight));
}

void TemporalUpscaler::UpdateStats(uint32_t displayWidth, uint32_t displayHeight, uint32_t frameIndex) {
    uint32_t rw = displayWidth;
    uint32_t rh = displayHeight;
    CalculateRenderDimensions(displayWidth, displayHeight, rw, rh);

    m_stats.activeMode = m_settings.enableUpscaling ? m_settings.mode : UpscalerMode::Native_TAA;
    m_stats.activePreset = m_settings.preset;
    m_stats.displayResolution = glm::uvec2(displayWidth, displayHeight);
    m_stats.renderResolution = glm::uvec2(rw, rh);

    float scale = (displayWidth > 0) ? (static_cast<float>(rw) / static_cast<float>(displayWidth)) : 1.0f;
    m_stats.renderScaleFactor = scale;

    float pixelAreaRatio = (scale * scale);
    if (pixelAreaRatio > 0.0f && pixelAreaRatio < 0.999f) {
        m_stats.estimatedGpuSpeedupPercent = ((1.0f / pixelAreaRatio) - 1.0f) * 100.0f;
    } else {
        m_stats.estimatedGpuSpeedupPercent = 0.0f;
    }

    m_stats.jitterPhaseIndex = frameIndex % 16;
    m_stats.currentJitterOffset = GetJitterPhase(frameIndex, rw, rh);
}

const char* TemporalUpscaler::GetModeName(UpscalerMode mode) {
    switch (mode) {
        case UpscalerMode::Native_TAA: return "Native Resolution (TAA)";
        case UpscalerMode::AMD_FSR_3: return "AMD FidelityFX Super Resolution 3.1 (FSR 3)";
        case UpscalerMode::NVIDIA_DLSS_3_5: return "NVIDIA Deep Learning Super Sampling 3.5 (DLSS)";
        case UpscalerMode::Intel_XeSS: return "Intel Xe Super Sampling (XeSS)";
        default: return "Unknown";
    }
}

const char* TemporalUpscaler::GetPresetName(UpscalerPreset preset) {
    switch (preset) {
        case UpscalerPreset::Native: return "Native (1.0x)";
        case UpscalerPreset::Quality: return "Quality (1.5x Scale)";
        case UpscalerPreset::Balanced: return "Balanced (1.7x Scale)";
        case UpscalerPreset::Performance: return "Performance (2.0x Scale)";
        case UpscalerPreset::UltraPerformance: return "Ultra Performance (3.0x Scale)";
        default: return "Custom";
    }
}

} // namespace Upscaling
} // namespace Graphics

