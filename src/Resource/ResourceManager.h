#pragma once

#include "Resource.h"
#include <map>
#include <string>
#include <memory>
#include <mutex>

namespace Resource {

class ResourceManager {
public:
    static ResourceManager& Get();

    ResourceManager() = default;
    ~ResourceManager() = default;

    // Mesh
    std::shared_ptr<MeshResource> GetOrCreateMesh(const std::string& path);
    // Texture
    std::shared_ptr<TextureResource> GetOrCreateTexture(const std::string& path, int width = 0, int height = 0, int channels = 4);
    // Material
    std::shared_ptr<MaterialResource> GetOrCreateMaterial(const std::string& nameOrPath);
    // Audio
    std::shared_ptr<AudioResource> GetOrCreateAudio(const std::string& path, float duration = 0.0f);

    // Generic lookup
    std::shared_ptr<Resource> FindResource(const std::string& path) const;
    bool HasResource(const std::string& path) const;

    // Purge resources whose only remaining reference is this cache (use_count == 1)
    size_t UnloadUnused();
    void Clear();

    size_t GetLoadedResourceCount() const;
    uint64_t GetTotalMemoryUsage() const;

private:
    mutable std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<Resource>> m_resources;
};

} // namespace Resource

