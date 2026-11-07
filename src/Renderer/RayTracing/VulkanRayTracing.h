#pragma once

#include "IRayTracingBackend.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>

namespace Graphics {
namespace RayTracing {

/**
 * VulkanRayTracing:
 * Hardware Ray Tracing implementation using Vulkan KHR extensions:
 * - VK_KHR_acceleration_structure
 * - VK_KHR_ray_query
 * - VK_KHR_deferred_host_operations
 * - VK_KHR_buffer_device_address
 * 
 * Generates Bottom-Level Acceleration Structures (BLAS) for geometry meshes
 * and Top-Level Acceleration Structures (TLAS) for scene instances.
 */
class VulkanRayTracing : public IRayTracingBackend {
public:
    VulkanRayTracing();
    ~VulkanRayTracing() override;

    RayTracingBackendType GetType() const override { return RayTracingBackendType::Vulkan_KHR; }
    const char* GetName() const override { return "Vulkan KHR Hardware Ray Tracing"; }

    bool IsSupported() const override;
    bool Initialize() override;
    void Shutdown() override;

    RayTracingMeshHandle CreateBLAS(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) override;
    void DestroyBLAS(RayTracingMeshHandle handle) override;

    bool BuildTLAS(const std::vector<RayTracingInstance>& instances) override;

    RayTracingSettings& GetSettings() override { return m_settings; }
    const RayTracingSettings& GetSettings() const override { return m_settings; }
    RayTracingStats GetStats() const override;

    // Vulkan device binding (allows decoupling from VulkanContext for headless test suites)
    void SetDeviceHandles(VkPhysicalDevice physicalDevice, VkDevice device);

    // Vulkan-specific handles for pipeline binding
    VkAccelerationStructureKHR GetTLAS() const { return m_tlas; }

private:
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};

    struct VulkanBLAS {
        VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceAddress deviceAddress{0};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        uint64_t bvhSize{0};
    };

    bool QueryExtensionSupport();
    void LoadRayTracingProcs();

    bool m_initialized{false};
    bool m_supported{false};
    RayTracingSettings m_settings;
    mutable RayTracingStats m_stats;

    uint32_t m_nextHandle{1};
    std::unordered_map<RayTracingMeshHandle, VulkanBLAS> m_blases;

    // Top-Level Acceleration Structure
    VkAccelerationStructureKHR m_tlas{VK_NULL_HANDLE};
    VkBuffer m_tlasBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_tlasMemory{VK_NULL_HANDLE};

    // Instance buffer for TLAS build
    VkBuffer m_instanceBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_instanceMemory{VK_NULL_HANDLE};

    // Dynamic Vulkan KHR Ray Tracing function pointers
    PFN_vkCreateAccelerationStructureKHR m_vkCreateAccelerationStructureKHR{nullptr};
    PFN_vkDestroyAccelerationStructureKHR m_vkDestroyAccelerationStructureKHR{nullptr};
    PFN_vkGetAccelerationStructureBuildSizesKHR m_vkGetAccelerationStructureBuildSizesKHR{nullptr};
    PFN_vkCmdBuildAccelerationStructuresKHR m_vkCmdBuildAccelerationStructuresKHR{nullptr};
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR{nullptr};
    PFN_vkBuildAccelerationStructuresKHR m_vkBuildAccelerationStructuresKHR{nullptr};
};

} // namespace RayTracing
} // namespace Graphics
