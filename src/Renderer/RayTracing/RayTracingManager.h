#pragma once

#include "RayTracingTypes.h"
#include "IRayTracingBackend.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <functional>

class Scene;
struct Node;

namespace Graphics {
namespace RayTracing {

/**
 * RayTracingManager:
 * High-level Hardware Ray Tracing coordinator for the Tessera Game Engine.
 * 
 * Provides backend-agnostic acceleration structure management, supporting:
 * - Vulkan KHR Hardware Ray Tracing (Windows / Linux)
 * - Apple Metal MPS / MTLAccelerationStructure (macOS / iOS)
 * - Hybrid Software Fallback
 * 
 * Synchronizes scene geometry into BLAS/TLAS trees and drives RTAO, RTR, RTS, and RTGI.
 */
class RayTracingManager {
public:
    static RayTracingManager& Get();

    bool Initialize(RayTracingBackendType preferredBackend = RayTracingBackendType::None);
    void Shutdown();

    // Finds a node's geometry for ray tracing: fills a stable cache key and the CPU mesh data
    // (data may be null when the key is already registered and unchanged). Return false to leave
    // the node out of the acceleration structure.
    using MeshResolver = std::function<bool(const Node& node, std::string& cacheKey, const MeshData*& data)>;
    // Fills material data (albedo, roughness, textures...) for a node's instance. Meshes with
    // submeshes get one instance per submesh (its own BLAS), `submesh` saying which; -1 = no
    // submeshes (the node's own material).
    using MaterialResolver = std::function<void(const Node& node, int submesh, RayTracingInstance& instance)>;

    // Scene Synchronization: registers BLASes for the scene's meshes (through `meshResolver`,
    // or the built-in primitive shapes when none is given) and hands the instance list to the
    // backend as the TLAS for `context`. Nodes further than `maxDistance` from `focus`
    // (when maxDistance > 0) are left out.
    void SyncScene(const Scene& scene, uint32_t context = 0,
                   const MeshResolver& meshResolver = {}, const MaterialResolver& materialResolver = {},
                   const glm::vec3& focus = glm::vec3(0.0f), float maxDistance = 0.0f);
    // Clears a context's TLAS (nothing to trace for it)
    void ClearContext(uint32_t context);

    // Active Backend & Capabilities
    IRayTracingBackend* GetBackend() const { return m_backend.get(); }
    void SetBackend(std::unique_ptr<IRayTracingBackend> customBackend);

    bool IsHardwareRayTracingSupported() const;
    bool IsHardwareRayTracingActive() const;

    // Settings
    RayTracingSettings& GetSettings();
    const RayTracingSettings& GetSettings() const;

    // Diagnostics
    RayTracingStats GetStats() const;

    // Register unique mesh geometry into the ray tracing cache
    RayTracingMeshHandle RegisterMesh(const std::string& cacheKey, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void UnregisterMesh(const std::string& cacheKey);

private:
    RayTracingManager();
    ~RayTracingManager();

    struct CachedMesh {
        RayTracingMeshHandle handle{kInvalidMeshHandle};   // first part
        std::vector<RayTracingMeshHandle> parts;           // one BLAS per submesh (or just `handle`)
        bool perSubmesh{false};                            // parts are the mesh's submeshes
        const void* source{nullptr};   // the MeshData it was built from (to spot changes)
        size_t vertexCount{0};
        size_t indexCount{0};
    };
    // Registers a mesh as one BLAS per submesh (each with only the vertices it uses)
    bool RegisterMeshParts(const std::string& cacheKey, const MeshData& data);

    std::unique_ptr<IRayTracingBackend> m_backend;
    RayTracingSettings m_fallbackSettings;
    std::unordered_map<std::string, CachedMesh> m_meshCache;
    std::unordered_map<int, MeshData> m_primitiveMeshes; // built-in shapes (ShapeType)
    bool m_initialized{false};
};

} // namespace RayTracing
} // namespace Graphics

