#pragma once

#include "RayTracingTypes.h"
#include "IRayTracingBackend.h"
#include <memory>
#include <unordered_map>
#include <string>

class Scene;

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

    // Scene Synchronization: updates BLASes for active meshes and builds the TLAS
    void SyncScene(const Scene& scene);

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

    std::unique_ptr<IRayTracingBackend> m_backend;
    RayTracingSettings m_fallbackSettings;
    std::unordered_map<std::string, RayTracingMeshHandle> m_meshCache;
    bool m_initialized{false};
};

} // namespace RayTracing
} // namespace Graphics

