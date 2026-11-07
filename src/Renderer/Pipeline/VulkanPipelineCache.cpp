#include "VulkanPipelineCache.h"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace Graphics {
namespace Pipeline {

VulkanPipelineCache& VulkanPipelineCache::Get() {
    static VulkanPipelineCache instance;
    return instance;
}

void VulkanPipelineCache::Initialize(VkDevice device, const std::string& diskPath) {
    m_device = device;
    m_diskPath = diskPath;
    m_stats = PipelineCacheStats{};
    m_stats.cacheFilePath = diskPath;

    if (m_device == VK_NULL_HANDLE) {
        m_initialized = true;
        return;
    }

    std::vector<char> initialData;
    if (!diskPath.empty() && fs::exists(diskPath)) {
        std::ifstream file(diskPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            if (size > 0) {
                initialData.resize(static_cast<size_t>(size));
                file.seekg(0, std::ios::beg);
                file.read(initialData.data(), size);
            }
        }
    }

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (!initialData.empty()) {
        createInfo.initialDataSize = initialData.size();
        createInfo.pInitialData = initialData.data();
    }

    VkResult result = vkCreatePipelineCache(m_device, &createInfo, nullptr, &m_pipelineCache);
    if (result == VK_SUCCESS) {
        m_stats.cacheLoaded = !initialData.empty();
        m_stats.cacheSizeBytes = initialData.size();
    } else {
        // Fallback: create fresh cache without initial data
        createInfo.initialDataSize = 0;
        createInfo.pInitialData = nullptr;
        vkCreatePipelineCache(m_device, &createInfo, nullptr, &m_pipelineCache);
        m_stats.cacheLoaded = false;
        m_stats.cacheSizeBytes = 0;
    }

    m_initialized = true;
}

void VulkanPipelineCache::Shutdown() {
    if (m_device != VK_NULL_HANDLE && m_pipelineCache != VK_NULL_HANDLE) {
        SaveToDisk();
        vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
        m_pipelineCache = VK_NULL_HANDLE;
    }
    m_initialized = false;
}

bool VulkanPipelineCache::SaveToDisk(const std::string& diskPath) {
    if (m_device == VK_NULL_HANDLE || m_pipelineCache == VK_NULL_HANDLE) {
        return false;
    }

    std::string targetPath = diskPath.empty() ? m_diskPath : diskPath;
    if (targetPath.empty()) return false;

    size_t dataSize = 0;
    VkResult res = vkGetPipelineCacheData(m_device, m_pipelineCache, &dataSize, nullptr);
    if (res != VK_SUCCESS || dataSize == 0) {
        return false;
    }

    std::vector<char> buffer(dataSize);
    res = vkGetPipelineCacheData(m_device, m_pipelineCache, &dataSize, buffer.data());
    if (res != VK_SUCCESS) {
        return false;
    }

    try {
        fs::path p(targetPath);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
        std::ofstream file(targetPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(buffer.data(), static_cast<std::streamsize>(dataSize));
        m_stats.cacheSizeBytes = dataSize;
        return true;
    } catch (...) {
        return false;
    }
}

void VulkanPipelineCache::RecordCompilation(bool wasCacheHit, float compileDurationMs) {
    m_stats.totalPipelinesCompiled++;
    if (wasCacheHit) {
        m_stats.cacheHits++;
    } else {
        m_stats.cacheMisses++;
    }
    m_stats.totalCompileTimeMs += compileDurationMs;
}

void VulkanPipelineCache::ResetStats() {
    m_stats.totalPipelinesCompiled = 0;
    m_stats.cacheHits = 0;
    m_stats.cacheMisses = 0;
    m_stats.totalCompileTimeMs = 0.0f;
}

} // namespace Pipeline
} // namespace Graphics

