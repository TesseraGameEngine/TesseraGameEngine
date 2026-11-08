#include "VulkanRayTracing.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/VulkanBuffer.h"
#include <iostream>
#include <chrono>
#include <set>
#include <cstring>
#include <algorithm>

namespace Graphics {
namespace RayTracing {

namespace {
VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
}
} // namespace

VulkanRayTracing::VulkanRayTracing() {
    m_stats.activeBackend = RayTracingBackendType::Vulkan_KHR;
}

VulkanRayTracing::~VulkanRayTracing() {
    Shutdown();
}

void VulkanRayTracing::SetDeviceHandles(VkPhysicalDevice physicalDevice, VkDevice device) {
    if (m_initialized) Shutdown();
    m_physicalDevice = physicalDevice;
    m_device = device;
    if (m_physicalDevice != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        Initialize();
    }
}

bool VulkanRayTracing::IsSupported() const {
    return m_supported;
}

bool VulkanRayTracing::QueryExtensionSupport() {
    if (m_physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_stats.deviceName = props.deviceName;

    // The extensions and features have to be enabled on the device itself (VulkanContext does
    // that when the GPU supports them), not merely be available
    auto& ctx = VulkanContext::Get();
    return ctx.GetDevice() == m_device && ctx.SupportsRayQuery();
}

void VulkanRayTracing::LoadRayTracingProcs() {
    if (m_device == VK_NULL_HANDLE) return;

    m_vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR"));
    m_vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR"));
    m_vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR"));
    m_vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR"));
    m_vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR"));

    if (!m_vkCreateAccelerationStructureKHR || !m_vkDestroyAccelerationStructureKHR ||
        !m_vkGetAccelerationStructureBuildSizesKHR || !m_vkCmdBuildAccelerationStructuresKHR ||
        !m_vkGetAccelerationStructureDeviceAddressKHR) {
        std::cerr << "[VulkanRT] Ray tracing entry points missing; disabling hardware ray tracing" << std::endl;
        m_supported = false;
    }
}

bool VulkanRayTracing::Initialize() {
    if (m_initialized) return m_supported;

    if (m_device == VK_NULL_HANDLE || m_physicalDevice == VK_NULL_HANDLE) {
        // Headless test suite or non-Vulkan runtime: CPU bookkeeping only
        m_supported = false;
        m_stats.supported = false;
        m_stats.deviceName = "Headless / Non-VK Context";
        m_initialized = true;
        return false;
    }

    m_supported = QueryExtensionSupport();
    if (m_supported) LoadRayTracingProcs();
    if (m_supported) CreateDescriptorResources();
    m_stats.supported = m_supported;

    if (m_supported) {
        std::cout << "[VulkanRT] Hardware ray tracing (ray query) active on: " << m_stats.deviceName << std::endl;
    } else {
        std::cout << "[VulkanRT] Hardware ray tracing unavailable on: " << m_stats.deviceName
                  << " (shadow maps and screen-space reflections are used instead)" << std::endl;
    }

    m_initialized = true;
    return m_supported;
}

void VulkanRayTracing::Shutdown() {
    if (HasDevice()) {
        vkDeviceWaitIdle(m_device);
        FlushDeferred(true);
        for (auto& [handle, blas] : m_blases) {
            if (blas.handle != VK_NULL_HANDLE) m_vkDestroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
            DestroyGpuBuffer(blas.asBuffer);
            DestroyGpuBuffer(blas.vertices);
            DestroyGpuBuffer(blas.indices);
            DestroyGpuBuffer(blas.staging);
            DestroyGpuBuffer(blas.scratch);
        }
        for (auto& contextSlots : m_tlas) {
            for (auto& tlas : contextSlots) {
                if (tlas.handle != VK_NULL_HANDLE) m_vkDestroyAccelerationStructureKHR(m_device, tlas.handle, nullptr);
                DestroyGpuBuffer(tlas.asBuffer);
                DestroyGpuBuffer(tlas.scratch);
                DestroyGpuBuffer(tlas.instances);
                DestroyGpuBuffer(tlas.surfaces);
                tlas = FrameTLAS{};
            }
        }
        if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    }
    m_descriptorPool = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_blases.clear();
    m_pendingBuilds.clear();
    m_deferred.clear();
    m_textureIndices.clear();
    m_textures.clear();
    for (auto& list : m_contextInstances) list.clear();
    m_initialized = false;
}

// ----------------------------------------------------------------------------------------------
// Buffers
// ----------------------------------------------------------------------------------------------

VulkanRayTracing::GpuBuffer VulkanRayTracing::CreateGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    GpuBuffer out;
    out.size = std::max<VkDeviceSize>(size, 16);
    const VkMemoryPropertyFlags props = hostVisible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VulkanBufferUtils::CreateBuffer(out.size, usage, props, out.buffer, out.memory);
    if (hostVisible) {
        vkMapMemory(m_device, out.memory, 0, out.size, 0, &out.mapped);
    }
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = out.buffer;
        out.address = vkGetBufferDeviceAddress(m_device, &info);
    }
    return out;
}

