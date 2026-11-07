#pragma once

#include "RayTracingTypes.h"
#include "Scene/Mesh.h"
#include <vector>
#include <memory>

class Scene;

namespace Graphics {
namespace RayTracing {

class IRayTracingBackend {
public:
    virtual ~IRayTracingBackend() = default;

    virtual RayTracingBackendType GetType() const = 0;
    virtual const char* GetName() const = 0;

    virtual bool IsSupported() const = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    // Bottom-Level Acceleration Structure (BLAS) for unique geometry
    virtual RayTracingMeshHandle CreateBLAS(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) = 0;
    virtual void DestroyBLAS(RayTracingMeshHandle handle) = 0;

    // Top-Level Acceleration Structure (TLAS) for scene instances
    virtual bool BuildTLAS(const std::vector<RayTracingInstance>& instances) = 0;

    // Settings & Diagnostics
    virtual RayTracingSettings& GetSettings() = 0;
    virtual const RayTracingSettings& GetSettings() const = 0;
    virtual RayTracingStats GetStats() const = 0;
};

} // namespace RayTracing
} // namespace Graphics

