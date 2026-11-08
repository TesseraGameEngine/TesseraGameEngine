#include <stb_image.h>

#include "VulkanTexture.h"
#include "Bc7Encoder.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include <iostream>
#include <cstring>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h> // after <cstring>: it uses memcpy
#include <algorithm>
#include <vector>
#include <glm/gtc/packing.hpp>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

static uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(VulkanContext::Get().GetPhysicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

static void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer commandBuffer = ctx.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    ctx.EndSingleTimeCommands(commandBuffer);
}

static void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer commandBuffer = ctx.BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    ctx.EndSingleTimeCommands(commandBuffer);
}

// Downsamples mip 0 into every lower level with linear blits, leaving all levels in SHADER_READ_ONLY_OPTIMAL.
// Expects every level to be in TRANSFER_DST_OPTIMAL with mip 0 already filled.
static void GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels) {
    auto& ctx = VulkanContext::Get();
    VkCommandBuffer commandBuffer = ctx.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; ++i) {
        // Previous level becomes the blit source
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        vkCmdBlitImage(commandBuffer,
                       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        // Source level is finished; hand it to the fragment shader
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Last level was only ever a blit destination
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    ctx.EndSingleTimeCommands(commandBuffer);
}

bool VulkanTexture::InitFromMemory(const unsigned char* pixels, uint32_t width, uint32_t height, VkFormat format,
                                   uint32_t bytesPerPixel) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    m_width = width;
    m_height = height;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

    // Create Staging Buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanBufferUtils::CreateBuffer(imageSize,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Full mip chain, unless the format can't be linearly blitted on this GPU
    uint32_t mipLevels = 1;
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(ctx.GetPhysicalDevice(), format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
            uint32_t largest = (std::max)(width, height);
            while (largest > 1) {
                largest >>= 1;
                ++mipLevels;
            }
        }
    }

    // Create VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        std::cerr << "[VulkanTexture] Failed to create image!" << std::endl;
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_imageMemory) != VK_SUCCESS) {
        std::cerr << "[VulkanTexture] Failed to allocate image memory!" << std::endl;
        return false;
    }

    vkBindImageMemory(device, m_image, m_imageMemory, 0);

    // Copy staging buffer to GPU optimal image layout
    TransitionImageLayout(m_image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
    CopyBufferToImage(stagingBuffer, m_image, width, height);
    GenerateMipmaps(m_image, static_cast<int32_t>(width), static_cast<int32_t>(height), mipLevels);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    m_mipLevels = mipLevels;
    return CreateViewAndSampler(format, mipLevels);
}

std::shared_ptr<VulkanTexture> VulkanTexture::CreateFromPixels(const unsigned char* rgba, uint32_t width, uint32_t height) {
    auto tex = std::make_shared<VulkanTexture>();
    tex->m_clampToEdge = true;
    if (!tex->InitFromMemory(rgba, width, height, VK_FORMAT_R8G8B8A8_UNORM)) return nullptr;
    return tex;
}

void VulkanTexture::UpdatePixels(const unsigned char* rgba) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    vkDeviceWaitIdle(device); // frames in flight may be sampling it
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(m_width) * m_height * 4;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VulkanBufferUtils::CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingMemory);
    // Every mip back to a transfer target, then the same upload and mip chain as at creation
    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevels, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    ctx.EndSingleTimeCommands(cmd);
    CopyBufferToImage(staging, m_image, m_width, m_height);
    GenerateMipmaps(m_image, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height), m_mipLevels);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

bool VulkanTexture::CreateViewAndSampler(VkFormat format, uint32_t mipLevels) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    // Create Image View
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        std::cerr << "[VulkanTexture] Failed to create image view!" << std::endl;
        return false;
    }

    // Create Texture Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    const VkSamplerAddressMode address = m_clampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeU = address;
    samplerInfo.addressModeV = address;
    samplerInfo.addressModeW = address;
    float maxAniso = (std::min)(ctx.GetMaxSamplerAnisotropy(), 16.0f);
    samplerInfo.anisotropyEnable = (maxAniso > 1.0f) ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = (maxAniso > 1.0f) ? maxAniso : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        std::cerr << "[VulkanTexture] Failed to create sampler!" << std::endl;
        return false;
    }

    return true;
}