void VulkanRayTracing::DestroyGpuBuffer(GpuBuffer& buffer) {
    if (m_device == VK_NULL_HANDLE) { buffer = GpuBuffer{}; return; }
    if (buffer.mapped) vkUnmapMemory(m_device, buffer.memory);
    if (buffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = GpuBuffer{};
}

void VulkanRayTracing::DeferDestroy(VkAccelerationStructureKHR handle, std::vector<GpuBuffer> buffers) {
    PendingDestroy pending;
    pending.frame = m_frameCounter;
    pending.handle = handle;
    pending.buffers = std::move(buffers);
    m_deferred.push_back(std::move(pending));
}

void VulkanRayTracing::FlushDeferred(bool all) {
    // A frame slot is reused only after its fence signalled, so anything queued for deletion
    // kFramesInFlight frames ago is no longer read by the GPU
    auto it = m_deferred.begin();
    while (it != m_deferred.end()) {
        if (all || it->frame + kFramesInFlight <= m_frameCounter) {
            if (it->handle != VK_NULL_HANDLE) m_vkDestroyAccelerationStructureKHR(m_device, it->handle, nullptr);
            for (auto& b : it->buffers) DestroyGpuBuffer(b);
            it = m_deferred.erase(it);
        } else {
            ++it;
        }
    }
}

// ----------------------------------------------------------------------------------------------
// Descriptors (set 1)
// ----------------------------------------------------------------------------------------------

void VulkanRayTracing::CreateDescriptorResources() {
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = kMaxTextures;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Texture slots that no instance uses are never written
    VkDescriptorBindingFlags bindingFlags[3] = { 0, 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT };
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 3;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        std::cerr << "[VulkanRT] Failed to create the ray tracing descriptor set layout" << std::endl;
        m_supported = false;
        return;
    }

    const uint32_t setCount = kRTContextCount * kFramesInFlight;
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount };
    poolSizes[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * kMaxTextures };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        std::cerr << "[VulkanRT] Failed to create the ray tracing descriptor pool" << std::endl;
        m_supported = false;
        return;
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, m_setLayout);
    std::vector<VkDescriptorSet> sets(setCount);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocInfo, sets.data()) != VK_SUCCESS) {
        std::cerr << "[VulkanRT] Failed to allocate ray tracing descriptor sets" << std::endl;
        m_supported = false;
        return;
    }
    for (uint32_t c = 0; c < kRTContextCount; ++c) {
        for (uint32_t f = 0; f < kFramesInFlight; ++f) {
            m_tlas[c][f].set = sets[c * kFramesInFlight + f];
        }
    }
}

int VulkanRayTracing::TextureIndex(const std::string& path) {
    if (path.empty()) return -1;
    auto it = m_textureIndices.find(path);
    if (it != m_textureIndices.end()) return it->second;
    int index = -1;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    if (m_textureResolver && m_textures.size() < kMaxTextures &&
        m_textureResolver(path, view, sampler) && view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE) {
        index = static_cast<int>(m_textures.size());
        m_textures.emplace_back(view, sampler);
    }
    m_textureIndices[path] = index; // failures are remembered too, so missing files are tried once
    return index;
}

