#include "RayTracingManager.h"
#include "VulkanRayTracing.h"
#include "MetalRayTracing.h"
#include "Scene/Scene.h"
#include "Scene/Nodes/3D/MeshNode.h"
#include <iostream>

namespace Graphics {
namespace RayTracing {

RayTracingManager& RayTracingManager::Get() {
    static RayTracingManager instance;
    return instance;
}

RayTracingManager::RayTracingManager() = default;

RayTracingManager::~RayTracingManager() {
    Shutdown();
}

bool RayTracingManager::Initialize(RayTracingBackendType preferredBackend) {
    if (m_initialized && m_backend) return true;

    if (preferredBackend == RayTracingBackendType::Metal_MPS) {
        m_backend = std::make_unique<MetalRayTracing>();
    } else if (preferredBackend == RayTracingBackendType::Vulkan_KHR) {
        m_backend = std::make_unique<VulkanRayTracing>();
    } else {
#if defined(__APPLE__)
        m_backend = std::make_unique<MetalRayTracing>();
#else
        m_backend = std::make_unique<VulkanRayTracing>();
#endif
    }

    if (m_backend) {
        m_backend->Initialize();
    }

    m_initialized = true;
    return true;
}

void RayTracingManager::Shutdown() {
    if (m_backend) {
        m_backend->Shutdown();
        m_backend.reset();
    }
    m_meshCache.clear();
    m_initialized = false;
}

void RayTracingManager::SetBackend(std::unique_ptr<IRayTracingBackend> customBackend) {
    if (m_backend) {
        m_backend->Shutdown();
    }
    m_backend = std::move(customBackend);
    if (m_backend) {
        m_backend->Initialize();
    }
    m_meshCache.clear();
}

bool RayTracingManager::IsHardwareRayTracingSupported() const {
    return m_backend ? m_backend->IsSupported() : false;
}

bool RayTracingManager::IsHardwareRayTracingActive() const {
    return m_backend && m_backend->IsSupported() && m_backend->GetSettings().enableHardwareRT;
}

RayTracingSettings& RayTracingManager::GetSettings() {
    if (m_backend) {
        return m_backend->GetSettings();
    }
    return m_fallbackSettings;
}

const RayTracingSettings& RayTracingManager::GetSettings() const {
    if (m_backend) {
        return m_backend->GetSettings();
    }
    return m_fallbackSettings;
}

RayTracingStats RayTracingManager::GetStats() const {
    if (m_backend) {
        return m_backend->GetStats();
    }
    RayTracingStats stats;
    stats.activeBackend = RayTracingBackendType::None;
    stats.supported = false;
    return stats;
}

RayTracingMeshHandle RayTracingManager::RegisterMesh(const std::string& cacheKey,
                                                     const std::vector<Vertex>& vertices,
                                                     const std::vector<uint32_t>& indices) {
    if (!m_backend) return kInvalidMeshHandle;

    auto it = m_meshCache.find(cacheKey);
    if (it != m_meshCache.end()) {
        return it->second;
    }

    RayTracingMeshHandle handle = m_backend->CreateBLAS(vertices, indices);
    if (handle != kInvalidMeshHandle) {
        m_meshCache[cacheKey] = handle;
    }
    return handle;
}

void RayTracingManager::UnregisterMesh(const std::string& cacheKey) {
    auto it = m_meshCache.find(cacheKey);
    if (it != m_meshCache.end()) {
        if (m_backend) {
            m_backend->DestroyBLAS(it->second);
        }
        m_meshCache.erase(it);
    }
}

void RayTracingManager::SyncScene(const Scene& scene) {
    if (!m_backend) return;

    std::vector<RayTracingInstance> instances;
    const auto& nodes = scene.GetNodes();
    instances.reserve(nodes.size());

    for (const auto& node : nodes) {
        if (!node) continue;

        if (node->nodeType == NodeType::Mesh || node->nodeType == NodeType::Character) {
            auto meshNode = std::dynamic_pointer_cast<MeshNode>(node);
            if (!meshNode) continue;

            // Generate or lookup BLAS
            std::string cacheKey = meshNode->customMeshName.empty() 
                ? "proc_shape_" + std::to_string(static_cast<int>(meshNode->type))
                : "mesh_asset_" + meshNode->customMeshName;

            RayTracingMeshHandle handle = kInvalidMeshHandle;
            auto it = m_meshCache.find(cacheKey);
            if (it != m_meshCache.end()) {
                handle = it->second;
            } else {
                MeshData genData;
                switch (meshNode->type) {
                    case ShapeType::Cube: genData = MeshGenerator::CreateCube(1.0f); break;
                    case ShapeType::Sphere: genData = MeshGenerator::CreateSphere(0.5f); break;
                    case ShapeType::Cone: genData = MeshGenerator::CreateCone(0.5f, 1.0f); break;
                    case ShapeType::Cylinder: genData = MeshGenerator::CreateCylinder(0.5f, 1.0f); break;
                    case ShapeType::Plane: genData = MeshGenerator::CreatePlane(2.0f); break;
                    case ShapeType::Quad: genData = MeshGenerator::CreateQuad(1.0f); break;
                    default: genData = MeshGenerator::CreateCube(1.0f); break;
                }
                handle = RegisterMesh(cacheKey, genData.vertices, genData.indices);
            }

            RayTracingInstance inst;
            inst.nodeId = node->id;
            inst.meshHandle = handle;
            inst.transform = node->GetWorldTransformMatrix(scene);
            inst.customId = node->id;
            inst.mask = 0xFF;
            inst.isOpaque = (meshNode->alphaCutoff <= 0.001f);

            instances.push_back(inst);
        }
    }

    m_backend->BuildTLAS(instances);
}

} // namespace RayTracing
} // namespace Graphics
