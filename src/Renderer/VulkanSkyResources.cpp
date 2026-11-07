#include "VulkanSkyResources.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

uint32_t HashU(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

float Hash2(int x, int y, uint32_t seed) {
    return static_cast<float>(HashU(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ seed) & 0xffffffu) / 16777215.0f;
}

float Hash3(int x, int y, int z, uint32_t seed) {
    return static_cast<float>(HashU(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                                    static_cast<uint32_t>(z) * 83492791u ^ seed) & 0xffffffu) / 16777215.0f;
}

int Wrap(int v, int period) {
    return ((v % period) + period) % period;
}

// Value noise that tiles every `period` cells
float TilingValueNoise2(float x, float y, int period, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    auto h = [&](int a, int b) { return Hash2(Wrap(a, period), Wrap(b, period), seed); };
    const float a = h(ix, iy) + (h(ix + 1, iy) - h(ix, iy)) * fx;
    const float b = h(ix, iy + 1) + (h(ix + 1, iy + 1) - h(ix, iy + 1)) * fx;
    return a + (b - a) * fy;
}

// Inverted Worley (cellular) noise that tiles every `period` cells: 1 at feature points, puffy
float TilingWorley3(float x, float y, float z, int period, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y)), iz = static_cast<int>(std::floor(z));
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                const int wx = Wrap(cx, period), wy = Wrap(cy, period), wz = Wrap(cz, period);
                const float px = cx + Hash3(wx, wy, wz, seed);
                const float py = cy + Hash3(wx, wy, wz, seed + 1u);
                const float pz = cz + Hash3(wx, wy, wz, seed + 2u);
                const float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
                best = std::min(best, d);
            }
        }
    }
    return 1.0f - std::min(std::sqrt(best), 1.0f);
}

std::vector<char> ReadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open shader: " + path);
    std::vector<char> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

// ---- The atmosphere on the CPU (same numbers as atmosphere_common.glsl) ----
constexpr float kGroundRadius = 6360.0f;
constexpr float kTopRadius = 6460.0f;

glm::vec3 Extinction(float h) {
    const glm::vec3 rayleigh = glm::vec3(5.802f, 13.558f, 33.1f) * 1e-3f;
    const glm::vec3 ozone = glm::vec3(0.650f, 1.881f, 0.085f) * 1e-3f;
    const float ozoneDensity = std::max(0.0f, 1.0f - std::abs(h - 25.0f) / 15.0f);
    return rayleigh * std::exp(-h / 8.0f) + glm::vec3(4.40e-3f * std::exp(-h / 1.2f)) + ozone * ozoneDensity;
}

} // namespace

glm::vec3 VulkanSkyResources::SunTransmittance(const glm::vec3& sunDir, float cameraHeight) {
    const float r = kGroundRadius + std::max(cameraHeight / 1000.0f, 0.001f);
    const float mu = glm::clamp(glm::normalize(sunDir).y, -1.0f, 1.0f);
    // Blocked by the ground below the horizon
    if (mu < 0.0f && r * r * (mu * mu - 1.0f) + kGroundRadius * kGroundRadius >= 0.0f) return glm::vec3(0.0f);
    const float d = r * r * (mu * mu - 1.0f) + kTopRadius * kTopRadius;
    const float length = -r * mu + std::sqrt(std::max(d, 0.0f));
    const int steps = 40;
    const float dt = length / steps;
    glm::vec3 depth(0.0f);
    for (int i = 0; i < steps; ++i) {
        const float t = (i + 0.5f) * dt;
        const float ri = std::sqrt(r * r + t * t + 2.0f * r * mu * t);
        depth += Extinction(ri - kGroundRadius) * dt;
    }
    return glm::exp(-depth);
}

void VulkanSkyResources::CreateImage(Image& image, VkImageType type, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage) {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = type;
    info.format = format;
    info.extent = extent;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &image.image) != VK_SUCCESS) throw std::runtime_error("Failed to create sky image!");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image.image, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = VulkanContext::Get().FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &image.memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate sky image!");
    vkBindImageMemory(device, image.image, image.memory, 0);
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image.image;
    view.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &view, nullptr, &image.view) != VK_SUCCESS) throw std::runtime_error("Failed to create sky image view!");
}

