#include "VulkanSkinning.h"
#include "VulkanContext.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

constexpr uint32_t kMaxCharacters = 512;   // descriptor sets in the pool
constexpr uint32_t kWorkgroupSize = 64;    // local_size_x in skinning.comp

struct SkinningPushConstants {
    glm::vec4 centerScale;
    glm::vec4 morphs;
    uint32_t vertexCount;
    uint32_t boneCount;
    uint32_t pad[2];
};

std::vector<char> ReadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open shader: " + path);
    std::vector<char> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

} // namespace

void VulkanSkinning::Init() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    // 0: bind-pose source, 1: bone matrices (dynamic: frame region + character offset), 2: output vertices
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t b = 0; b < 3; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = (b == 1) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skinning descriptor set layout!");
    }

    VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinningPushConstants) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skinning pipeline layout!");
    }

    std::vector<char> code = ReadSpirv("shaders/skinning_comp.spv");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skinning shader module!");
    }
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_pipelineLayout;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create skinning pipeline!");

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxCharacters * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kMaxCharacters },
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kMaxCharacters;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skinning descriptor pool!");
    }

    // Bone matrices: one host-visible region per frame in flight
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.GetPhysicalDevice(), &props);
    m_boneAlignment = std::max<VkDeviceSize>(props.limits.minStorageBufferOffsetAlignment, sizeof(glm::mat4));
    m_boneRegionSize = static_cast<VkDeviceSize>(kMaxBoneMatrices) * sizeof(glm::mat4);
    VulkanBufferUtils::CreateBuffer(m_boneRegionSize * kFramesInFlight, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    m_boneBuffer, m_boneMemory);
    void* mapped = nullptr;
    vkMapMemory(device, m_boneMemory, 0, m_boneRegionSize * kFramesInFlight, 0, &mapped);
    m_boneMapped = static_cast<char*>(mapped);
}

void VulkanSkinning::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (auto& [key, instance] : m_instances) DestroyInstance(instance);
    m_instances.clear();
    for (auto& [key, source] : m_sources) {
        vkDestroyBuffer(device, source.buffer, nullptr);
        vkFreeMemory(device, source.memory, nullptr);
    }
    m_sources.clear();
    m_dispatches.clear();
    if (m_boneBuffer) {
        vkUnmapMemory(device, m_boneMemory);
        vkDestroyBuffer(device, m_boneBuffer, nullptr);
        vkFreeMemory(device, m_boneMemory, nullptr);
        m_boneBuffer = VK_NULL_HANDLE;
        m_boneMapped = nullptr;
    }
    if (m_descriptorPool) { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
    if (m_pipeline) { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_setLayout) { vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }
}

void VulkanSkinning::SetSource(const std::string& meshKey, const std::vector<SourceVertex>& vertices) {
    if (vertices.empty()) return;
    VkDevice device = VulkanContext::Get().GetDevice();
    auto existing = m_sources.find(meshKey);
    if (existing != m_sources.end()) {
        // Re-imported mesh: the GPU may still be reading the old data
        vkDeviceWaitIdle(device);
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (it->second.meshKey == meshKey) { DestroyInstance(it->second); it = m_instances.erase(it); } else { ++it; }
        }
        vkDestroyBuffer(device, existing->second.buffer, nullptr);
        vkFreeMemory(device, existing->second.memory, nullptr);
        m_sources.erase(existing);
    }

    const VkDeviceSize size = vertices.size() * sizeof(SourceVertex);
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VulkanBufferUtils::CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    staging, stagingMemory);
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &data);
    std::memcpy(data, vertices.data(), static_cast<size_t>(size));
    vkUnmapMemory(device, stagingMemory);

    Source source;
    source.vertexCount = static_cast<uint32_t>(vertices.size());
    VulkanBufferUtils::CreateBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, source.buffer, source.memory);
    VulkanBufferUtils::CopyBuffer(staging, source.buffer, size);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    m_sources[meshKey] = source;
}

