#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include <cstring>
#include <stdexcept>

void VulkanBufferUtils::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                     VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = ctx.FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VulkanBufferUtils::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer commandBuffer = ctx.BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    ctx.EndSingleTimeCommands(commandBuffer);
}

GPUMesh VulkanBufferUtils::UploadMesh(const MeshData& meshData) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    GPUMesh gpuMesh;

    // 1. Vertex Buffer
    VkDeviceSize vertexSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();

    VkBuffer stagingVertexBuffer;
    VkDeviceMemory stagingVertexBufferMemory;
    CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingVertexBuffer, stagingVertexBufferMemory);

    void* data;
    vkMapMemory(device, stagingVertexBufferMemory, 0, vertexSize, 0, &data);
    memcpy(data, meshData.vertices.data(), (size_t)vertexSize);
    vkUnmapMemory(device, stagingVertexBufferMemory);

    CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 gpuMesh.vertexBuffer, gpuMesh.vertexBufferMemory);

    CopyBuffer(stagingVertexBuffer, gpuMesh.vertexBuffer, vertexSize);

    vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
    vkFreeMemory(device, stagingVertexBufferMemory, nullptr);

    // 2. Index Buffer
    VkDeviceSize indexSize = sizeof(meshData.indices[0]) * meshData.indices.size();

    VkBuffer stagingIndexBuffer;
    VkDeviceMemory stagingIndexBufferMemory;
    CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingIndexBuffer, stagingIndexBufferMemory);

    vkMapMemory(device, stagingIndexBufferMemory, 0, indexSize, 0, &data);
    memcpy(data, meshData.indices.data(), (size_t)indexSize);
    vkUnmapMemory(device, stagingIndexBufferMemory);

    CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 gpuMesh.indexBuffer, gpuMesh.indexBufferMemory);

    CopyBuffer(stagingIndexBuffer, gpuMesh.indexBuffer, indexSize);

    vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
    vkFreeMemory(device, stagingIndexBufferMemory, nullptr);

    gpuMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    gpuMesh.submeshes = meshData.submeshes;
    return gpuMesh;
}

void VulkanBufferUtils::UpdateVertexBuffer(GPUMesh& gpuMesh, const std::vector<Vertex>& vertices) {
    if (gpuMesh.vertexBuffer == VK_NULL_HANDLE || vertices.empty()) return;
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();

    if (gpuMesh.dynamicStagingBuffer == VK_NULL_HANDLE || gpuMesh.dynamicStagingSize < vertexSize) {
        if (gpuMesh.dynamicStagingBuffer != VK_NULL_HANDLE) {
            if (gpuMesh.mappedStagingData) {
                vkUnmapMemory(device, gpuMesh.dynamicStagingMemory);
                gpuMesh.mappedStagingData = nullptr;
            }
            vkDestroyBuffer(device, gpuMesh.dynamicStagingBuffer, nullptr);
            vkFreeMemory(device, gpuMesh.dynamicStagingMemory, nullptr);
        }
        gpuMesh.dynamicStagingSize = vertexSize;
        CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     gpuMesh.dynamicStagingBuffer, gpuMesh.dynamicStagingMemory);
        vkMapMemory(device, gpuMesh.dynamicStagingMemory, 0, vertexSize, 0, &gpuMesh.mappedStagingData);
    }

    if (gpuMesh.mappedStagingData) {
        memcpy(gpuMesh.mappedStagingData, vertices.data(), (size_t)vertexSize);
        CopyBuffer(gpuMesh.dynamicStagingBuffer, gpuMesh.vertexBuffer, vertexSize);
    }
}

