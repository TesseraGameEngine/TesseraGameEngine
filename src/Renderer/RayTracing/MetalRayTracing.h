#pragma once

#include "IRayTracingBackend.h"
#include <unordered_map>
#include <string>

namespace Graphics {
namespace RayTracing {

/**
 * MetalRayTracing:
 * Hardware-accelerated Ray Tracing backend for Apple Metal (Metal Performance Shaders / MPSRayIntersector
 * and MTLAccelerationStructure on Apple Silicon M1/M2/M3/M4+).
 * 
 * Implements IRayTracingBackend with 1:1 mapping between:
 * - BLAS <--> MTLPrimitiveAccelerationStructureDescriptor
 * - TLAS <--> MTLInstanceAccelerationStructureDescriptor
 * - Shaders <--> metal::raytracing::intersector / ray_query
 */
class MetalRayTracing : public IRayTracingBackend {
public:
    MetalRayTracing();
    ~MetalRayTracing() override;

    RayTracingBackendType GetType() const override { return RayTracingBackendType::Metal_MPS; }
    const char* GetName() const override { return "Apple Metal Ray Tracing (MPS / MTLAccelerationStructure)"; }

    bool IsSupported() const override;
    bool Initialize() override;
    void Shutdown() override;

    RayTracingMeshHandle CreateBLAS(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) override;
    void DestroyBLAS(RayTracingMeshHandle handle) override;

    bool BuildTLAS(const std::vector<RayTracingInstance>& instances) override;

    RayTracingSettings& GetSettings() override { return m_settings; }
    const RayTracingSettings& GetSettings() const override { return m_settings; }
    RayTracingStats GetStats() const override;

private:
    struct MetalBLAS {
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        uint64_t bvhSize{0};
        void* nativePrimitiveDescriptor{nullptr};
        void* nativeAccelerationStructure{nullptr};
    };

    bool m_initialized{false};
    RayTracingSettings m_settings;
    mutable RayTracingStats m_stats;

    uint32_t m_nextHandle{1};
    std::unordered_map<RayTracingMeshHandle, MetalBLAS> m_blases;
    void* m_nativeInstanceDescriptor{nullptr};
    void* m_nativeTopLevelAccelerationStructure{nullptr};
};

} // namespace RayTracing
} // namespace Graphics