VkDescriptorSet VulkanRayTracing::GetDescriptorSet(uint32_t context) const {
    if (!m_supported || context >= kRTContextCount) return VK_NULL_HANDLE;
    const FrameTLAS& tlas = m_tlas[context][m_frameInFlight];
    return tlas.valid ? tlas.set : VK_NULL_HANDLE;
}

// ----------------------------------------------------------------------------------------------
// BLAS
// ----------------------------------------------------------------------------------------------

RayTracingMeshHandle VulkanRayTracing::CreateBLAS(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.size() < 3) {
        return kInvalidMeshHandle;
    }

    RayTracingMeshHandle handle = m_nextHandle++;
    VulkanBLAS blas;
    blas.vertexCount = static_cast<uint32_t>(vertices.size());
    blas.indexCount = static_cast<uint32_t>(indices.size() / 3 * 3);

    if (!HasDevice()) {
        // CPU-only bookkeeping (headless): an estimate of what the GPU structure would take
        const uint64_t triCount = std::max<uint64_t>(1ULL, blas.indexCount / 3);
        blas.bvhSize = std::max<uint64_t>(4096ULL, triCount * 160 + vertices.size() * sizeof(glm::vec3));
        m_blases[handle] = std::move(blas);
        return handle;
    }

    // Geometry the shaders can read through device addresses (normals and UVs for hit shading)
    std::vector<GpuVertex> gpuVerts(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vertex& v = vertices[i];
        gpuVerts[i] = { v.pos.x, v.pos.y, v.pos.z, v.texCoord.x, v.normal.x, v.normal.y, v.normal.z, v.texCoord.y };
    }
    const VkDeviceSize vertexBytes = gpuVerts.size() * sizeof(GpuVertex);
    const VkDeviceSize indexBytes = blas.indexCount * sizeof(uint32_t);

    const VkBufferUsageFlags geometryUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    blas.vertices = CreateGpuBuffer(vertexBytes, geometryUsage, false);
    blas.indices = CreateGpuBuffer(indexBytes, geometryUsage, false);
    blas.staging = CreateGpuBuffer(vertexBytes + indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(blas.staging.mapped, gpuVerts.data(), vertexBytes);
    std::memcpy(static_cast<char*>(blas.staging.mapped) + vertexBytes, indices.data(), indexBytes);

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = 0; // opacity is decided per instance (alpha-tested foliage)
    auto& tri = geometry.geometry.triangles;
    tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = blas.vertices.address;
    tri.vertexStride = sizeof(GpuVertex);
    tri.maxVertex = blas.vertexCount - 1;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = blas.indices.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    const uint32_t primitiveCount = blas.indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &buildInfo, &primitiveCount, &sizes);

    blas.asBuffer = CreateGpuBuffer(sizes.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    false);
    const VkDeviceSize scratchAlign = VulkanContext::Get().GetMinASScratchAlignment();
    blas.scratch = CreateGpuBuffer(sizes.buildScratchSize + scratchAlign,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);

    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = blas.asBuffer.buffer;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (m_vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &blas.handle) != VK_SUCCESS) {
        DestroyGpuBuffer(blas.asBuffer);
        DestroyGpuBuffer(blas.scratch);
        DestroyGpuBuffer(blas.staging);
        DestroyGpuBuffer(blas.vertices);
        DestroyGpuBuffer(blas.indices);
        return kInvalidMeshHandle;
    }
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addrInfo.accelerationStructure = blas.handle;
    blas.asAddress = m_vkGetAccelerationStructureDeviceAddressKHR(m_device, &addrInfo);
    blas.bvhSize = sizes.accelerationStructureSize + vertexBytes + indexBytes;

    m_blases[handle] = std::move(blas);
    m_pendingBuilds.push_back(handle);
    return handle;
}