void VulkanSkyResources::UploadR8(Image& image, VkExtent3D extent, const uint8_t* texels, size_t size) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VulkanBufferUtils::CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, texels, size);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = extent;
    vkCmdCopyBufferToImage(cmd, staging, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    ctx.EndSingleTimeCommands(cmd);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void VulkanSkyResources::DestroyImage(Image& image) {
    VkDevice device = VulkanContext::Get().GetDevice();
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = Image{};
}

void VulkanSkyResources::Init() {
    VkDevice device = VulkanContext::Get().GetDevice();

    // Coverage: tiling 5-octave value noise (the cloud layer's shapes; its range matches the
    // thresholds in CloudDensityAt)
    {
        std::vector<uint8_t> texels(kCoverageSize * kCoverageSize);
        for (uint32_t y = 0; y < kCoverageSize; ++y) {
            for (uint32_t x = 0; x < kCoverageSize; ++x) {
                float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
                int period = static_cast<int>(kCoveragePeriod);
                for (int octave = 0; octave < 5; ++octave) {
                    const float scale = static_cast<float>(period) / kCoverageSize;
                    sum += amplitude * TilingValueNoise2(x * scale, y * scale, period, 17u + octave * 101u);
                    total += amplitude;
                    amplitude *= 0.5f;
                    period *= 2;
                }
                texels[y * kCoverageSize + x] = static_cast<uint8_t>(std::clamp(sum / total, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        CreateImage(m_coverage, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, { kCoverageSize, kCoverageSize, 1 },
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        UploadR8(m_coverage, { kCoverageSize, kCoverageSize, 1 }, texels.data(), texels.size());
    }

    // Detail: tiling Worley fbm (billowy cauliflower shapes that erode the cloud edges)
    {
        const uint32_t n = kDetailSize;
        std::vector<uint8_t> texels(static_cast<size_t>(n) * n * n);
        for (uint32_t z = 0; z < n; ++z) {
            for (uint32_t y = 0; y < n; ++y) {
                for (uint32_t x = 0; x < n; ++x) {
                    float v = 0.0f;
                    const int periods[3] = { 4, 8, 16 };
                    const float weights[3] = { 0.625f, 0.25f, 0.125f };
                    for (int o = 0; o < 3; ++o) {
                        const float s = static_cast<float>(periods[o]) / n;
                        v += weights[o] * TilingWorley3(x * s, y * s, z * s, periods[o], 1000u + o * 37u);
                    }
                    texels[(static_cast<size_t>(z) * n + y) * n + x] = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
        CreateImage(m_detail, VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM, { n, n, n }, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        UploadR8(m_detail, { n, n, n }, texels.data(), texels.size());
    }

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = 0.0f;
    if (vkCreateSampler(device, &sampler, nullptr, &m_repeatSampler) != VK_SUCCESS) throw std::runtime_error("Failed to create sky sampler!");
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sampler, nullptr, &m_clampSampler) != VK_SUCCESS) throw std::runtime_error("Failed to create sky sampler!");

    // Sky-view LUTs: written by the compute pass, sampled by the sky, kept in GENERAL
    for (Image& lut : m_luts) {
        CreateImage(lut, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, { kLutWidth, kLutHeight, 1 },
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        VkCommandBuffer cmd = VulkanContext::Get().BeginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = lut.image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkClearColorValue black{};
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cmd, lut.image, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &range);
        VulkanContext::Get().EndSingleTimeCommands(cmd);
    }
    CreateLutPipeline();
}

void VulkanSkyResources::CreateLutPipeline() {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_lutSetLayout) != VK_SUCCESS) throw std::runtime_error("Failed to create sky LUT layout!");
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_lutPool) != VK_SUCCESS) throw std::runtime_error("Failed to create sky LUT pool!");
    const VkDescriptorSetLayout layouts[2] = { m_lutSetLayout, m_lutSetLayout };
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_lutPool;
    alloc.descriptorSetCount = 2;
    alloc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &alloc, m_lutSets) != VK_SUCCESS) throw std::runtime_error("Failed to allocate sky LUT sets!");
    for (int i = 0; i < 2; ++i) {
        VkDescriptorImageInfo image{ VK_NULL_HANDLE, m_luts[i].view, VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_lutSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec4) * 2 };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_lutSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_lutPipelineLayout) != VK_SUCCESS) throw std::runtime_error("Failed to create sky LUT pipeline layout!");
    const std::vector<char> code = ReadSpirv("shaders/sky_view_lut_comp.spv");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) throw std::runtime_error("Failed to create sky LUT shader!");
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = m_lutPipelineLayout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_lutPipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create sky LUT pipeline!");
}

void VulkanSkyResources::RecordSkyViewLut(VkCommandBuffer cmd, int slot, const glm::vec3& sunDir, float cameraHeight) {
    if (!m_lutPipeline) return;
    slot %= 2;
    // Earlier work sampling the LUT finishes before it is rewritten
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_luts[slot].image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lutPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lutPipelineLayout, 0, 1, &m_lutSets[slot], 0, nullptr);
    const glm::vec4 push[2] = { glm::vec4(glm::normalize(sunDir), std::max(cameraHeight, 1.0f) / 1000.0f),
                                glm::vec4(static_cast<float>(kLutWidth), static_cast<float>(kLutHeight), 0.0f, 0.0f) };
    vkCmdPushConstants(cmd, m_lutPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
    vkCmdDispatch(cmd, (kLutWidth + 7) / 8, (kLutHeight + 7) / 8, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanSkyResources::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    DestroyImage(m_coverage);
    DestroyImage(m_detail);
    for (Image& lut : m_luts) DestroyImage(lut);
    if (m_repeatSampler) vkDestroySampler(device, m_repeatSampler, nullptr);
    if (m_clampSampler) vkDestroySampler(device, m_clampSampler, nullptr);
    if (m_lutPipeline) vkDestroyPipeline(device, m_lutPipeline, nullptr);
    if (m_lutPipelineLayout) vkDestroyPipelineLayout(device, m_lutPipelineLayout, nullptr);
    if (m_lutPool) vkDestroyDescriptorPool(device, m_lutPool, nullptr);
    if (m_lutSetLayout) vkDestroyDescriptorSetLayout(device, m_lutSetLayout, nullptr);
    m_repeatSampler = m_clampSampler = VK_NULL_HANDLE;
    m_lutPipeline = VK_NULL_HANDLE;
    m_lutPipelineLayout = VK_NULL_HANDLE;
    m_lutPool = VK_NULL_HANDLE;
    m_lutSetLayout = VK_NULL_HANDLE;
}
