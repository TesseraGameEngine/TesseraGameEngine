#pragma once

#include "IRayTracingBackend.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

namespace Graphics {
namespace RayTracing {

/**
 * VulkanRayTracing:
 * Hardware ray tracing with VK_KHR_acceleration_structure + VK_KHR_ray_query.
 *
 * - CreateBLAS uploads the mesh (position, normal, UV per vertex) to device memory and queues a
 *   BLAS build. Nothing waits on the GPU: queued builds are recorded into the frame's command
 *   buffer by RecordFrame(), so meshes can be registered while streaming without hitches.
 * - BuildTLAS stores a context's instance list; RecordFrame() writes it into that frame slot's
 *   instance buffer, (re)builds the TLAS and updates the context's descriptor set (set 1).
 * - Shaders read set 1: binding 0 = TLAS, binding 1 = per-instance surface data (with device
 *   addresses of the instance's vertex and index buffers), binding 2 = bindless albedo textures
 *   (alpha-tested shadows and textured reflections).
 *
 * Without a Vulkan device (headless tests) BLAS/TLAS bookkeeping still works on the CPU so
 * statistics can be checked; nothing is built.
 */
class VulkanRayTracing : public IRayTracingBackend {
public:
    // Matches RTVertex in shaders/rt_common.glsl (std430, 32 bytes)
    struct GpuVertex {
        float px, py, pz, u;
        float nx, ny, nz, v;
    };
    // Matches RTInstance in shaders/rt_common.glsl (std430, 64 bytes)
    struct GpuInstance {
        uint32_t vertexAddrLo, vertexAddrHi;
        uint32_t indexAddrLo, indexAddrHi;
        float albedoRough[4];   // rgb albedo, a roughness
        float emissiveMetal[4]; // rgb emissive, a metallic
        float alpha[4];         // x cutoff (0 = opaque), y texture index (-1 = none)
    };
    static_assert(sizeof(GpuVertex) == 32, "GpuVertex must match RTVertex");
    static_assert(sizeof(GpuInstance) == 64, "GpuInstance must match RTInstance");

    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxTextures = 2048;

    // Resolves an albedo texture path to an image view + sampler (VulkanPipeline::GetOrLoadTexture)
    using TextureResolver = std::function<bool(const std::string& path, VkImageView& view, VkSampler& sampler)>;

    VulkanRayTracing();
    ~VulkanRayTracing() override;

    RayTracingBackendType GetType() const override { return RayTracingBackendType::Vulkan_KHR; }
    const char* GetName() const override { return "Vulkan KHR Hardware Ray Tracing"; }

    bool IsSupported() const override;
    bool Initialize() override;
    void Shutdown() override;

    RayTracingMeshHandle CreateBLAS(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) override;
    void DestroyBLAS(RayTracingMeshHandle handle) override;

    bool BuildTLAS(const std::vector<RayTracingInstance>& instances, uint32_t context) override;

    RayTracingSettings& GetSettings() override { return m_settings; }
    const RayTracingSettings& GetSettings() const override { return m_settings; }
    RayTracingStats GetStats() const override;

    // Vulkan device binding (allows decoupling from VulkanContext for headless test suites)
    void SetDeviceHandles(VkPhysicalDevice physicalDevice, VkDevice device);
    void SetTextureResolver(TextureResolver resolver) { m_textureResolver = std::move(resolver); }

    // Records this frame's queued BLAS builds and every context's TLAS build into `cmd`
    // (outside any render pass). Call once per frame after BuildTLAS for each context.
    void RecordFrame(VkCommandBuffer cmd, uint32_t frameInFlight);

    // Set 1 layout for pipelines that trace rays (always valid once the device supports RT)
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_setLayout; }
    // The descriptor set to bind for `context` this frame; VK_NULL_HANDLE when there is nothing
    // to trace (no TLAS built yet, or no instances)
    VkDescriptorSet GetDescriptorSet(uint32_t context) const;

private:
    struct GpuBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{0};
        void* mapped{nullptr};
        VkDeviceAddress address{0};
    };

    struct VulkanBLAS {
        VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
        GpuBuffer asBuffer;
        GpuBuffer vertices;
        GpuBuffer indices;
        GpuBuffer staging;        // vertex + index upload, freed once the build has run
        GpuBuffer scratch;        // build scratch, freed once the build has run
        VkDeviceAddress asAddress{0};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        uint64_t bvhSize{0};
        bool built{false};
    };

    struct FrameTLAS {
        VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
        GpuBuffer asBuffer;
        GpuBuffer scratch;
        GpuBuffer instances;      // VkAccelerationStructureInstanceKHR[], host visible
        GpuBuffer surfaces;       // GpuInstance[], host visible (binding 1)
        VkDescriptorSet set{VK_NULL_HANDLE};
        uint32_t texturesWritten{0};
        uint32_t instanceCount{0};
        bool valid{false};
    };

    struct PendingDestroy {
        uint64_t frame{0};
        VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
        std::vector<GpuBuffer> buffers;
    };

    bool QueryExtensionSupport();
    void LoadRayTracingProcs();
    bool HasDevice() const { return m_device != VK_NULL_HANDLE && m_supported; }

    GpuBuffer CreateGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void DestroyGpuBuffer(GpuBuffer& buffer);
    void DeferDestroy(VkAccelerationStructureKHR handle, std::vector<GpuBuffer> buffers);
    void FlushDeferred(bool all);
    void CreateDescriptorResources();
    int TextureIndex(const std::string& path);
    void RecordBLASBuilds(VkCommandBuffer cmd);
    void RecordTLASBuild(VkCommandBuffer cmd, uint32_t context, FrameTLAS& tlas);

    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};

    bool m_initialized{false};
    bool m_supported{false};
    RayTracingSettings m_settings;
    mutable RayTracingStats m_stats;

    uint32_t m_nextHandle{1};
    std::unordered_map<RayTracingMeshHandle, VulkanBLAS> m_blases;
    std::vector<RayTracingMeshHandle> m_pendingBuilds;

    std::vector<RayTracingInstance> m_contextInstances[kRTContextCount];
    bool m_contextDirty[kRTContextCount]{};
    FrameTLAS m_tlas[kRTContextCount][kFramesInFlight];
    uint32_t m_frameInFlight{0};
    uint64_t m_frameCounter{0};
    std::vector<PendingDestroy> m_deferred;

    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    TextureResolver m_textureResolver;
    std::unordered_map<std::string, int> m_textureIndices;
    std::vector<std::pair<VkImageView, VkSampler>> m_textures;

    // Dynamic Vulkan KHR ray tracing entry points
    PFN_vkCreateAccelerationStructureKHR m_vkCreateAccelerationStructureKHR{nullptr};
    PFN_vkDestroyAccelerationStructureKHR m_vkDestroyAccelerationStructureKHR{nullptr};
    PFN_vkGetAccelerationStructureBuildSizesKHR m_vkGetAccelerationStructureBuildSizesKHR{nullptr};
    PFN_vkCmdBuildAccelerationStructuresKHR m_vkCmdBuildAccelerationStructuresKHR{nullptr};
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR{nullptr};
};

} // namespace RayTracing
} // namespace Graphics