std::shared_ptr<VulkanTexture> VulkanTexture::CreateDefaultWhite() {
    auto texture = std::make_shared<VulkanTexture>();
    unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    texture->InitFromMemory(whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
    texture->m_filePath = "<default_white>";
    return texture;
}

std::shared_ptr<VulkanTexture> VulkanTexture::CreateDefaultNormal() {
    auto texture = std::make_shared<VulkanTexture>();
    // (128, 128, 255) maps to (0, 0, 1) flat tangent-space normal pointing directly outward
    unsigned char normalPixel[4] = { 128, 128, 255, 255 };
    texture->InitFromMemory(normalPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
    texture->m_filePath = "<default_normal>";
    return texture;
}

std::shared_ptr<VulkanTexture> VulkanTexture::LoadFromFile(const std::string& filepath, bool srgb) {
    int texWidth, texHeight, texChannels;
    std::string clean = filepath;
    std::replace(clean.begin(), clean.end(), '\\', '/');

    stbi_uc* pixels = stbi_load(clean.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels && (clean.rfind("/Users/", 0) == 0 || clean.rfind("/users/", 0) == 0)) {
        std::string withDrive = "C:" + clean;
        pixels = stbi_load(withDrive.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    }

    if (!pixels) {
        // Try fallback relative path
        std::string fallback = "../" + clean;
        pixels = stbi_load(fallback.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    }

    if (!pixels) {
        std::string fallback = "projects/Headland/" + clean;
        pixels = stbi_load(fallback.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    }

    if (!pixels) {
        std::string fallback = "projects/Headland/assets/" + clean;
        pixels = stbi_load(fallback.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    }

    if (!pixels) {
        std::cerr << "[VulkanTexture] Failed to load texture image from: " << filepath << " - Falling back to white texture." << std::endl;
        return CreateDefaultWhite();
    }

    auto texture = std::make_shared<VulkanTexture>();
    texture->m_filePath = filepath;
    bool ok = texture->InitFromMemory(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
                                      srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);
    stbi_image_free(pixels);

    if (!ok) {
        return CreateDefaultWhite();
    }
    std::cout << "[VulkanTexture] Loaded texture: " << filepath << " (" << texWidth << "x" << texHeight << ")" << std::endl;
    return texture;
}

namespace {

// The texture file actually present (the same fallbacks as LoadFromFile)
std::string ResolveTextureFile(const std::string& filepath) {
    std::string clean = filepath;
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::error_code ec;
    if (std::filesystem::exists(clean, ec)) return clean;
    if ((clean.rfind("/Users/", 0) == 0 || clean.rfind("/users/", 0) == 0) && std::filesystem::exists("C:" + clean, ec)) return "C:" + clean;
    if (std::filesystem::exists("../" + clean, ec)) return "../" + clean;
    if (std::filesystem::exists("projects/Headland/" + clean, ec)) return "projects/Headland/" + clean;
    if (std::filesystem::exists("projects/Headland/assets/" + clean, ec)) return "projects/Headland/assets/" + clean;
    return {};
}

// Halves an RGBA8 image with a 2x2 box filter (odd edges repeat the last texel)
std::vector<unsigned char> DownsampleRGBA(const std::vector<unsigned char>& src, int w, int h, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<unsigned char> dst(static_cast<size_t>(outW) * outH * 4);
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
            const int y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum = src[(static_cast<size_t>(y0) * w + x0) * 4 + c] + src[(static_cast<size_t>(y0) * w + x1) * 4 + c] +
                                src[(static_cast<size_t>(y1) * w + x0) * 4 + c] + src[(static_cast<size_t>(y1) * w + x1) * 4 + c];
                dst[(static_cast<size_t>(y) * outW + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

// Compresses one RGBA8 level: BC1 (8 bytes per 4x4 block), BC3, BC5 (red/green only) or BC7 (16 bytes)
std::vector<unsigned char> CompressLevel(const std::vector<unsigned char>& rgba, int w, int h, VkFormat format) {
    const int blocksX = (w + 3) / 4, blocksY = (h + 3) / 4;
    const int blockBytes = (format == VK_FORMAT_BC1_RGB_UNORM_BLOCK) ? 8 : 16;
    std::vector<unsigned char> out(static_cast<size_t>(blocksX) * blocksY * blockBytes);
    unsigned char block[64];
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    const int sx = std::min(bx * 4 + px, w - 1), sy = std::min(by * 4 + py, h - 1);
                    std::memcpy(block + (py * 4 + px) * 4, &rgba[(static_cast<size_t>(sy) * w + sx) * 4], 4);
                }
            }
            unsigned char* dst = &out[(static_cast<size_t>(by) * blocksX + bx) * blockBytes];
            if (format == VK_FORMAT_BC7_UNORM_BLOCK) {
                Bc7::EncodeBlockMode6(block, dst);
            } else if (format == VK_FORMAT_BC5_UNORM_BLOCK) {
                unsigned char rg[32];
                for (int t = 0; t < 16; ++t) { rg[t * 2] = block[t * 4]; rg[t * 2 + 1] = block[t * 4 + 1]; }
                stb_compress_bc5_block(dst, rg);
            } else {
                stb_compress_dxt_block(dst, block, format == VK_FORMAT_BC3_UNORM_BLOCK ? 1 : 0, STB_DXT_HIGHQUAL);
            }
        }
    }
    return out;
}

// Cache file: "TBC1", VkFormat, width, height, mip count, then (byte count, bytes) per level
constexpr uint32_t kCacheMagic = 0x31434254; // "TBC1"

const char* FormatName(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return "BC1";
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return "BC1 sRGB";
        case VK_FORMAT_BC3_SRGB_BLOCK: return "BC3 sRGB";
        case VK_FORMAT_BC7_SRGB_BLOCK: return "BC7 sRGB";
        case VK_FORMAT_BC3_UNORM_BLOCK: return "BC3";
        case VK_FORMAT_BC5_UNORM_BLOCK: return "BC5";
        case VK_FORMAT_BC7_UNORM_BLOCK: return "BC7";
        default: return "?";
    }
}

// mode: what the file is compressed as (part of the cache key), e.g. "bc13", "bc5", "bc7"
std::string CompressedCachePath(const std::string& file, const char* mode) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const auto stamp = std::filesystem::last_write_time(file, ec).time_since_epoch().count();
    const std::string identity = std::filesystem::absolute(file, ec).lexically_normal().string() + "|" +
                                 std::to_string(size) + "|" + std::to_string(stamp) + "|" + mode + "-v2";
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.bctex", static_cast<unsigned long long>(std::hash<std::string>{}(identity)));
    return "cache/textures/" + std::string(name);
}

bool ReadCompressedCache(const std::string& path, VkFormat& format, uint32_t& width, uint32_t& height,
                         std::vector<std::vector<unsigned char>>& mips) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint32_t header[5]{};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!in || header[0] != kCacheMagic || header[4] == 0 || header[4] > 16) return false;
    format = static_cast<VkFormat>(header[1]);
    width = header[2];
    height = header[3];
    mips.resize(header[4]);
    for (auto& level : mips) {
        uint32_t bytes = 0;
        in.read(reinterpret_cast<char*>(&bytes), sizeof(bytes));
        if (!in || bytes == 0 || bytes > (64u << 20)) return false;
        level.resize(bytes);
        in.read(reinterpret_cast<char*>(level.data()), bytes);
        if (!in) return false;
    }
    return true;
}

void WriteCompressedCache(const std::string& path, VkFormat format, uint32_t width, uint32_t height,
                          const std::vector<std::vector<unsigned char>>& mips) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    const uint32_t header[5] = { kCacheMagic, static_cast<uint32_t>(format), width, height, static_cast<uint32_t>(mips.size()) };
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (const auto& level : mips) {
        const uint32_t bytes = static_cast<uint32_t>(level.size());
        out.write(reinterpret_cast<const char*>(&bytes), sizeof(bytes));
        out.write(reinterpret_cast<const char*>(level.data()), bytes);
    }
}

} // namespace

