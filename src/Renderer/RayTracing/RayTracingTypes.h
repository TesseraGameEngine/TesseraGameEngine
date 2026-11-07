#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics {
namespace RayTracing {

using RayTracingMeshHandle = uint32_t;
constexpr RayTracingMeshHandle kInvalidMeshHandle = 0;

enum class RayTracingBackendType {
    None = 0,
    Vulkan_KHR = 1,
    Metal_MPS = 2,
    Hybrid_Software = 3
};

struct RayTracingInstance {
    uint32_t nodeId{0};
    RayTracingMeshHandle meshHandle{kInvalidMeshHandle};
    glm::mat4 transform{1.0f};
    uint32_t customId{0};
    uint8_t mask{0xFF};
    bool isOpaque{true};
};

struct RayTracingSettings {
    bool enableHardwareRT{false};      // Master toggle for hardware ray tracing
    bool enableRTAO{false};            // Ray-Traced Ambient Occlusion
    bool enableRTR{false};             // Ray-Traced Reflections (off-screen mirror reflections)
    bool enableRTS{false};             // Ray-Traced Shadows (true contact penumbras)
    bool enableRTGI{false};            // Ray-Traced Global Illumination (multi-bounce diffuse)

    float rtaoRadius{3.0f};
    int rtaoSamples{4};

    int rtrMaxBounces{2};
    float rtrRoughnessThreshold{0.65f}; // Surfaces with roughness <= threshold receive ray-traced reflections

    int rtsLightSamples{2};
    float rtsPenumbraScale{1.0f};

    int rtgiBounces{2};
    float rtgiIntensity{2.5f};
};

struct RayTracingStats {
    bool supported{false};
    RayTracingBackendType activeBackend{RayTracingBackendType::None};
    std::string deviceName{""};
    uint32_t totalBLASCount{0};
    uint32_t activeTLASInstances{0};
    uint64_t bvhMemoryBytes{0};
    float lastBuildTimeMs{0.0f};
};

} // namespace RayTracing
} // namespace Graphics