void VulkanRayTracing::DestroyBLAS(RayTracingMeshHandle handle) {
    auto it = m_blases.find(handle);
    if (it == m_blases.end()) return;
    if (HasDevice()) {
        VulkanBLAS& b = it->second;
        DeferDestroy(b.handle, { b.asBuffer, b.vertices, b.indices, b.staging, b.scratch });
    }
    m_pendingBuilds.erase(std::remove(m_pendingBuilds.begin(), m_pendingBuilds.end(), handle), m_pendingBuilds.end());
    m_blases.erase(it);
}

void VulkanRayTracing::RecordBLASBuilds(VkCommandBuffer cmd) {
    if (m_pendingBuilds.empty()) return;

    // 1. Copy the uploaded geometry into place
    for (RayTracingMeshHandle h : m_pendingBuilds) {
        VulkanBLAS& b = m_blases[h];
        const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(b.vertexCount) * sizeof(GpuVertex);
        const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(b.indexCount) * sizeof(uint32_t);
        VkBufferCopy vCopy{0, 0, vertexBytes};
        VkBufferCopy iCopy{vertexBytes, 0, indexBytes};
        vkCmdCopyBuffer(cmd, b.staging.buffer, b.vertices.buffer, 1, &vCopy);
        vkCmdCopyBuffer(cmd, b.staging.buffer, b.indices.buffer, 1, &iCopy);
    }
    VkMemoryBarrier copyBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &copyBarrier, 0, nullptr, 0, nullptr);

    // 2. Build every queued BLAS in one call
    const VkDeviceSize scratchAlign = VulkanContext::Get().GetMinASScratchAlignment();
    std::vector<VkAccelerationStructureGeometryKHR> geometries(m_pendingBuilds.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(m_pendingBuilds.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(m_pendingBuilds.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePtrs(m_pendingBuilds.size());
    for (size_t i = 0; i < m_pendingBuilds.size(); ++i) {
        VulkanBLAS& b = m_blases[m_pendingBuilds[i]];
        auto& geometry = geometries[i];
        geometry = VkAccelerationStructureGeometryKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        auto& tri = geometry.geometry.triangles;
        tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tri.vertexData.deviceAddress = b.vertices.address;
        tri.vertexStride = sizeof(GpuVertex);
        tri.maxVertex = b.vertexCount - 1;
        tri.indexType = VK_INDEX_TYPE_UINT32;
        tri.indexData.deviceAddress = b.indices.address;

        auto& info = infos[i];
        info = VkAccelerationStructureBuildGeometryInfoKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.dstAccelerationStructure = b.handle;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        info.scratchData.deviceAddress = AlignUp(b.scratch.address, scratchAlign);

        ranges[i] = VkAccelerationStructureBuildRangeInfoKHR{ b.indexCount / 3, 0, 0, 0 };
        rangePtrs[i] = &ranges[i];
        b.built = true;
    }
    m_vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(infos.size()), infos.data(), rangePtrs.data());

    // 3. The staging and scratch memory is free once this frame finishes
    for (RayTracingMeshHandle h : m_pendingBuilds) {
        VulkanBLAS& b = m_blases[h];
        DeferDestroy(VK_NULL_HANDLE, { b.staging, b.scratch });
        b.staging = GpuBuffer{};
        b.scratch = GpuBuffer{};
    }
    m_pendingBuilds.clear();

    VkMemoryBarrier buildBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    buildBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    buildBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &buildBarrier, 0, nullptr, 0, nullptr);
}

// ----------------------------------------------------------------------------------------------
// TLAS
// ----------------------------------------------------------------------------------------------

bool VulkanRayTracing::BuildTLAS(const std::vector<RayTracingInstance>& instances, uint32_t context) {
    if (context >= kRTContextCount) return false;
    m_contextInstances[context] = instances;
    m_contextDirty[context] = true;

    if (!HasDevice()) {
        // Headless: statistics only
        auto start = std::chrono::high_resolution_clock::now();
        m_stats.activeTLASInstances = static_cast<uint32_t>(instances.size());
        uint64_t totalMem = 0;
        for (const auto& pair : m_blases) totalMem += pair.second.bvhSize;
        totalMem += std::max<uint64_t>(4096ULL, static_cast<uint64_t>(instances.size() * 128));
        m_stats.bvhMemoryBytes = totalMem;
        m_stats.totalBLASCount = static_cast<uint32_t>(m_blases.size());
        auto end = std::chrono::high_resolution_clock::now();
        m_stats.lastBuildTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
    }
    return true;
}