std::shared_ptr<VulkanTexture> VulkanTexture::LoadCompressedFromFile(const std::string& filepath, TextureUsage usage) {
    const bool srgb = usage == TextureUsage::Color;
    if (s_compressionQuality == CompressionQuality::Off || !VulkanContext::Get().SupportsTextureCompressionBC()) {
        return LoadFromFile(filepath, srgb);
    }
    const std::string file = ResolveTextureFile(filepath);
    if (file.empty()) return LoadFromFile(filepath, srgb); // reports the failure and returns white

    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
    std::vector<std::vector<unsigned char>> mips;
    const char* mode = (usage == TextureUsage::NormalMap) ? "bc5"
                     : (s_compressionQuality == CompressionQuality::High) ? "bc7" : "bc13";
    const std::string cachePath = CompressedCachePath(file, mode);
    bool fromCache = ReadCompressedCache(cachePath, format, width, height, mips);

    if (!fromCache) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(file.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels) return LoadFromFile(filepath, srgb);
        if (w < 4 || h < 4) {
            stbi_image_free(pixels);
            return LoadFromFile(filepath, srgb); // too small to be worth compressing
        }
        std::vector<unsigned char> level(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);

        bool withAlpha = false;
        for (size_t i = 3; i < level.size() && !withAlpha; i += 4) withAlpha = level[i] < 255;
        if (usage == TextureUsage::NormalMap) format = VK_FORMAT_BC5_UNORM_BLOCK;
        else if (s_compressionQuality == CompressionQuality::High) format = VK_FORMAT_BC7_UNORM_BLOCK;
        else format = withAlpha ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);

        int lw = w, lh = h;
        while (true) {
            mips.push_back(CompressLevel(level, lw, lh, format));
            if (lw == 1 && lh == 1) break;
            int nw = 0, nh = 0;
            level = DownsampleRGBA(level, lw, lh, nw, nh);
            lw = nw;
            lh = nh;
        }
        WriteCompressedCache(cachePath, format, width, height, mips);
    }

    // The cache holds the blocks under the UNORM format; colour is sampled through the sRGB
    // view of the same blocks (identical data, decoded to linear by the hardware)
    if (srgb) {
        if (format == VK_FORMAT_BC1_RGB_UNORM_BLOCK) format = VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        else if (format == VK_FORMAT_BC3_UNORM_BLOCK) format = VK_FORMAT_BC3_SRGB_BLOCK;
        else if (format == VK_FORMAT_BC7_UNORM_BLOCK) format = VK_FORMAT_BC7_SRGB_BLOCK;
    }
    auto texture = std::make_shared<VulkanTexture>();
    texture->m_filePath = filepath;
    if (!texture->InitCompressed(format, width, height, mips)) return LoadFromFile(filepath, srgb);
    std::cout << "[VulkanTexture] Loaded compressed texture (" << FormatName(format)
              << (fromCache ? ", cached" : "") << "): " << filepath << " (" << width << "x" << height << ")" << std::endl;
    return texture;
}

