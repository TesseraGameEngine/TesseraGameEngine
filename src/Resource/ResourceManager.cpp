#include "ResourceManager.h"

namespace Resource {

ResourceManager& ResourceManager::Get() {
    static ResourceManager instance;
    return instance;
}

std::shared_ptr<MeshResource> ResourceManager::GetOrCreateMesh(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(path);
    if (it != m_resources.end()) {
        auto mesh = std::dynamic_pointer_cast<MeshResource>(it->second);
        if (mesh) return mesh;
    }

    auto mesh = std::make_shared<MeshResource>();
    mesh->path = path;
    mesh->name = path.substr(path.find_last_of("/\\") + 1);
    mesh->isLoaded = true;
    m_resources[path] = mesh;
    return mesh;
}

std::shared_ptr<TextureResource> ResourceManager::GetOrCreateTexture(const std::string& path, int width, int height, int channels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(path);
    if (it != m_resources.end()) {
        auto tex = std::dynamic_pointer_cast<TextureResource>(it->second);
        if (tex) return tex;
    }

    auto tex = std::make_shared<TextureResource>();
    tex->path = path;
    tex->name = path.substr(path.find_last_of("/\\") + 1);
    tex->width = width;
    tex->height = height;
    tex->channels = channels;
    tex->isLoaded = true;
    m_resources[path] = tex;
    return tex;
}

std::shared_ptr<MaterialResource> ResourceManager::GetOrCreateMaterial(const std::string& nameOrPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(nameOrPath);
    if (it != m_resources.end()) {
        auto mat = std::dynamic_pointer_cast<MaterialResource>(it->second);
        if (mat) return mat;
    }

    auto mat = std::make_shared<MaterialResource>();
    mat->path = nameOrPath;
    mat->name = nameOrPath;
    mat->isLoaded = true;
    m_resources[nameOrPath] = mat;
    return mat;
}

std::shared_ptr<AudioResource> ResourceManager::GetOrCreateAudio(const std::string& path, float duration) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(path);
    if (it != m_resources.end()) {
        auto aud = std::dynamic_pointer_cast<AudioResource>(it->second);
        if (aud) return aud;
    }

    auto aud = std::make_shared<AudioResource>();
    aud->path = path;
    aud->name = path.substr(path.find_last_of("/\\") + 1);
    aud->durationSeconds = duration;
    aud->isLoaded = true;
    m_resources[path] = aud;
    return aud;
}

std::shared_ptr<Resource> ResourceManager::FindResource(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(path);
    if (it != m_resources.end()) {
        return it->second;
    }
    return nullptr;
}

bool ResourceManager::HasResource(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_resources.find(path) != m_resources.end();
}

size_t ResourceManager::UnloadUnused() {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t unloaded = 0;
    for (auto it = m_resources.begin(); it != m_resources.end();) {
        // If the only reference is the ResourceManager's internal map
        if (it->second.use_count() <= 1) {
            it = m_resources.erase(it);
            ++unloaded;
        } else {
            ++it;
        }
    }
    return unloaded;
}

void ResourceManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_resources.clear();
}

size_t ResourceManager::GetLoadedResourceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_resources.size();
}

uint64_t ResourceManager::GetTotalMemoryUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& kv : m_resources) {
        if (kv.second) {
            total += kv.second->CalculateMemoryUsage();
        }
    }
    return total;
}

} // namespace Resource

