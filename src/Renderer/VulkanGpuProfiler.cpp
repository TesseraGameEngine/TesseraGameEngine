#include "VulkanGpuProfiler.h"
#include "VulkanContext.h"
#include <algorithm>

VulkanGpuProfiler* VulkanGpuProfiler::s_active = nullptr;

void VulkanGpuProfiler::Init() {
    auto& ctx = VulkanContext::Get();
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.GetPhysicalDevice(), &props);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.GetPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.GetPhysicalDevice(), &familyCount, families.data());
    const uint32_t family = ctx.GetQueueIndices().graphicsFamily.value_or(0);
    const uint32_t validBits = family < familyCount ? families[family].timestampValidBits : 0;
    m_supported = validBits > 0 && props.limits.timestampPeriod > 0.0f;
    if (!m_supported) return;
    m_periodNs = props.limits.timestampPeriod;
    m_validMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);
    for (Frame& frame : m_frames) {
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kMaxScopes * 2;
        if (vkCreateQueryPool(ctx.GetDevice(), &info, nullptr, &frame.pool) != VK_SUCCESS) {
            m_supported = false;
            return;
        }
    }
    s_active = this;
}

void VulkanGpuProfiler::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (Frame& frame : m_frames) {
        if (frame.pool) vkDestroyQueryPool(device, frame.pool, nullptr);
        frame = Frame{};
    }
    if (s_active == this) s_active = nullptr;
    m_current = nullptr;
}

void VulkanGpuProfiler::BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    m_current = nullptr;
    m_open.clear();
    if (!m_supported || !m_enabled) return;
    Frame& frame = m_frames[frameIndex % 2];
    if (frame.recorded) ReadBack(frame);
    vkCmdResetQueryPool(cmd, frame.pool, 0, kMaxScopes * 2);
    frame.scopes.clear();
    frame.queries = 0;
    frame.recorded = false;
    m_current = &frame;
}

void VulkanGpuProfiler::EndFrame(VkCommandBuffer cmd) {
    if (!m_current) return;
    while (!m_open.empty()) End(cmd); // scopes left open close here
    m_current->recorded = m_current->queries > 0;
    m_current = nullptr;
}

void VulkanGpuProfiler::Begin(VkCommandBuffer cmd, const char* name) {
    if (!m_current) return;
    if (m_current->queries + 2 > kMaxScopes * 2) {
        m_open.push_back(UINT32_MAX); // out of queries: not timed
        return;
    }
    Scope scope;
    scope.name = name;
    scope.depth = static_cast<int>(m_open.size());
    scope.beginQuery = m_current->queries++;
    scope.endQuery = UINT32_MAX;
    m_open.push_back(static_cast<uint32_t>(m_current->scopes.size()));
    m_current->scopes.push_back(scope);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_current->pool, scope.beginQuery);
}

void VulkanGpuProfiler::End(VkCommandBuffer cmd) {
    if (!m_current || m_open.empty()) return;
    const uint32_t index = m_open.back();
    m_open.pop_back();
    if (index == UINT32_MAX) return;
    Scope& scope = m_current->scopes[index];
    scope.endQuery = m_current->queries++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_current->pool, scope.endQuery);
}

void VulkanGpuProfiler::ReadBack(Frame& frame) {
    frame.recorded = false;
    if (frame.queries == 0) return;
    // Value and availability per query; the frame's fence has been waited on, so these are ready
    std::vector<uint64_t> data(static_cast<size_t>(frame.queries) * 2, 0);
    const VkResult result = vkGetQueryPoolResults(VulkanContext::Get().GetDevice(), frame.pool, 0, frame.queries, data.size() * sizeof(uint64_t),
                                                  data.data(), 2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY) return;
    std::vector<Timing> timings;
    uint64_t first = ~0ull, last = 0;
    for (const Scope& scope : frame.scopes) {
        if (scope.endQuery == UINT32_MAX) continue;
        const uint64_t* b = &data[static_cast<size_t>(scope.beginQuery) * 2];
        const uint64_t* e = &data[static_cast<size_t>(scope.endQuery) * 2];
        if (b[1] == 0 || e[1] == 0) continue; // not available
        const uint64_t begin = b[0] & m_validMask;
        const uint64_t end = e[0] & m_validMask;
        const uint64_t ticks = end >= begin ? end - begin : 0;
        Timing t;
        t.name = scope.name;
        t.depth = scope.depth;
        t.ms = static_cast<float>(static_cast<double>(ticks) * m_periodNs * 1e-6);
        // Smoothed against the same scope last time: at the same place in the list (names repeat
        // across views), else the first with its name and depth
        t.averageMs = t.ms;
        const size_t k = timings.size();
        const Timing* previous = nullptr;
        if (k < m_timings.size() && m_timings[k].name == t.name && m_timings[k].depth == t.depth) previous = &m_timings[k];
        for (size_t i = 0; !previous && i < m_timings.size(); ++i) {
            if (m_timings[i].name == t.name && m_timings[i].depth == t.depth) previous = &m_timings[i];
        }
        if (previous) t.averageMs = previous->averageMs + (t.ms - previous->averageMs) * 0.05f;
        timings.push_back(t);
        first = std::min(first, begin);
        last = std::max(last, end);
    }
    if (timings.empty()) return;
    m_timings = std::move(timings);
    m_frameMs = last > first ? static_cast<float>(static_cast<double>(last - first) * m_periodNs * 1e-6) : 0.0f;
    m_averageFrameMs = m_averageFrameMs <= 0.0f ? m_frameMs : m_averageFrameMs + (m_frameMs - m_averageFrameMs) * 0.05f;
    m_history.push_back(m_frameMs);
    if (m_history.size() > 240) m_history.erase(m_history.begin());
}
