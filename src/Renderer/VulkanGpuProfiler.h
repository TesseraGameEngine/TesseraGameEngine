#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

// Real GPU timings per pass, from Vulkan timestamp queries (like Unreal's "stat gpu" and Godot's
// visual profiler). Scopes can nest and can be opened anywhere a command buffer is recorded:
//     GpuProfileScope scope(cmd, "Shadows");
// Each frame in flight has its own query pool; a frame's timings are read back when its slot is
// recorded again (two frames later), so reading never waits for the GPU.
class VulkanGpuProfiler {
public:
    static constexpr uint32_t kMaxScopes = 128;

    struct Timing {
        std::string name;
        int depth{0};       // nesting level (0 = top)
        float ms{0.0f};     // this frame
        float averageMs{0.0f}; // smoothed over about a second
    };

    void Init();
    void Cleanup();
    bool Supported() const { return m_supported; }

    // At the start of a frame's command buffer (outside render passes): reads the timings this
    // frame slot recorded last time, then resets its queries
    void BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex);
    void EndFrame(VkCommandBuffer cmd);

    void Begin(VkCommandBuffer cmd, const char* name);
    void End(VkCommandBuffer cmd);

    // The last frame read back, in the order the scopes opened
    const std::vector<Timing>& Timings() const { return m_timings; }
    // The whole frame's GPU time (first scope opened to last closed)
    float FrameMs() const { return m_frameMs; }
    float AverageFrameMs() const { return m_averageFrameMs; }
    const std::vector<float>& FrameHistory() const { return m_history; } // the last 240 frames (ms)
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool Enabled() const { return m_enabled; }

    // The profiler the renderer's scopes write to (null when there is none)
    static VulkanGpuProfiler* Active() { return s_active; }

private:
    struct Scope {
        std::string name;
        int depth{0};
        uint32_t beginQuery{0};
        uint32_t endQuery{0};
    };
    struct Frame {
        VkQueryPool pool{VK_NULL_HANDLE};
        std::vector<Scope> scopes;
        uint32_t queries{0};
        bool recorded{false};
    };
    void ReadBack(Frame& frame);

    static VulkanGpuProfiler* s_active;
    Frame m_frames[2];
    Frame* m_current{nullptr};
    std::vector<uint32_t> m_open; // indices into the current frame's scopes
    std::vector<Timing> m_timings;
    std::vector<float> m_history;
    float m_frameMs{0.0f};
    float m_averageFrameMs{0.0f};
    double m_periodNs{1.0};
    uint64_t m_validMask{~0ull};
    bool m_supported{false};
    bool m_enabled{true};
};

// Opens a profiler scope for the rest of the C++ scope (does nothing without a profiler)
class GpuProfileScope {
public:
    GpuProfileScope(VkCommandBuffer cmd, const char* name) : m_cmd(cmd) {
        if (VulkanGpuProfiler* profiler = VulkanGpuProfiler::Active()) profiler->Begin(cmd, name);
    }
    ~GpuProfileScope() {
        if (VulkanGpuProfiler* profiler = VulkanGpuProfiler::Active()) profiler->End(m_cmd);
    }
    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    VkCommandBuffer m_cmd;
};