void VulkanSkinning::BeginFrame(uint32_t frameInFlight) {
    m_frameIndex = frameInFlight % kFramesInFlight;
    ++m_frameNumber;
    m_boneCursor = 0;
    m_dispatches.clear();

    // Buffers neither skinned nor drawn for a few frames: every frame that used them has finished
    for (auto it = m_instances.begin(); it != m_instances.end();) {
        if (m_frameNumber - it->second.lastUsedFrame > kFramesInFlight + 1) {
            DestroyInstance(it->second);
            it = m_instances.erase(it);
        } else {
            ++it;
        }
    }
}

void VulkanSkinning::Submit(const void* nodeKey, const std::string& meshKey, const GPUMesh& indexSource,
                            const std::vector<glm::mat4>& boneMatrices, const glm::vec4& centerScale,
                            const glm::vec4& morphs) {
    auto sourceIt = m_sources.find(meshKey);
    if (sourceIt == m_sources.end() || boneMatrices.empty() || !m_pipeline) return;
    const Source& source = sourceIt->second;

    const uint32_t boneCount = static_cast<uint32_t>(boneMatrices.size());
    const VkDeviceSize boneBytes = boneCount * sizeof(glm::mat4);
    const VkDeviceSize offset = (m_boneCursor + m_boneAlignment - 1) / m_boneAlignment * m_boneAlignment;
    if (offset + boneBytes > m_boneRegionSize) {
        std::cerr << "[VulkanSkinning] Bone budget exceeded this frame; some characters keep their last pose" << std::endl;
        return;
    }
    m_boneCursor = offset + boneBytes;
    std::memcpy(m_boneMapped + m_frameIndex * m_boneRegionSize + offset, boneMatrices.data(), static_cast<size_t>(boneBytes));

    VkDevice device = VulkanContext::Get().GetDevice();
    Instance& instance = m_instances[nodeKey];
    const bool meshChanged = instance.meshKey != meshKey || instance.mesh.indexBuffer != indexSource.indexBuffer;
    if (instance.mesh.vertexBuffer && meshChanged) {
        // The character switched models; older frames may still draw the old buffer
        vkDeviceWaitIdle(device);
        DestroyInstance(instance);
        instance = Instance{};
    }
    if (!instance.mesh.vertexBuffer) {
        instance.meshKey = meshKey;
        const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(source.vertexCount) * sizeof(Vertex);
        VulkanBufferUtils::CreateBuffer(vertexBytes,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instance.mesh.vertexBuffer, instance.outputMemory);
        VulkanBufferUtils::CreateBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instance.previousBuffer, instance.previousMemory);
        instance.previousValid = false;
        instance.mesh.indexBuffer = indexSource.indexBuffer;
        instance.mesh.indexCount = indexSource.indexCount;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_setLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &instance.set) != VK_SUCCESS) {
            std::cerr << "[VulkanSkinning] Out of skinning descriptor sets (max " << kMaxCharacters << " characters)" << std::endl;
            DestroyInstance(instance);
            m_instances.erase(nodeKey);
            return;
        }
        instance.boneCapacity = 0;
    }
    if (instance.boneCapacity != boneCount) {
        instance.boneCapacity = boneCount;
        WriteInstanceSet(instance, source);
    }
    instance.lastUsedFrame = m_frameNumber;
    instance.lastSkinnedFrame = m_frameNumber;

    Dispatch dispatch{};
    dispatch.instance = &instance;
    dispatch.firstPose = !instance.previousValid;
    dispatch.vertexBytes = static_cast<VkDeviceSize>(source.vertexCount) * sizeof(Vertex);
    dispatch.boneOffsetBytes = static_cast<uint32_t>(m_frameIndex * m_boneRegionSize + offset);
    dispatch.vertexCount = source.vertexCount;
    dispatch.boneCount = boneCount;
    dispatch.centerScale = centerScale;
    dispatch.morphs = morphs;
    m_dispatches.push_back(dispatch);
}