void VulkanRayTracing::RecordTLASBuild(VkCommandBuffer cmd, uint32_t context, FrameTLAS& tlas) {
    tlas.valid = false;
    const auto& source = m_contextInstances[context];

    // Gather the instances whose geometry exists (built earlier or queued in this command buffer)
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    std::vector<GpuInstance> surfaces;
    asInstances.reserve(source.size());
    surfaces.reserve(source.size());
    for (const auto& inst : source) {
        auto it = m_blases.find(inst.meshHandle);
        if (it == m_blases.end() || !it->second.built || it->second.handle == VK_NULL_HANDLE) continue;
        const VulkanBLAS& b = it->second;

        VkAccelerationStructureInstanceKHR asi{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                asi.transform.matrix[r][c] = inst.transform[c][r];
            }
        }
        asi.instanceCustomIndex = static_cast<uint32_t>(surfaces.size()) & 0xFFFFFF;
        asi.mask = inst.mask;
        asi.instanceShaderBindingTableRecordOffset = 0;
        const bool alphaTested = inst.alphaCutoff > 0.0f && !inst.albedoTexture.empty();
        asi.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                    (alphaTested ? VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR);
        asi.accelerationStructureReference = b.asAddress;
        asInstances.push_back(asi);

        GpuInstance s{};
        s.vertexAddrLo = static_cast<uint32_t>(b.vertices.address & 0xFFFFFFFFull);
        s.vertexAddrHi = static_cast<uint32_t>(b.vertices.address >> 32);
        s.indexAddrLo = static_cast<uint32_t>(b.indices.address & 0xFFFFFFFFull);
        s.indexAddrHi = static_cast<uint32_t>(b.indices.address >> 32);
        s.albedoRough[0] = inst.albedo.r; s.albedoRough[1] = inst.albedo.g; s.albedoRough[2] = inst.albedo.b;
        s.albedoRough[3] = inst.roughness;
        s.emissiveMetal[0] = inst.emissive.r; s.emissiveMetal[1] = inst.emissive.g; s.emissiveMetal[2] = inst.emissive.b;
        s.emissiveMetal[3] = inst.metallic;
        s.alpha[0] = alphaTested ? inst.alphaCutoff : 0.0f;
        s.alpha[1] = static_cast<float>(TextureIndex(inst.albedoTexture));
        surfaces.push_back(s);
    }
    tlas.instanceCount = static_cast<uint32_t>(asInstances.size());
    if (asInstances.empty()) return;

    // Host-written inputs: grown when needed (this slot's previous frame has finished)
    const VkDeviceSize instanceBytes = asInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    if (tlas.instances.size < instanceBytes) {
        DestroyGpuBuffer(tlas.instances);
        tlas.instances = CreateGpuBuffer(instanceBytes * 3 / 2,
                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true);
    }
    const VkDeviceSize surfaceBytes = surfaces.size() * sizeof(GpuInstance);
    if (tlas.surfaces.size < surfaceBytes) {
        DestroyGpuBuffer(tlas.surfaces);
        tlas.surfaces = CreateGpuBuffer(surfaceBytes * 3 / 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    }
    std::memcpy(tlas.instances.mapped, asInstances.data(), instanceBytes);
    std::memcpy(tlas.surfaces.mapped, surfaces.data(), surfaceBytes);

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = tlas.instances.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    const uint32_t count = tlas.instanceCount;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &count, &sizes);

    bool recreated = false;
    if (tlas.handle == VK_NULL_HANDLE || tlas.asBuffer.size < sizes.accelerationStructureSize) {
        if (tlas.handle != VK_NULL_HANDLE) m_vkDestroyAccelerationStructureKHR(m_device, tlas.handle, nullptr);
        DestroyGpuBuffer(tlas.asBuffer);
        const VkDeviceSize asSize = sizes.accelerationStructureSize * 3 / 2;
        tlas.asBuffer = CreateGpuBuffer(asSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = tlas.asBuffer.buffer;
        createInfo.size = asSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (m_vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &tlas.handle) != VK_SUCCESS) {
            tlas.handle = VK_NULL_HANDLE;
            return;
        }
        recreated = true;
    }
    const VkDeviceSize scratchAlign = VulkanContext::Get().GetMinASScratchAlignment();
    if (tlas.scratch.size < sizes.buildScratchSize + scratchAlign) {
        DestroyGpuBuffer(tlas.scratch);
        tlas.scratch = CreateGpuBuffer((sizes.buildScratchSize + scratchAlign) * 3 / 2,
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
    }

    buildInfo.dstAccelerationStructure = tlas.handle;
    buildInfo.scratchData.deviceAddress = AlignUp(tlas.scratch.address, scratchAlign);
    VkAccelerationStructureBuildRangeInfoKHR range{ count, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
    m_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &rangePtr);

    // Descriptors: TLAS + surfaces every time (cheap), textures added since the last update
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &tlas.handle;
    VkDescriptorBufferInfo surfaceInfo{ tlas.surfaces.buffer, 0, VK_WHOLE_SIZE };

    std::vector<VkWriteDescriptorSet> writes;
    VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w0.pNext = &asWrite;
    w0.dstSet = tlas.set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes.push_back(w0);
    VkWriteDescriptorSet w1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w1.dstSet = tlas.set;
    w1.dstBinding = 1;
    w1.descriptorCount = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w1.pBufferInfo = &surfaceInfo;
    writes.push_back(w1);

    std::vector<VkDescriptorImageInfo> imageInfos;
    if (tlas.texturesWritten < m_textures.size()) {
        for (size_t i = tlas.texturesWritten; i < m_textures.size(); ++i) {
            imageInfos.push_back({ m_textures[i].second, m_textures[i].first, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        }
        VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w2.dstSet = tlas.set;
        w2.dstBinding = 2;
        w2.dstArrayElement = tlas.texturesWritten;
        w2.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.pImageInfo = imageInfos.data();
        writes.push_back(w2);
        tlas.texturesWritten = static_cast<uint32_t>(m_textures.size());
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    (void)recreated;
    tlas.valid = true;
}

void VulkanRayTracing::RecordFrame(VkCommandBuffer cmd, uint32_t frameInFlight) {
    if (!HasDevice()) return;
    auto start = std::chrono::high_resolution_clock::now();

    ++m_frameCounter;
    m_frameInFlight = frameInFlight % kFramesInFlight;
    FlushDeferred(false);

    RecordBLASBuilds(cmd);

    uint32_t totalInstances = 0;
    for (uint32_t c = 0; c < kRTContextCount; ++c) {
        FrameTLAS& tlas = m_tlas[c][m_frameInFlight];
        if (m_contextInstances[c].empty() && !m_contextDirty[c]) {
            tlas.valid = false;
            continue;
        }
        RecordTLASBuild(cmd, c, tlas);
        m_contextDirty[c] = false;
        totalInstances += tlas.instanceCount;
    }

    // Fragment shaders trace against the new TLASes
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    uint64_t memory = 0;
    for (const auto& pair : m_blases) memory += pair.second.bvhSize;
    for (auto& contextSlots : m_tlas) {
        for (auto& t : contextSlots) memory += t.asBuffer.size + t.instances.size + t.surfaces.size;
    }
    m_stats.bvhMemoryBytes = memory;
    m_stats.activeTLASInstances = totalInstances;
    m_stats.totalBLASCount = static_cast<uint32_t>(m_blases.size());
    auto end = std::chrono::high_resolution_clock::now();
    m_stats.lastBuildTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
}

RayTracingStats VulkanRayTracing::GetStats() const {
    m_stats.totalBLASCount = static_cast<uint32_t>(m_blases.size());
    m_stats.supported = m_supported;
    return m_stats;
}

} // namespace RayTracing
} // namespace Graphics
