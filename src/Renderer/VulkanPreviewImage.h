#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanOffscreen;

// A small image the editor shows with ImGui (a material preview), filled on the GPU by copying a
// render target's finished (tone-mapped) image into it.
class VulkanPreviewImage {
public:
    void Create(uint32_t width, uint32_t height);
    void Destroy(); // wait for the GPU first
    // Records the copy of source's display image (same size) into this image
    void CopyFrom(VkCommandBuffer cmd, const VulkanOffscreen& source);
    void* GetTextureId() const { return m_written ? reinterpret_cast<void*>(m_descriptorSet) : nullptr; }
    bool Valid() const { return m_image != VK_NULL_HANDLE; }

private:
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_memory{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    uint32_t m_width{0};
    uint32_t m_height{0};
    bool m_written{false};
};
