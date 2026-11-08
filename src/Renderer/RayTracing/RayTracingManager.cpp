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
        return it->second.handle;
    }

    RayTracingMeshHandle handle = m_backend->CreateBLAS(vertices, indices);
    if (handle != kInvalidMeshHandle) {
        CachedMesh cached;
        cached.handle = handle;
        cached.vertexCount = vertices.size();
        cached.indexCount = indices.size();
        m_meshCache[cacheKey] = cached;
    }
    return handle;
}

void RayTracingManager::UnregisterMesh(const std::string& cacheKey) {
    auto it = m_meshCache.find(cacheKey);
    if (it != m_meshCache.end()) {
        if (m_backend) {
            m_backend->DestroyBLAS(it->second.handle);
        }
        m_meshCache.erase(it);
    }
}

void RayTracingManager::ClearContext(uint32_t context) {
    if (m_backend) m_backend->BuildTLAS({}, context);
}

void RayTracingManager::SyncScene(const Scene& scene, uint32_t context,
                                  const MeshResolver& meshResolver, const MaterialResolver& materialResolver,
                                  const glm::vec3& focus, float maxDistance) {
    if (!m_backend) return;

    // Without a resolver: the built-in primitive shapes (same sizes as the renderer's meshes)
    auto builtinResolver = [this](const Node& node, std::string& key, const MeshData*& data) -> bool {
        if (node.type == ShapeType::Custom) return false; // imported geometry needs a resolver
        const int shape = static_cast<int>(node.type);
        key = "proc_shape_" + std::to_string(shape);
        auto it = m_primitiveMeshes.find(shape);
        if (it == m_primitiveMeshes.end()) {
            MeshData genData;
            switch (node.type) {
                case ShapeType::Cube: genData = MeshGenerator::CreateCube(1.0f); break;
                case ShapeType::Sphere: genData = MeshGenerator::CreateSphere(0.5f); break;
                case ShapeType::Cone: genData = MeshGenerator::CreateCone(0.5f, 1.0f); break;
                case ShapeType::Cylinder: genData = MeshGenerator::CreateCylinder(0.5f, 1.0f); break;
                case ShapeType::Plane: genData = MeshGenerator::CreatePlane(2.0f); break;
                case ShapeType::Quad: genData = MeshGenerator::CreateQuad(1.0f); break;
                default: return false;
            }
            it = m_primitiveMeshes.emplace(shape, std::move(genData)).first;
        }
        data = &it->second;
        return true;
    };

    std::vector<RayTracingInstance> instances;
    const auto& nodes = scene.GetNodes();
    instances.reserve(nodes.size());

    for (const auto& node : nodes) {
        if (!node) continue;
        const bool drawable = node->nodeType == NodeType::Mesh || node->nodeType == NodeType::Character ||
                              node->nodeType == NodeType::Terrain;
        if (!drawable) continue;
        // Surfaces drawn by special passes are not scene geometry for rays
        if (node->isOcean || node->isLiquid || node->isMirror) continue;
        if (node->color.a < 0.99f && node->alphaCutoff <= 0.001f) continue; // see-through glass

        const glm::mat4 world = node->GetWorldTransformMatrix(scene);
        if (maxDistance > 0.0f) {
            const glm::vec3 pos(world[3]);
            const glm::vec3 extent = (node->boundsMax - node->boundsMin) * 0.5f;
            const float radius = glm::length(extent * glm::vec3(glm::length(glm::vec3(world[0])),
                                                                glm::length(glm::vec3(world[1])),
                                                                glm::length(glm::vec3(world[2]))));
            if (glm::length(pos - focus) - radius > maxDistance) continue;
        }

        std::string cacheKey;
        const MeshData* data = nullptr;
        const bool resolved = meshResolver ? meshResolver(*node, cacheKey, data) : builtinResolver(*node, cacheKey, data);
        if (!resolved || cacheKey.empty()) continue;

        RayTracingMeshHandle handle = kInvalidMeshHandle;
        auto it = m_meshCache.find(cacheKey);
        if (it != m_meshCache.end()) {
            // Same key, different data (re-imported model, sculpted terrain): rebuild it
            const bool changed = data && (it->second.source != data ||
                                          it->second.vertexCount != data->vertices.size() ||
                                          it->second.indexCount != data->indices.size());
            if (changed && it->second.source != nullptr) {
                UnregisterMesh(cacheKey);
                it = m_meshCache.end();
            } else {
                if (data) it->second.source = data;
                handle = it->second.handle;
            }
        }
        if (it == m_meshCache.end()) {
            if (!data || data->vertices.empty()) continue;
            handle = RegisterMesh(cacheKey, data->vertices, data->indices);
            auto cached = m_meshCache.find(cacheKey);
            if (cached != m_meshCache.end()) cached->second.source = data;
        }
        if (handle == kInvalidMeshHandle) continue;

        RayTracingInstance inst;
        inst.nodeId = node->id;
        inst.meshHandle = handle;
        inst.transform = world;
        inst.customId = node->id;
        inst.mask = kRTMaskOpaque;
        inst.albedo = glm::vec3(node->color);
        inst.roughness = node->roughness;
        inst.metallic = node->metallic;
        inst.emissive = node->emissiveColor * node->emissiveIntensity;
        inst.alphaCutoff = node->alphaCutoff;
        if (materialResolver) materialResolver(*node, inst);
        inst.isOpaque = inst.alphaCutoff <= 0.001f || inst.albedoTexture.empty();
        if (!inst.isOpaque) inst.mask = kRTMaskFoliage;

        instances.push_back(std::move(inst));
    }

    m_backend->BuildTLAS(instances, context);
}

} // namespace RayTracing
} // namespace Graphics
