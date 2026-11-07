#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Graphics {
namespace Pipeline {

struct PipelineCacheStats {
    bool cacheLoaded{false};
    uint32_t totalPipelinesCompiled{0};
    uint32_t cacheHits{0};
    uint32_t cacheMisses{0};
    float totalCompileTimeMs{0.0f};
    size_t cacheSizeBytes{0};
    std::string cacheFilePath{""};
};

class VulkanPipelineCache {
public:
    static VulkanPipelineCache& Get();

    // Initializes or creates the VkPipelineCache with optional disk cache deserialization
    void Initialize(VkDevice device, const std::string& diskPath = "cache/pipeline_cache.bin");
    void Shutdown();

    // Gets the active VkPipelineCache handle (returns VK_NULL_HANDLE if disabled/uninitialized)
    VkPipelineCache GetCache() const { return m_pipelineCache; }

    // Serializes the current pipeline cache blob to disk
    bool SaveToDisk(const std::string& diskPath = "");

    // Records a pipeline compilation metric
    void RecordCompilation(bool wasCacheHit, float compileDurationMs);

    const PipelineCacheStats& GetStats() const { return m_stats; }
    void ResetStats();

private:
    VulkanPipelineCache() = default;
    ~VulkanPipelineCache() = default;

    VkDevice m_device{VK_NULL_HANDLE};
    VkPipelineCache m_pipelineCache{VK_NULL_HANDLE};
    std::string m_diskPath{"cache/pipeline_cache.bin"};
    PipelineCacheStats m_stats;
    bool m_initialized{false};
};

} // namespace Pipeline
} // namespace Graphics