void VulkanSkinning::WriteInstanceSet(Instance& instance, const Source& source) {
    VkDescriptorBufferInfo infos[3] = {
        { source.buffer, 0, VK_WHOLE_SIZE },
        { m_boneBuffer, 0, instance.boneCapacity * sizeof(glm::mat4) },
        { instance.mesh.vertexBuffer, 0, VK_WHOLE_SIZE },
    };
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t b = 0; b < 3; ++b) {
        writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[b].dstSet = instance.set;
        writes[b].dstBinding = b;
        writes[b].descriptorCount = 1;
        writes[b].descriptorType = (b == 1) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[b].pBufferInfo = &infos[b];
    }
    vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), 3, writes, 0, nullptr);
}

void VulkanSkinning::Record(VkCommandBuffer cmd) {
    if (m_dispatches.empty()) return;

    // Earlier frames may still be drawing these buffers: wait for their vertex reads (write after read)
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

    // Keep last frame's pose for motion vectors before it is overwritten
    bool copiedPrevious = false;
    for (const Dispatch& d : m_dispatches) {
        if (d.firstPose) continue;
        VkBufferCopy region{ 0, 0, d.vertexBytes };
        vkCmdCopyBuffer(cmd, d.instance->mesh.vertexBuffer, d.instance->previousBuffer, 1, &region);
        copiedPrevious = true;
    }
    if (copiedPrevious) {
        // The copies read the current buffers before the compute shader rewrites them
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    for (const Dispatch& d : m_dispatches) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &d.instance->set, 1, &d.boneOffsetBytes);
        SkinningPushConstants push{};
        push.centerScale = d.centerScale;
        push.morphs = d.morphs;
        push.vertexCount = d.vertexCount;
        push.boneCount = d.boneCount;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (d.vertexCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
    }

    // Skinned vertices must be written before any pass reads them as vertex input (or copies them)
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // A character's first pose is also its "previous" one (no motion on its first frame)
    bool copiedFirst = false;
    for (const Dispatch& d : m_dispatches) {
        if (!d.firstPose) continue;
        VkBufferCopy region{ 0, 0, d.vertexBytes };
        vkCmdCopyBuffer(cmd, d.instance->mesh.vertexBuffer, d.instance->previousBuffer, 1, &region);
        d.instance->previousValid = true;
        copiedFirst = true;
    }
    if (copiedFirst) {
        VkMemoryBarrier copyBarrier{};
        copyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &copyBarrier, 0, nullptr, 0, nullptr);
    }
}

VkBuffer VulkanSkinning::FindPrevious(const void* nodeKey) const {
    auto it = m_instances.find(nodeKey);
    if (it == m_instances.end()) return VK_NULL_HANDLE;
    const Instance& instance = it->second;
    // Only meaningful when the pose was updated this frame (otherwise both poses are the same)
    if (!instance.previousValid || instance.lastSkinnedFrame != m_frameNumber) return VK_NULL_HANDLE;
    return instance.previousBuffer;
}

const GPUMesh* VulkanSkinning::Find(const void* nodeKey) {
    auto it = m_instances.find(nodeKey);
    if (it == m_instances.end()) return nullptr;
    it->second.lastUsedFrame = m_frameNumber; // drawn this frame: keep it alive
    return &it->second.mesh;
}

void VulkanSkinning::DestroyInstance(Instance& instance) {
    VkDevice device = VulkanContext::Get().GetDevice();
    if (instance.set) { vkFreeDescriptorSets(device, m_descriptorPool, 1, &instance.set); instance.set = VK_NULL_HANDLE; }
    if (instance.mesh.vertexBuffer) { vkDestroyBuffer(device, instance.mesh.vertexBuffer, nullptr); instance.mesh.vertexBuffer = VK_NULL_HANDLE; }
    if (instance.outputMemory) { vkFreeMemory(device, instance.outputMemory, nullptr); instance.outputMemory = VK_NULL_HANDLE; }
    if (instance.previousBuffer) { vkDestroyBuffer(device, instance.previousBuffer, nullptr); instance.previousBuffer = VK_NULL_HANDLE; }
    if (instance.previousMemory) { vkFreeMemory(device, instance.previousMemory, nullptr); instance.previousMemory = VK_NULL_HANDLE; }
    instance.previousValid = false;
    instance.mesh.indexBuffer = VK_NULL_HANDLE; // owned by the shared mesh
}
