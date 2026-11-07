#include "MeshletManager.h"
#include "Scene/Mesh.h"
#include <iostream>
#include <cstring>

namespace Graphics {
namespace Meshlet {

MeshletManager& MeshletManager::Get() {
    static MeshletManager instance;
    return instance;
}

void MeshletManager::Initialize(VkPhysicalDevice physicalDevice, VkDevice device) {
    m_physicalDevice = physicalDevice;
    m_device = device;
    m_stats = MeshletStats{};

    if (m_physicalDevice != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        m_stats.deviceName = props.deviceName;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, extensions.data());

        for (const auto& ext : extensions) {
            if (std::strcmp(ext.extensionName, "VK_EXT_mesh_shader") == 0 ||
                std::strcmp(ext.extensionName, "VK_NV_mesh_shader") == 0) {
                m_stats.extensionSupported = true;
                break;
            }
        }
    } else {
        m_stats.deviceName = "CPU Cluster Virtualization";
    }

    m_initialized = true;
}

void MeshletManager::Shutdown() {
    ClearCache();
    m_initialized = false;
}

void MeshletManager::ClearCache() {
    m_modelCache.clear();
    m_stats.totalMeshletsActive = 0;
    m_stats.gpuMemoryBytes = 0;
}

const MeshletModel& MeshletManager::RegisterMesh(
    const std::string& key,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    auto it = m_modelCache.find(key);
    if (it != m_modelCache.end()) {
        return it->second;
    }

    MeshletModel model = MeshletBuilder::BuildMeshlets(vertices, indices);
    m_modelCache[key] = std::move(model);

    // Recompute total memory
    uint64_t totalMem = 0;
    for (const auto& pair : m_modelCache) {
        totalMem += pair.second.GetMemoryBytes();
    }
    m_stats.gpuMemoryBytes = totalMem;

    return m_modelCache[key];
}

const MeshletModel* MeshletManager::GetMeshletModel(const std::string& key) const {
    auto it = m_modelCache.find(key);
    if (it != m_modelCache.end()) {
        return &it->second;
    }
    return nullptr;
}

void MeshletManager::SyncScene(const Scene& scene, const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    uint32_t totalActive = 0;
    uint32_t totalVisible = 0;
    uint32_t totalCulled = 0;

    for (const auto& node : scene.GetNodes()) {
        if (!node || node->IsUINode() || node->nodeType != NodeType::Mesh) continue;

        std::string meshKey;
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        if (node->type == ShapeType::Custom) {
            meshKey = node->customMeshName.empty() ? "custom_node_" + std::to_string(node->id) : node->customMeshName;
        } else {
            meshKey = "shape_" + std::to_string(static_cast<int>(node->type));
        }

        const MeshletModel* model = GetMeshletModel(meshKey);
        if (!model) {
            // Generate standard procedural geometry
            MeshData meshData;
            switch (node->type) {
                case ShapeType::Cube: meshData = MeshGenerator::CreateCube(); break;
                case ShapeType::Sphere: meshData = MeshGenerator::CreateSphere(); break;
                case ShapeType::Plane: meshData = MeshGenerator::CreatePlane(); break;
                case ShapeType::Cylinder: meshData = MeshGenerator::CreateCylinder(); break;
                default: meshData = MeshGenerator::CreateCube(); break;
            }
            if (!meshData.vertices.empty()) {
                const auto& registered = RegisterMesh(meshKey, meshData.vertices, meshData.indices);
                model = &registered;
            }
        }

        if (model && !model->meshlets.empty()) {
            glm::mat4 world = node->GetWorldTransformMatrix(scene);
            MeshletCullResult cull = MeshletBuilder::CullMeshlets(*model, world, cameraPos, viewProj, m_settings);

            totalActive += cull.totalMeshlets;
            totalVisible += cull.passedMeshlets;
            totalCulled += (cull.frustumCulled + cull.backfaceConeCulled);
        }
    }

    m_stats.totalMeshletsActive = totalActive;
    m_stats.visibleMeshletsLastFrame = totalVisible;
    m_stats.culledMeshletsLastFrame = totalCulled;

    if (totalActive > 0) {
        m_stats.cullingEfficiencyPercent = (static_cast<float>(totalCulled) / static_cast<float>(totalActive)) * 100.0f;
    } else {
        m_stats.cullingEfficiencyPercent = 0.0f;
    }
}

} // namespace Meshlet
} // namespace Graphics