bool VulkanTexture::InitCompressed(VkFormat format, uint32_t width, uint32_t height,
                                   const std::vector<std::vector<unsigned char>>& mips) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    m_width = width;
    m_height = height;
    const uint32_t mipLevels = static_cast<uint32_t>(mips.size());

    VkDeviceSize totalSize = 0;
    for (const auto& level : mips) totalSize += level.size();
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VulkanBufferUtils::CreateBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    stagingBuffer, stagingMemory);
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, totalSize, 0, &data);
    VkDeviceSize offset = 0;
    for (const auto& level : mips) {
        std::memcpy(static_cast<char*>(data) + offset, level.data(), level.size());
        offset += level.size();
    }
    vkUnmapMemory(device, stagingMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    bool ok = vkCreateImage(device, &imageInfo, nullptr, &m_image) == VK_SUCCESS;
    if (ok) {
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, m_image, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ok = vkAllocateMemory(device, &allocInfo, nullptr, &m_imageMemory) == VK_SUCCESS;
    }
    if (ok) {
        vkBindImageMemory(device, m_image, m_imageMemory, 0);
        TransitionImageLayout(m_image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

        // One copy region per level (block-compressed: extents may end mid-block at the image edge)
        std::vector<VkBufferImageCopy> regions(mipLevels);
        offset = 0;
        for (uint32_t m = 0; m < mipLevels; ++m) {
            VkBufferImageCopy& region = regions[m];
            region.bufferOffset = offset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
            region.imageExtent = { std::max(1u, width >> m), std::max(1u, height >> m), 1 };
            offset += mips[m].size();
        }
        VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
        ctx.EndSingleTimeCommands(cmd);
        TransitionImageLayout(m_image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
    }
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    if (!ok) {
        std::cerr << "[VulkanTexture] Failed to create compressed texture image" << std::endl;
        return false;
    }
    return CreateViewAndSampler(format, mipLevels);
}

std::shared_ptr<VulkanTexture> VulkanTexture::LoadHDRFromFile(const std::string& filepath) {
    std::string clean = filepath;
    std::replace(clean.begin(), clean.end(), '\\', '/');

    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf(clean.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::cerr << "[VulkanTexture] Failed to load HDR image: " << filepath << std::endl;
        return nullptr;
    }

    // Half floats: filterable on every GPU (32-bit float filtering is optional) and half the memory
    std::vector<uint16_t> halfPixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < halfPixels.size(); ++i) {
        halfPixels[i] = glm::packHalf1x16(std::min(pixels[i], 60000.0f));
    }
    stbi_image_free(pixels);

    auto texture = std::make_shared<VulkanTexture>();
    texture->m_filePath = filepath;
    if (!texture->InitFromMemory(reinterpret_cast<const unsigned char*>(halfPixels.data()),
                                 static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                 VK_FORMAT_R16G16B16A16_SFLOAT, 8)) {
        return nullptr;
    }
    std::cout << "[VulkanTexture] Loaded HDR image: " << filepath << " (" << width << "x" << height << ")" << std::endl;
    return texture;
}

std::shared_ptr<VulkanTexture> VulkanTexture::LoadColorLUT(const std::string& filepath, uint32_t& outSize) {
    std::string clean = filepath;
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::string ext = std::filesystem::path(clean).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    uint32_t size = 0;
    std::vector<unsigned char> strip;
    if (ext == ".cube") {
        // Text format: "LUT_3D_SIZE N", then N^3 lines of "r g b" with red changing fastest
        std::ifstream file(clean);
        if (!file.is_open()) {
            std::cerr << "[VulkanTexture] Failed to open LUT: " << filepath << std::endl;
            return nullptr;
        }
        std::vector<float> values;
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream in(line);
            std::string first;
            in >> first;
            if (first == "LUT_3D_SIZE") {
                in >> size;
            } else if (!first.empty() && (std::isdigit(static_cast<unsigned char>(first[0])) || first[0] == '-' || first[0] == '.')) {
                float g = 0.0f, b = 0.0f;
                in >> g >> b;
                values.push_back(std::stof(first));
                values.push_back(g);
                values.push_back(b);
            }
            // TITLE, DOMAIN_MIN/MAX and LUT_1D_SIZE lines are ignored (domain assumed 0..1)
        }
        if (size < 2 || size > 128 || values.size() != static_cast<size_t>(size) * size * size * 3) {
            std::cerr << "[VulkanTexture] Not a valid 3D .cube LUT: " << filepath << std::endl;
            return nullptr;
        }
        strip.resize(static_cast<size_t>(size) * size * size * 4);
        for (uint32_t b = 0; b < size; ++b) {
            for (uint32_t g = 0; g < size; ++g) {
                for (uint32_t r = 0; r < size; ++r) {
                    const size_t src = ((static_cast<size_t>(b) * size + g) * size + r) * 3;
                    const size_t dst = (static_cast<size_t>(g) * size * size + b * size + r) * 4;
                    for (int c = 0; c < 3; ++c) {
                        strip[dst + c] = static_cast<unsigned char>(std::clamp(values[src + c], 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                    strip[dst + 3] = 255;
                }
            }
        }
    } else {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(clean.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            std::cerr << "[VulkanTexture] Failed to load LUT image: " << filepath << std::endl;
            return nullptr;
        }
        if (height < 2 || width != height * height) {
            std::cerr << "[VulkanTexture] LUT image must be N*N wide and N high (e.g. 256x16, 1024x32): "
                      << filepath << " is " << width << "x" << height << std::endl;
            stbi_image_free(pixels);
            return nullptr;
        }
        size = static_cast<uint32_t>(height);
        strip.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
    }

    auto texture = std::make_shared<VulkanTexture>();
    texture->m_filePath = filepath;
    // UNORM, not sRGB: the shader looks values up in display (gamma) space
    if (!texture->InitFromMemory(strip.data(), size * size, size, VK_FORMAT_R8G8B8A8_UNORM)) {
        return nullptr;
    }
    outSize = size;
    std::cout << "[VulkanTexture] Loaded colour LUT: " << filepath << " (" << size << "^3)" << std::endl;
    return texture;
}

void VulkanTexture::Cleanup() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_imageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_imageMemory, nullptr);
        m_imageMemory = VK_NULL_HANDLE;
    }
}
