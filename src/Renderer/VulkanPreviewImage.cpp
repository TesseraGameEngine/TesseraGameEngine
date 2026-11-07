#include "VulkanPreviewImage.h"
#include "VulkanContext.h"
#include "VulkanOffscreen.h"
#include <imgui_impl_vulkan.h>
#include <stdexcept>

void VulkanPreviewImage::Create(uint32_t width, uint32_t height) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    m_width = width;
    m_height = height;
    m_written = false;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VulkanOffscreen::kDisplayFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) throw std::runtime_error("Failed to create preview image!");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, m_image, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = ctx.FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate preview image!");
    vkBindImageMemory(device, m_image, m_memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VulkanOffscreen::kDisplayFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_view) != VK_SUCCESS) throw std::runtime_error("Failed to create preview image view!");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) throw std::runtime_error("Failed to create preview sampler!");
    m_descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanPreviewImage::Destroy() {
    VkDevice device = VulkanContext::Get().GetDevice();
    if (m_descriptorSet) ImGui_ImplVulkan_RemoveTexture(m_descriptorSet);
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    if (m_view) vkDestroyImageView(device, m_view, nullptr);
    if (m_image) vkDestroyImage(device, m_image, nullptr);
    if (m_memory) vkFreeMemory(device, m_memory, nullptr);
    *this = VulkanPreviewImage{};
}

void VulkanPreviewImage::CopyFrom(VkCommandBuffer cmd, const VulkanOffscreen& source) {
    if (!m_image || source.GetWidth() != m_width || source.GetHeight() != m_height) return;
    source.CopyDisplayImageTo(cmd, m_image, m_written ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED);
    m_written = true;
}
