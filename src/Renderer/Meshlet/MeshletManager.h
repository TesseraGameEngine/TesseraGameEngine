#pragma once

#include "MeshletTypes.h"
#include "MeshletBuilder.h"
#include "Scene/Scene.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <vulkan/vulkan.h>

namespace Graphics {
namespace Meshlet {

class MeshletManager {
public:
    static MeshletManager& Get();

    // Initializes meshlet system and probes Vulkan device for VK_EXT_mesh_shader
    void Initialize(VkPhysicalDevice physicalDevice = VK_NULL_HANDLE, VkDevice device = VK_NULL_HANDLE);
    void Shutdown();

    // Registers or retrieves a cached meshlet model by unique key
    const MeshletModel& RegisterMesh(const std::string& key, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    const MeshletModel* GetMeshletModel(const std::string& key) const;

    // Evaluates all scene instances each frame: performs cluster frustum and backface cone culling
    void SyncScene(const Scene& scene, const glm::vec3& cameraPos = glm::vec3(0.0f, 0.0f, 5.0f), const glm::mat4& viewProj = glm::mat4(1.0f));

    MeshletSettings& GetSettings() { return m_settings; }
    const MeshletSettings& GetSettings() const { return m_settings; }

    const MeshletStats& GetStats() const { return m_stats; }

    void ClearCache();

private:
    MeshletManager() = default;
    ~MeshletManager() = default;

    MeshletSettings m_settings;
    MeshletStats m_stats;
    std::unordered_map<std::string, MeshletModel> m_modelCache;

    VkDevice m_device{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    bool m_initialized{false};
};

} // namespace Meshlet
} // namespace Graphics
