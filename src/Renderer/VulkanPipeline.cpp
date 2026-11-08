#include "VulkanPipeline.h"
#include "VulkanContext.h"
#include "VulkanGpuProfiler.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include "Scene/ParticleSheets.h"
#include "Scene/Nodes/3D/DecalNode.h"
#include "Scene/Nodes/3D/TerrainNode.h"
#include "Scene/FoliageMeshes.h"
#include "ShaderGraph.h"
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <future>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <limits>
#include <filesystem>

static std::vector<char> ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::string fallback = "../" + filename;
        file.open(fallback, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            file.open("build/Debug/" + filename, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open shader file: " + filename);
            }
        }
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

void VulkanPipeline::Init(VkRenderPass renderPass, VkRenderPass prepassRenderPass,
                          const std::string& vertShaderPath, const std::string& fragShaderPath) {
    m_defaultTexture = VulkanTexture::CreateDefaultWhite();
    m_defaultNormalTexture = VulkanTexture::CreateDefaultNormal();
    m_prepassRenderPass = prepassRenderPass;
    // Per-view textures default to white ("no occlusion") until each view hands over its own
    m_defaultViewTexture = VulkanTexture::CreateDefaultWhite();
    m_giVolume.Init(); // 1x1x1 black until a bake is loaded
    m_vxgiVolume.Init(); // 32x32x32 mipmapped black until VXGI is enabled
    m_lightmapAtlas.Init(); // 1x1 black until lightmaps are loaded
    CreateOceanGrid();
    for (uint32_t t = 0; t < kViewTextureCount; ++t) {
        const uint32_t binding = kViewTextureBindings[t];
        const bool volume = binding >= kGIVolumeBinding && binding < kGIVolumeBinding + 3;
        for (int i = 0; i < kViewCount; ++i) {
            if (binding == kVXGIVolumeBinding) {
                m_viewTextureViews[t][i] = m_vxgiVolume.GetView();
                m_viewTextureSamplers[t][i] = m_vxgiVolume.GetSampler();
                continue;
            }
            if (binding == kLightmapBinding) {
                m_viewTextureViews[t][i] = m_lightmapAtlas.GetView();
                m_viewTextureSamplers[t][i] = m_lightmapAtlas.GetSampler();
                continue;
            }
            m_viewTextureViews[t][i] = volume ? m_giVolume.GetView(binding - kGIVolumeBinding) : m_defaultViewTexture->GetImageView();
            m_viewTextureSamplers[t][i] = volume ? m_giVolume.GetSampler() : m_defaultViewTexture->GetSampler();
        }
    }
    // Shadow maps must exist before descriptor sets reference them
    for (auto& shadowMap : m_shadowMaps) {
        shadowMap.Init(kShadowMapResolution);
    }
    for (auto& shadowMap : m_localShadowMaps) {
        shadowMap.Init(kLocalShadowResolution, kLocalShadowLayers);
    }
    for (auto& shelter : m_shelterMaps) {
        shelter.Init(kShelterResolution, 1);
    }
    m_sky.Init();
    for (auto& probes : m_reflectionProbes) {
        probes.Init();
    }
    CreateDescriptorSetLayout();
    CreateUniformBuffer();
    CreateFrameStorage();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateGraphicsPipeline(renderPass, vertShaderPath, fragShaderPath);

    // The other shaders sit next to the main vertex shader
    std::string shaderDir;
    size_t slash = vertShaderPath.find_last_of("/\\");
    if (slash != std::string::npos) {
        shaderDir = vertShaderPath.substr(0, slash + 1);
    }
    CreateShadowPipeline(shaderDir + "shadow_vert.spv");
    CreateSkyPipeline(renderPass, shaderDir + "sky_vert.spv", shaderDir + "sky_frag.spv");
    CreateOcclusionResources();
}

void VulkanPipeline::Cleanup() {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    DestroyOcclusionResources();
    m_giVolume.Cleanup();
    m_vxgiVolume.Cleanup();
    for (auto& entry : m_lightmapMeshes) entry.second.mesh.Cleanup(device);
    m_lightmapMeshes.clear();
    m_lightmapAtlas.Cleanup();
    m_oceanGrid.Cleanup(device);
    ReleaseParticleUploads(true);
    for (VkPipeline* pipeline : { &m_fluidDepthPipeline, &m_fluidThicknessPipeline, &m_fluidSmoothPipeline, &m_fluidCompositePipeline, &m_precipPipeline,
                                  &m_cloudVolumePipeline, &m_fluidFoamPipeline, &m_fluidSmoothWidePipeline,
                                  &m_decalPipelines[0], &m_decalPipelines[1], &m_decalPipelines[2],
                                  &m_particlePipelines[0], &m_particlePipelines[1], &m_particlePipelines[2], &m_particleMeshPipeline }) {
        if (*pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    m_oceanGrid = GPUMesh{};
    if (m_oceanPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_oceanPipeline, nullptr);
        m_oceanPipeline = VK_NULL_HANDLE;
    }

    for (auto& pair : m_textures) {
        if (pair.second) pair.second->Cleanup();
    }
    m_textures.clear();
    for (auto& pair : m_runtimeTextures) {
        if (pair.second) pair.second->Cleanup();
    }
    m_runtimeTextures.clear();
    m_textureDescriptorSets.clear();

    if (m_defaultTexture) {
        m_defaultTexture->Cleanup();
        m_defaultTexture = nullptr;
    }
    if (m_defaultNormalTexture) {
        m_defaultNormalTexture->Cleanup();
        m_defaultNormalTexture = nullptr;
    }
    if (m_defaultViewTexture) {
        m_defaultViewTexture->Cleanup();
        m_defaultViewTexture = nullptr;
    }
    if (m_prepassPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_prepassPipeline, nullptr);
        m_prepassPipeline = VK_NULL_HANDLE;
    }

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineWireframe != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineWireframe, nullptr);
        m_graphicsPipelineWireframe = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineLines != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineLines, nullptr);
        m_graphicsPipelineLines = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineParticlesAlpha != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineParticlesAlpha, nullptr);
        m_graphicsPipelineParticlesAlpha = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineTransparent != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineTransparent, nullptr);
        m_graphicsPipelineTransparent = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineRT != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineRT, nullptr);
        m_graphicsPipelineRT = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineTransparentRT != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipelineTransparentRT, nullptr);
        m_graphicsPipelineTransparentRT = VK_NULL_HANDLE;
    }
    if (m_emptySetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_emptySetLayout, nullptr);
        m_emptySetLayout = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }
    if (m_shadowCutoutPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_shadowCutoutPipeline, nullptr);
        m_shadowCutoutPipeline = VK_NULL_HANDLE;
    }
    if (m_skyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_skyPipeline, nullptr);
        m_skyPipeline = VK_NULL_HANDLE;
    }
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_shadowPipelineLayout, nullptr);
        m_shadowPipelineLayout = VK_NULL_HANDLE;
    }
    for (auto& shadowMap : m_shadowMaps) {
        shadowMap.Cleanup();
    }
    for (auto& shelter : m_shelterMaps) {
        shelter.Cleanup();
    }
    m_sky.Cleanup();
    for (auto& shadowMap : m_localShadowMaps) {
        shadowMap.Cleanup();
    }
    for (auto& probes : m_reflectionProbes) {
        probes.Cleanup();
    }
    if (m_environmentTexture) {
        m_environmentTexture->Cleanup();
        m_environmentTexture = nullptr;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    for (int i = 0; i < kViewCount; ++i) {
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_uniformBuffers[i], nullptr);
            m_uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_uniformBuffersMemory[i], nullptr);
            m_uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
        m_uniformBuffersMapped[i] = nullptr;
    }
    DestroyFrameStorage();
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanPipeline::CreateDescriptorSetLayout() {
    auto& ctx = VulkanContext::Get();

    VkDescriptorSetLayoutBinding bindings[kMaterialBindingCount]{};

    // Binding 0: UniformBufferObject (Camera, View/Proj, Lights)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Bindings 1-13: material maps and per-view textures; 14-16: instance / light storage buffers
    for (uint32_t b = 1; b < kMaterialBindingCount; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = IsStorageBinding(b) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                   : (b == kMaterialExtBinding) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = (b == kInstanceBinding) ? VK_SHADER_STAGE_VERTEX_BIT
                               : (b == kShelterBinding) ? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                                                        : VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[b].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kMaterialBindingCount;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx.GetDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void VulkanPipeline::CreateUniformBuffer() {
    auto& ctx = VulkanContext::Get();
    // One region per frame in flight, each aligned for use as a dynamic offset
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.GetPhysicalDevice(), &props);
    const VkDeviceSize alignment = std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 1);
    m_uboStride = (sizeof(UniformBufferObject) + alignment - 1) / alignment * alignment;
    VkDeviceSize bufferSize = m_uboStride * kFramesInFlight;

    for (int i = 0; i < kViewCount; ++i) {
        VulkanBufferUtils::CreateBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i],
            m_uniformBuffersMemory[i]
        );

        vkMapMemory(ctx.GetDevice(), m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
    }
}

void VulkanPipeline::BindMaterialSet(VkCommandBuffer cmd, VkPipelineLayout layout, VkDescriptorSet set) const {
    auto slotIt = m_setSlots.find(set);
    const uint32_t region = m_frameIndex * kViewCount + static_cast<uint32_t>(slotIt != m_setSlots.end() ? slotIt->second : 0);
    // Dynamic offsets in binding order: UBO (0), instances (14), light clusters (15), lights (16)
    const uint32_t offsets[4] = {
        CurrentUboOffset(),
        static_cast<uint32_t>(m_instanceStorage.regionSize * m_frameIndex),
        static_cast<uint32_t>(m_clusterStorage.regionSize * region),
        static_cast<uint32_t>(m_lightStorage.regionSize * region),
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 4, offsets);
}

void VulkanPipeline::SetViewRayTracing(int viewSlot, VkDescriptorSet set) {
    if (viewSlot < 0 || viewSlot >= kViewCount) return;
    m_viewRTSets[viewSlot] = (m_graphicsPipelineRT != VK_NULL_HANDLE) ? set : VK_NULL_HANDLE;
}

void VulkanPipeline::BindForwardPipeline(VkCommandBuffer cmd, VkPipeline pipeline) const {
    VkPipeline bound = pipeline;
    const VkDescriptorSet rtSet = (m_activeRTView >= 0) ? m_viewRTSets[m_activeRTView] : VK_NULL_HANDLE;
    if (rtSet != VK_NULL_HANDLE) {
        if (pipeline == m_graphicsPipeline && m_graphicsPipelineRT != VK_NULL_HANDLE) {
            bound = m_graphicsPipelineRT;
        } else if (pipeline == m_graphicsPipelineTransparent && m_graphicsPipelineTransparentRT != VK_NULL_HANDLE) {
            bound = m_graphicsPipelineTransparentRT;
        }
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bound);
    if (bound != pipeline) {
        // Set 1 may have been disturbed by a pipeline with a different layout (decals, particles)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &rtSet, 0, nullptr);
    }
}

void VulkanPipeline::BeginFrame(uint32_t frameInFlight) {
    m_frameIndex = frameInFlight % kFramesInFlight;
    ++m_frameCounter;
    m_particleUploadCursor = 0;
    ReleaseParticleUploads(false);
    m_instanceCursor = 0;
    m_materialExtIndices.clear();
}

void VulkanPipeline::SetViewLights(int viewSlot, const std::vector<LightData>& lights) {
    m_viewLights[LightSetFor(viewSlot)] = lights;
    if (m_viewLights[LightSetFor(viewSlot)].size() > static_cast<size_t>(kMaxSceneLights)) {
        m_viewLights[LightSetFor(viewSlot)].resize(kMaxSceneLights);
    }
}

void VulkanPipeline::SetBakedGIVolume(const glm::uvec3& dims, const std::vector<glm::vec4>& red,
                                      const std::vector<glm::vec4>& green, const std::vector<glm::vec4>& blue) {
    m_giVolume.Upload(dims, red, green, blue);
    for (int slot = 0; slot < kViewCount; ++slot) {
        for (uint32_t c = 0; c < 3; ++c) {
            SetViewTexture(slot, kGIVolumeBinding + c, m_giVolume.GetView(static_cast<int>(c)), m_giVolume.GetSampler());
        }
    }
}

void VulkanPipeline::CreateOceanGrid() {
    // Rings around the camera, each a little wider than the last so the cells stay roughly
    // square: dense near the camera, coarse towards the horizon. y holds the cell size there.
    constexpr uint32_t kSegments = 192;
    constexpr float kInnerRadius = 0.1f;
    constexpr float kOuterRadius = 6000.0f;
    const float growth = 1.0f + 6.2831853f / kSegments;
    MeshData grid;
    grid.vertices.push_back(Vertex{ glm::vec3(0.0f, kInnerRadius * (growth - 1.0f), 0.0f) });
    uint32_t rings = 0;
    for (float radius = kInnerRadius; radius < kOuterRadius * growth; radius *= growth, ++rings) {
        const float spacing = radius * (growth - 1.0f);
        for (uint32_t s = 0; s < kSegments; ++s) {
            const float angle = 6.2831853f * s / kSegments;
            grid.vertices.push_back(Vertex{ glm::vec3(std::cos(angle) * radius, spacing, std::sin(angle) * radius) });
        }
    }
    auto ringVertex = [&](uint32_t ring, uint32_t segment) { return 1 + ring * kSegments + segment % kSegments; };
    for (uint32_t s = 0; s < kSegments; ++s) {
        grid.indices.insert(grid.indices.end(), { 0u, ringVertex(0, s + 1), ringVertex(0, s) });
    }
    for (uint32_t r = 0; r + 1 < rings; ++r) {
        for (uint32_t s = 0; s < kSegments; ++s) {
            const uint32_t a = ringVertex(r, s), b = ringVertex(r, s + 1);
            const uint32_t c = ringVertex(r + 1, s), d = ringVertex(r + 1, s + 1);
            grid.indices.insert(grid.indices.end(), { a, b, c, b, d, c });
        }
    }
    m_oceanGrid = VulkanBufferUtils::UploadMesh(grid);
}

void VulkanPipeline::DrawOcean(VkCommandBuffer cmd, const Scene& scene, const Node& node, int viewSlot) {
    GpuProfileScope gpuScope(cmd, "Ocean");
    if (m_oceanPipeline == VK_NULL_HANDLE || m_oceanGrid.indexCount == 0) return;
    // Must match OceanParams in ocean_common.glsl
    struct OceanPushConstants {
        glm::vec4 surface, wind, bounds, shallowColor, deepColor, foamParams, reserved0, reserved1;
    };
    static_assert(sizeof(OceanPushConstants) == sizeof(PushConstantData), "ocean push constants share the main range");
    const glm::vec3 center = node.GetWorldPosition(scene);
    const glm::vec2 half = glm::max(node.oceanSize, glm::vec2(1.0f)) * 0.5f;
    const float windAngle = glm::radians(node.waveDirection);
    OceanPushConstants push{};
    push.surface = glm::vec4(center.y, std::max(node.waveHeight, 0.0f), glm::clamp(node.waveChoppiness, 0.0f, 1.0f),
                             m_lastUBO[viewSlot].timeParams.x);
    push.wind = glm::vec4(std::cos(windAngle), std::sin(windAngle), std::max(node.waveLength, 0.5f), std::max(node.waveSpeed, 0.0f));
    push.bounds = glm::vec4(center.x - half.x, center.z - half.y, center.x + half.x, center.z + half.y);
    push.shallowColor = glm::vec4(node.waterShallowColor, std::max(node.waterClarity, 0.05f));
    push.deepColor = glm::vec4(node.waterDeepColor, glm::clamp(node.whitecaps, 0.0f, 1.0f));
    // Only the main views have a prepass (scene depth) to colour the water by depth
    push.foamParams = glm::vec4(std::max(node.shoreFoamWidth, 0.0f), viewSlot < 2 ? 1.0f : 0.0f,
                                glm::clamp(node.waterRoughness, 0.01f, 1.0f), 0.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_oceanPipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    VkBuffer vertexBuffers[] = { m_oceanGrid.vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_oceanGrid.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_oceanGrid.indexCount, 1, 0, 0, 0);
}

void VulkanPipeline::DrawLiquidStreams(VkCommandBuffer cmd, int viewSlot, const std::map<ShapeType, GPUMesh>& meshes) {
    auto cylinder = meshes.find(ShapeType::Cylinder);
    if (m_liquidStreams.empty() || cylinder == meshes.end() || cylinder->second.indexCount == 0) return;
    VkPipeline pipeline = (m_graphicsPipelineTransparent != VK_NULL_HANDLE) ? m_graphicsPipelineTransparent : m_graphicsPipeline;
    BindForwardPipeline(cmd, pipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    VkBuffer vertexBuffers[] = { cylinder->second.vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, cylinder->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (const LiquidStreamSegment& segment : m_liquidStreams) {
        const glm::vec3 axis = segment.end - segment.start;
        const float length = glm::length(axis);
        if (length < 1e-5f) continue;
        // The unit cylinder runs along Y: turn Y onto the segment, then size it
        const glm::vec3 y = axis / length;
        const glm::vec3 helper = std::abs(y.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 x = glm::normalize(glm::cross(helper, y));
        const glm::vec3 z = glm::cross(x, y);
        PushConstantData pc{};
        // Slightly longer than the segment so neighbouring segments overlap at the bends
        pc.model = glm::mat4(glm::vec4(x * segment.radius * 2.0f, 0.0f), glm::vec4(y * (length + segment.radius), 0.0f),
                             glm::vec4(z * segment.radius * 2.0f, 0.0f), glm::vec4((segment.start + segment.end) * 0.5f, 1.0f));
        pc.color = segment.color;
        pc.roughness = 0.05f;
        pc.metallic = 0.0f;
        pc.aoStrength = 1.0f;
        pc.materialFlags = MAT_DOUBLE_SIDED | (pipeline == m_graphicsPipelineTransparent ? MAT_TRANSPARENT : 0);
        pc.ior = 1.33f;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
        vkCmdDrawIndexed(cmd, cylinder->second.indexCount, 1, 0, 0, 0);
    }
}

void VulkanPipeline::SetFluidBodies(std::vector<FluidBody> bodies) {
    m_fluidBodies = std::move(bodies);
}

VkPipeline VulkanPipeline::BuildFluidPipeline(const std::string& vertSpv, const std::string& fragSpv, VkRenderPass renderPass,
                                              uint32_t instanceStreams, uint32_t colorCount, const VkPipelineColorBlendAttachmentState* blends,
                                              bool depthTest, bool depthWrite, VkCompareOp depthCompare) {
    VkDevice device = VulkanContext::Get().GetDevice();
    std::string shaderDir;
    const size_t slash = m_sceneTemplate.vertShaderPath.find_last_of("/\\");
    if (slash != std::string::npos) shaderDir = m_sceneTemplate.vertShaderPath.substr(0, slash + 1);
    VkShaderModule vert = CreateShaderModule(ReadFile(shaderDir + vertSpv));
    VkShaderModule frag = CreateShaderModule(ReadFile(shaderDir + fragSpv));
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = i == 0 ? vert : frag;
        stages[i].pName = "main";
    }
    // One instance per particle: vec4 (position, radius), and vec4 (velocity, foam)
    const VkVertexInputBindingDescription bindings[2] = { { 0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_INSTANCE },
                                                          { 1, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_INSTANCE } };
    const VkVertexInputAttributeDescription attributes[2] = { { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
                                                              { 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 } };
    const VkVertexInputBindingDescription meshBinding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    const VkVertexInputAttributeDescription meshAttribute{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, pos)) };
    // Particle quads (ParticleQuad): five vec4s per instance; mesh particles (5): the mesh's
    // positions and normals, then the quads as instances
    const uint32_t quadStream = instanceStreams == 5 ? 1 : 0;
    const VkVertexInputBindingDescription quadBindings[2] = { { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
                                                              { quadStream, sizeof(ParticleQuad), VK_VERTEX_INPUT_RATE_INSTANCE } };
    VkVertexInputAttributeDescription quadAttributes[7];
    const uint32_t firstQuadLocation = instanceStreams == 5 ? 2 : 0;
    quadAttributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, pos)) };
    quadAttributes[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal)) };
    for (uint32_t i = 0; i < 5; ++i) {
        quadAttributes[firstQuadLocation + i] = { firstQuadLocation + i, quadStream, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(i * sizeof(glm::vec4)) };
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (instanceStreams == 4 || instanceStreams == 5) {
        vertexInput.vertexBindingDescriptionCount = instanceStreams == 5 ? 2 : 1;
        vertexInput.pVertexBindingDescriptions = instanceStreams == 5 ? quadBindings : &quadBindings[1];
        vertexInput.vertexAttributeDescriptionCount = instanceStreams == 5 ? 7 : 5;
        vertexInput.pVertexAttributeDescriptions = instanceStreams == 5 ? quadAttributes : &quadAttributes[0];
    } else {
        vertexInput.vertexBindingDescriptionCount = instanceStreams == 3 ? 1 : std::min(instanceStreams, 2u);
        vertexInput.pVertexBindingDescriptions = instanceStreams == 3 ? &meshBinding : bindings;
        vertexInput.vertexAttributeDescriptionCount = instanceStreams == 3 ? 1 : std::min(instanceStreams, 2u);
        vertexInput.pVertexAttributeDescriptions = instanceStreams == 3 ? &meshAttribute : attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = depthCompare;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = colorCount;
    blending.pAttachments = blends;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamicState;
    info.layout = m_pipelineLayout;
    info.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) pipeline = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline;
}

void VulkanPipeline::CreateFluidPipelines(VkRenderPass particlePass, VkRenderPass thicknessPass, VkRenderPass smoothPass) {
    const VkColorComponentFlags red = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendAttachmentState write{};
    write.colorWriteMask = red;
    VkPipelineColorBlendAttachmentState add{};
    add.colorWriteMask = red;
    add.blendEnable = VK_TRUE;
    add.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    add.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    add.colorBlendOp = VK_BLEND_OP_ADD;
    add.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    add.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    add.alphaBlendOp = VK_BLEND_OP_ADD;
    // Nearest sphere surface and its body's colour (depth-tested; both attachments written alike,
    // as independent blending isn't enabled), then the summed thickness (additive, no depth test)
    VkPipelineColorBlendAttachmentState writeAll{};
    writeAll.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const VkPipelineColorBlendAttachmentState particleWrites[2] = { writeAll, writeAll };
    m_fluidDepthPipeline = BuildFluidPipeline("fluid_sprite_vert.spv", "fluid_sprite_frag.spv", particlePass, 1, 2, particleWrites, true, true);
    m_fluidThicknessPipeline = BuildFluidPipeline("fluid_sprite_vert.spv", "fluid_sprite_frag.spv", thicknessPass, 1, 1, &add, false, false);
    m_fluidSmoothPipeline = BuildFluidPipeline("fluid_fullscreen_vert.spv", "fluid_smooth_frag.spv", smoothPass, 0, 1, &write, false, false);
    m_fluidSmoothWidePipeline = BuildFluidPipeline("fluid_fullscreen_vert.spv", "fluid_smooth_wide_frag.spv", smoothPass, 0, 1, &write, false, false);
    // Shading: in the scene's transparent pass, depth-tested against the scene and writing the
    // fluid's own depth so later see-through surfaces sort against it
    VkPipelineColorBlendAttachmentState opaque{};
    opaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_fluidCompositePipeline = BuildFluidPipeline("fluid_fullscreen_vert.spv", "fluid_composite_frag.spv", m_sceneTemplate.renderPass, 0, 1, &opaque, true, true);

    // Rain and snow: premultiplied alpha like the other see-through surfaces, depth-tested
    VkPipelineColorBlendAttachmentState premultiplied = opaque;
    premultiplied.blendEnable = VK_TRUE;
    premultiplied.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    premultiplied.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premultiplied.colorBlendOp = VK_BLEND_OP_ADD;
    premultiplied.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    premultiplied.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premultiplied.alphaBlendOp = VK_BLEND_OP_ADD;
    m_precipPipeline = BuildFluidPipeline("precip_vert.spv", "precip_frag.spv", m_sceneTemplate.renderPass, 0, 1, &premultiplied, true, false);
    // Volumetric clouds: blended over the opaque scene, writing their own depth where solid
    m_cloudVolumePipeline = BuildFluidPipeline("fluid_fullscreen_vert.spv", "cloud_volume_frag.spv", m_sceneTemplate.renderPass, 0, 1,
                                               &premultiplied, true, true, VK_COMPARE_OP_ALWAYS);
    // Foam and spray over the fluid's surface (they test the scene and fluid depth themselves)
    m_fluidFoamPipeline = BuildFluidPipeline("fluid_foam_vert.spv", "fluid_foam_frag.spv", m_sceneTemplate.renderPass, 2, 1, &premultiplied, false, false);

    // Decals: drawn over the opaque scene with no depth test (the box can surround the camera);
    // the shader finds the surface from the prepass depth
    VkPipelineColorBlendAttachmentState multiply = premultiplied;
    multiply.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    multiply.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    multiply.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    multiply.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkPipelineColorBlendAttachmentState additive = premultiplied;
    additive.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    additive.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    additive.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    additive.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    const VkPipelineColorBlendAttachmentState* decalBlends[3] = { &premultiplied, &multiply, &additive };
    for (int mode = 0; mode < 3; ++mode) {
        m_decalPipelines[mode] = BuildFluidPipeline("decal_vert.spv", "decal_frag.spv", m_sceneTemplate.renderPass, 3, 1, decalBlends[mode], false, false);
    }

    // Particles (particle.vert/.frag): added light, premultiplied alpha, and heat haze (a bent copy
    // of the finished scene laid over it: premultiplied too). Depth-tested, not written.
    VkPipelineColorBlendAttachmentState glow = additive;
    const VkPipelineColorBlendAttachmentState* particleBlends[3] = { &glow, &premultiplied, &premultiplied };
    for (int mode = 0; mode < 3; ++mode) {
        m_particlePipelines[mode] = BuildFluidPipeline("particle_vert.spv", "particle_frag.spv", m_sceneTemplate.renderPass, 4, 1, particleBlends[mode], true, false);
    }
    // Mesh particles: instanced spheres and cubes, depth-written, fading out by alpha
    m_particleMeshPipeline = BuildFluidPipeline("particle_mesh_vert.spv", "particle_mesh_frag.spv", m_sceneTemplate.renderPass, 5, 1, &premultiplied, true, true);
}

void VulkanPipeline::DrawDecals(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const std::map<ShapeType, GPUMesh>& meshes) {
    GpuProfileScope gpuScope(cmd, "Decals");
    // Main views only: they have the prepass depth and normals the projection needs
    if (viewSlot >= 2 || !m_decalPipelines[0]) return;
    auto cube = meshes.find(ShapeType::Cube);
    if (cube == meshes.end() || cube->second.indexCount == 0) return;
    std::vector<const DecalNode*> decals;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::Decal) continue;
        if (const DecalNode* decal = dynamic_cast<const DecalNode*>(node.get())) {
            if (decal->opacity > 0.001f) decals.push_back(decal);
        }
    }
    if (decals.empty()) return;
    // Lower sort orders first, so higher ones lie on top
    std::stable_sort(decals.begin(), decals.end(), [](const DecalNode* a, const DecalNode* b) { return a->sortOrder < b->sortOrder; });
    const glm::mat4 viewProj = m_lastUBO[viewSlot].proj * m_lastUBO[viewSlot].view;
    VkBuffer vertexBuffers[] = { cube->second.vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    int bound = -1;
    for (const DecalNode* decal : decals) {
        const glm::mat4 box = decal->GetWorldTransformMatrix(scene) * glm::scale(glm::mat4(1.0f), glm::max(decal->decalSize, glm::vec3(0.01f)));
        // Skip boxes wholly off screen (all eight corners outside one clip plane)
        bool off = false;
        for (int axis = 0; axis < 3 && !off; ++axis) {
            for (int side = -1; side <= 1 && !off; side += 2) {
                int outside = 0;
                for (int c = 0; c < 8; ++c) {
                    const glm::vec4 clip = viewProj * box * glm::vec4((c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f, 1.0f);
                    if (side * clip[axis] > clip.w) ++outside;
                }
                off = outside == 8;
            }
        }
        if (off) continue;
        const int mode = std::clamp(static_cast<int>(decal->blendMode), 0, 2);
        if (!m_decalPipelines[mode]) continue;
        if (mode != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_decalPipelines[mode]);
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, cube->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            bound = mode;
        }
        BindMaterialSet(cmd, m_pipelineLayout, GetOrCreateDescriptorSet(viewSlot, decal->texturePath, decal->normalMapPath));
        PushConstantData pc{};
        pc.model = box;
        pc.color = glm::vec4(glm::vec3(decal->color), decal->color.a * decal->opacity);
        pc.emissiveColorAndIntensity = glm::vec4(decal->emissiveColor, mode == 2 ? std::max(decal->emissiveIntensity, 1.0f) : decal->emissiveIntensity);
        pc.roughness = std::cos(glm::radians(std::clamp(decal->normalAngleThreshold, 1.0f, 89.9f)));
        pc.metallic = std::max(decal->fadeDistance, 0.5f);
        pc.aoStrength = static_cast<float>(mode);
        pc.textureFlags = (!decal->texturePath.empty() ? TEX_ALBEDO : 0) | (!decal->normalMapPath.empty() ? TEX_NORMAL : 0);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
        vkCmdDrawIndexed(cmd, cube->second.indexCount, 1, 0, 0, 0);
    }
}

void VulkanPipeline::DrawFluidFoam(VkCommandBuffer cmd, int viewSlot) {
    if (m_fluidBodies.empty() || !m_fluidFoamPipeline || viewSlot >= 2) return;
    struct FluidPushConstants { glm::vec4 color, params, reserved[6]; };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fluidFoamPipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    for (const FluidBody& body : m_fluidBodies) {
        FluidPushConstants push{};
        push.color = body.color;
        push.params = glm::vec4(0.0f, body.colorDepth, body.radius, 0.0f);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        const VkBuffer buffers[2] = { body.particles, body.velocities };
        const VkDeviceSize offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        vkCmdDraw(cmd, 6, body.count, 0, 0);
    }
}

void VulkanPipeline::GatherFoliage(const Scene& scene, const std::map<std::string, GPUMesh>& customMeshes, const glm::vec3& cameraPos,
                                   const glm::mat4& viewProj, bool shadowCastersOnly, std::vector<FoliageDraw>& out) {
    out.clear();
    // The view's six frustum planes (rows of the view-projection), for testing cells as spheres
    const glm::mat4 m = glm::transpose(viewProj);
    const glm::vec4 planes[6] = { m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[2], m[3] - m[2] };
    auto visible = [&](const glm::vec3& c, float r) {
        for (const glm::vec4& p : planes) {
            if (glm::dot(glm::vec3(p), c) + p.w < -r * glm::length(glm::vec3(p))) return false;
        }
        return true;
    };
    std::vector<std::vector<glm::mat4>> perType;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::Terrain) continue;
        const TerrainNode* terrain = dynamic_cast<const TerrainNode*>(node.get());
        if (!terrain || terrain->foliage.empty()) continue;
        terrain->CollectFoliage(scene, cameraPos, visible, perType, shadowCastersOnly);
        for (size_t t = 0; t < perType.size(); ++t) {
            if (perType[t].empty()) continue;
            const FoliageType& type = terrain->foliageTypes[t];
            const std::string meshName = type.builtIn >= 0 ? FoliageMeshes::MeshName(type.builtIn) : type.modelMesh;
            auto mesh = customMeshes.find(meshName);
            if (mesh == customMeshes.end() || mesh->second.indexCount == 0) continue;
            out.push_back({ &mesh->second, &type, type.builtIn >= 0, std::move(perType[t]) });
        }
    }
}

void VulkanPipeline::DrawVolumetricClouds(VkCommandBuffer cmd, int viewSlot) {
    if (!m_cloudVolumePipeline || viewSlot >= 2 || m_lastUBO[viewSlot].volumetricCloudParams.x < 0.5f) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cloudVolumePipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanPipeline::DrawPrecipitation(VkCommandBuffer cmd, int viewSlot, const SceneEnvironment& env) {
    if (!m_precipPipeline || viewSlot >= 2) return;
    struct PrecipPushConstants { glm::vec4 params, reserved[7]; };
    static_assert(sizeof(PrecipPushConstants) == sizeof(PushConstantData), "precipitation push constants share the main range");
    bool bound = false;
    // Rain: up to 24,000 streaks in a 30 m box; snow: up to 16,000 flakes in a 24 m box;
    // splashes: up to 3,000 slots bursting over 24 m around the camera
    const struct { float amount; float kind; float box; float maxCount; } kinds[3] = {
        { env.rainAmount, 1.0f, 30.0f, 24000.0f }, { env.snowAmount, 0.0f, 24.0f, 16000.0f }, { env.rainAmount, 2.0f, 24.0f, 3000.0f }
    };
    for (const auto& kind : kinds) {
        const float amount = glm::clamp(kind.amount, 0.0f, 1.0f);
        const uint32_t count = static_cast<uint32_t>(amount * kind.maxCount);
        if (count < 16) continue;
        if (!bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_precipPipeline);
            BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
            bound = true;
        }
        PrecipPushConstants push{};
        push.params = glm::vec4(kind.kind, kind.box, amount, 0.0f);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, count, 0, 0);
    }
}

void VulkanPipeline::DrawFluidParticles(VkCommandBuffer cmd, int viewSlot, bool thickness) {
    if (m_fluidBodies.empty() || !m_fluidDepthPipeline || !m_fluidThicknessPipeline) return;
    struct FluidPushConstants { glm::vec4 color, params, reserved[6]; };
    static_assert(sizeof(FluidPushConstants) == sizeof(PushConstantData), "fluid push constants share the main range");
    {
        VkPipeline pipeline = thickness ? m_fluidThicknessPipeline : m_fluidDepthPipeline;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
        for (const FluidBody& body : m_fluidBodies) {
            FluidPushConstants push{};
            push.color = body.color;
            push.params = glm::vec4(pipeline == m_fluidThicknessPipeline ? 1.0f : 0.0f, body.colorDepth, body.radius, 0.0f);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &body.particles, &offset);
            vkCmdDraw(cmd, 6, body.count, 0, 0);
        }
    }
}

void VulkanPipeline::DrawFluidSmooth(VkCommandBuffer cmd, int viewSlot, int pass) {
    if (m_fluidBodies.empty() || !m_fluidSmoothPipeline) return;
    struct FluidPushConstants { glm::vec4 color, params, reserved[6]; };
    FluidPushConstants push{};
    push.color = m_fluidBodies[0].color;
    push.params = glm::vec4(0.0f, m_fluidBodies[0].colorDepth, m_fluidBodies[0].radius, static_cast<float>(pass));
    VkPipeline pipeline = (pass == 1 && m_fluidSmoothWidePipeline) ? m_fluidSmoothWidePipeline : m_fluidSmoothPipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanPipeline::DrawFluidComposite(VkCommandBuffer cmd, int viewSlot) {
    if (m_fluidBodies.empty() || !m_fluidCompositePipeline || viewSlot >= 2) return;
    struct FluidPushConstants { glm::vec4 color, params, reserved[6]; };
    FluidPushConstants push{};
    push.color = m_fluidBodies[0].color;
    push.params = glm::vec4(0.0f, m_fluidBodies[0].colorDepth, m_fluidBodies[0].radius, 0.0f);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fluidCompositePipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanPipeline::RenderSky(VkCommandBuffer cmd, int viewSlot) {
    const bool skyVisible = m_lastUBO[viewSlot].skyParams.w > 0.0f || m_lastUBO[viewSlot].envParams.x > 0.5f;
    if (m_skyPipeline == VK_NULL_HANDLE || !skyVisible) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VulkanPipeline::SetLightmapAtlas(uint32_t width, uint32_t height, const std::vector<uint32_t>& texels) {
    m_lightmapAtlas.Upload(width, height, texels);
    for (int slot = 0; slot < kViewCount; ++slot) {
        SetViewTexture(slot, kLightmapBinding, m_lightmapAtlas.GetView(), m_lightmapAtlas.GetSampler());
    }
}

void VulkanPipeline::SetLightmapMesh(uint32_t nodeId, const std::string& sourceKey, const GPUMesh& mesh) {
    VkDevice device = VulkanContext::Get().GetDevice();
    vkDeviceWaitIdle(device);
    auto it = m_lightmapMeshes.find(nodeId);
    if (it != m_lightmapMeshes.end()) it->second.mesh.Cleanup(device);
    m_lightmapMeshes[nodeId] = LightmapMesh{ sourceKey, mesh };
}

void VulkanPipeline::ClearLightmaps() {
    VkDevice device = VulkanContext::Get().GetDevice();
    vkDeviceWaitIdle(device);
    for (auto& entry : m_lightmapMeshes) entry.second.mesh.Cleanup(device);
    m_lightmapMeshes.clear();
    SetLightmapAtlas(1, 1, std::vector<uint32_t>(1, 0u));
}

std::string VulkanPipeline::LightmapSourceKey(const Node& node) {
    if (node.type == ShapeType::Custom) return node.customMeshName;
    return "shape:" + std::to_string(static_cast<int>(node.type));
}

const GPUMesh* VulkanPipeline::FindLightmapMesh(const Node& node) const {
    if (m_lightmapMeshes.empty()) return nullptr;
    auto it = m_lightmapMeshes.find(node.id);
    if (it == m_lightmapMeshes.end() || it->second.sourceKey != LightmapSourceKey(node)) return nullptr;
    return &it->second.mesh;
}

VkPipeline VulkanPipeline::BuildScenePipeline(const std::vector<char>& fragmentSpirv) {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkShaderModule vert = CreateShaderModule(ReadFile(m_sceneTemplate.vertShaderPath));
    VkShaderModule frag = CreateShaderModule(fragmentSpirv);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = (i == 0) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = (i == 0) ? vert : frag;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &m_sceneTemplate.binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_sceneTemplate.attributes.size());
    vertexInput.pVertexAttributeDescriptions = m_sceneTemplate.attributes.data();
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &m_sceneTemplate.blendAttachment;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &m_sceneTemplate.inputAssembly;
    info.pViewportState = &m_sceneTemplate.viewportState;
    info.pRasterizationState = &m_sceneTemplate.rasterizer;
    info.pMultisampleState = &m_sceneTemplate.multisampling;
    info.pDepthStencilState = &m_sceneTemplate.depthStencil;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamicState;
    info.layout = m_pipelineLayout;
    info.renderPass = m_sceneTemplate.renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) pipeline = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline;
}

VkPipeline VulkanPipeline::GetShaderGraphPipeline(const std::string& graphPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const long long stamp = fs::last_write_time(graphPath, ec).time_since_epoch().count();
    ShaderGraphPipeline& entry = m_shaderGraphPipelines[graphPath];
    if (entry.fileStamp == stamp && (entry.pipeline || entry.failed)) return entry.pipeline;

    // (Re)build: the graph changed or hasn't been built this run
    if (entry.pipeline) {
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        vkDestroyPipeline(VulkanContext::Get().GetDevice(), entry.pipeline, nullptr);
        entry.pipeline = VK_NULL_HANDLE;
    }
    entry.fileStamp = stamp;
    entry.failed = true;

    ShaderGraph graph;
    if (!graph.LoadFromFile(graphPath)) {
        std::cerr << "[ShaderGraph] Could not load " << graphPath << "; using the standard material" << std::endl;
        return VK_NULL_HANDLE;
    }
    const std::string function = graph.CompileToMaterialFunction();

    // Cache key: the generated code plus the engine's shader.frag (so engine updates rebuild)
    std::string shaderSource;
#ifdef TESSERA_SHADER_SOURCE_DIR
    const std::string sourceDir = TESSERA_SHADER_SOURCE_DIR;
    {
        std::ifstream in(sourceDir + "/shader.frag", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        shaderSource = text.str();
    }
#else
    const std::string sourceDir;
#endif
    char hashName[32];
    std::snprintf(hashName, sizeof(hashName), "%016llx", static_cast<unsigned long long>(std::hash<std::string>{}(function + "|" + shaderSource)));
    const fs::path cacheDir = fs::path("cache") / "shadergraphs" / hashName;
    const fs::path spvPath = cacheDir / "frag.spv";

    if (!fs::exists(spvPath, ec)) {
        std::string glslc;
#ifdef TESSERA_GLSLC_PATH
        glslc = TESSERA_GLSLC_PATH;
#endif
        if (glslc.empty() || !fs::exists(glslc, ec)) {
            const char* sdk = std::getenv("VULKAN_SDK");
            glslc = sdk ? (fs::path(sdk) / "Bin" / "glslc.exe").string() : std::string();
        }
        if (glslc.empty() || !fs::exists(glslc, ec) || sourceDir.empty() || !fs::exists(sourceDir + "/shader.frag", ec)) {
            std::cerr << "[ShaderGraph] " << graphPath << " is not compiled yet and glslc / engine shader sources aren't "
                      << "available here; using the standard material" << std::endl;
            return VK_NULL_HANDLE;
        }
        fs::create_directories(cacheDir, ec);
        {
            std::ofstream include(cacheDir / "shader_graph_material.glsl", std::ios::binary);
            include << function;
        }
        const fs::path logPath = cacheDir / "glslc.log";
        // cmd.exe needs the whole command quoted when it starts with a quoted path
        const std::string command = "\"\"" + glslc + "\" -DSHADER_GRAPH -I \"" + cacheDir.string() + "\" -I \"" + sourceDir +
                                    "\" \"" + sourceDir + "/shader.frag\" -o \"" + spvPath.string() + "\" > \"" + logPath.string() + "\" 2>&1\"";
        const int result = std::system(command.c_str());
        if (result != 0 || !fs::exists(spvPath, ec)) {
            std::cerr << "[ShaderGraph] Compiling " << graphPath << " failed (see " << logPath.string() << "); using the standard material" << std::endl;
            return VK_NULL_HANDLE;
        }
        std::cout << "[ShaderGraph] Compiled " << graphPath << " -> " << spvPath.string() << std::endl;
    }

    std::ifstream spvFile(spvPath, std::ios::binary | std::ios::ate);
    std::vector<char> spirv(static_cast<size_t>(spvFile.tellg()));
    spvFile.seekg(0);
    spvFile.read(spirv.data(), static_cast<std::streamsize>(spirv.size()));
    entry.pipeline = BuildScenePipeline(spirv);
    entry.failed = entry.pipeline == VK_NULL_HANDLE;
    if (entry.failed) std::cerr << "[ShaderGraph] Pipeline creation failed for " << graphPath << std::endl;
    return entry.pipeline;
}

void VulkanPipeline::CreateOverlayPipeline(VkRenderPass overlayRenderPass) {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkShaderModule vert = CreateShaderModule(ReadFile("shaders/overlay_vert.spv"));
    VkShaderModule frag = CreateShaderModule(ReadFile("shaders/overlay_frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = (i == 0) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = (i == 0) ? vert : frag;
        stages[i].pName = "main";
    }

    VkVertexInputBindingDescription binding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription position{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &position;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Hidden behind scene geometry, but never writes depth (the scene's depth is read only here)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blend;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlending;
    info.pDynamicState = &dynamicState;
    info.layout = m_pipelineLayout;
    info.renderPass = overlayRenderPass;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_overlayPipeline) != VK_SUCCESS) {
        m_overlayPipeline = VK_NULL_HANDLE; // no grid
    }
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void VulkanPipeline::CreateOcclusionResources() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (OcclusionView& view : m_occlusion) {
        for (VkQueryPool& pool : view.pools) {
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_OCCLUSION;
            info.queryCount = kMaxOcclusionQueries;
            if (vkCreateQueryPool(device, &info, nullptr, &pool) != VK_SUCCESS) pool = VK_NULL_HANDLE;
        }
    }
}

void VulkanPipeline::DestroyOcclusionResources() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (OcclusionView& view : m_occlusion) {
        for (VkQueryPool& pool : view.pools) {
            if (pool) vkDestroyQueryPool(device, pool, nullptr);
            pool = VK_NULL_HANDLE;
        }
    }
    if (m_occlusionPipeline) {
        vkDestroyPipeline(device, m_occlusionPipeline, nullptr);
        m_occlusionPipeline = VK_NULL_HANDLE;
    }
    if (m_overlayPipeline) {
        vkDestroyPipeline(device, m_overlayPipeline, nullptr);
        m_overlayPipeline = VK_NULL_HANDLE;
    }
    for (auto& [path, graphPipeline] : m_shaderGraphPipelines) {
        if (graphPipeline.pipeline) vkDestroyPipeline(device, graphPipeline.pipeline, nullptr);
    }
    m_shaderGraphPipelines.clear();
}

void VulkanPipeline::BeginOcclusionFrame(VkCommandBuffer cmd) {
    VkDevice device = VulkanContext::Get().GetDevice();
    std::vector<uint32_t> results;
    for (OcclusionView& view : m_occlusion) {
        VkQueryPool pool = view.pools[m_frameIndex];
        if (!pool) continue;
        std::vector<const Node*>& issued = view.issued[m_frameIndex];

        // This frame slot's fence has signalled, so its queries (from two frames ago) are finished
        if (view.enabled && !issued.empty()) {
            results.assign(issued.size(), 1);
            VkResult r = vkGetQueryPoolResults(device, pool, 0, static_cast<uint32_t>(issued.size()),
                                               results.size() * sizeof(uint32_t), results.data(), sizeof(uint32_t), 0);
            view.hidden.clear();
            if (r == VK_SUCCESS) {
                for (size_t i = 0; i < issued.size(); ++i) {
                    if (results[i] == 0) view.hidden.insert(issued[i]);
                }
            }
            view.resultCameraPos = view.issuedCameraPos[m_frameIndex];
            view.resultCameraForward = view.issuedCameraForward[m_frameIndex];
            view.stats.tested = static_cast<uint32_t>(issued.size());
            view.stats.hidden = static_cast<uint32_t>(view.hidden.size());
        } else if (!view.enabled) {
            view.hidden.clear();
            view.stats = OcclusionStats{};
        }
        issued.clear();
        vkCmdResetQueryPool(cmd, pool, 0, kMaxOcclusionQueries);
    }
}

bool VulkanPipeline::OcclusionResultsUsable(int viewSlot) const {
    const OcclusionView& view = m_occlusion[viewSlot % 2];
    if (!view.enabled || view.hidden.empty()) return false;
    // Results are two frames old: after a fast move or turn they no longer match the view
    const glm::mat4 invView = glm::inverse(m_lastUBO[viewSlot].view);
    const glm::vec3 position = glm::vec3(invView[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));
    return glm::distance(position, view.resultCameraPos) < 0.5f &&
           glm::dot(forward, view.resultCameraForward) > 0.996f; // within about 5 degrees
}

void VulkanPipeline::IssueOcclusionQueries(VkCommandBuffer cmd, int viewSlot, const Scene& scene,
                                           const std::vector<std::shared_ptr<Node>>& nodes,
                                           const std::map<ShapeType, GPUMesh>& meshes, const glm::mat4& viewProj) {
    OcclusionView& view = m_occlusion[viewSlot % 2];
    VkQueryPool pool = view.pools[m_frameIndex];
    auto cubeIt = meshes.find(ShapeType::Cube);
    if (!pool || m_occlusionPipeline == VK_NULL_HANDLE || cubeIt == meshes.end() || cubeIt->second.indexCount == 0) return;
    const GPUMesh& cube = cubeIt->second;

    const glm::mat4 invView = glm::inverse(m_lastUBO[viewSlot].view);
    const glm::vec3 cameraPos = glm::vec3(invView[3]);
    std::vector<const Node*>& issued = view.issued[m_frameIndex];
    view.issuedCameraPos[m_frameIndex] = cameraPos;
    view.issuedCameraForward[m_frameIndex] = -glm::normalize(glm::vec3(invView[2]));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_occlusionPipeline);
    BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
    VkBuffer vertexBuffers[] = { cube.vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, cube.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& node : nodes) {
        if (issued.size() >= kMaxOcclusionQueries) break;
        if (!node || node->IsUINode()) continue;
        switch (node->nodeType) {
            case NodeType::Mesh: case NodeType::Character: break;
            default: continue; // helpers, lights, particles, terrain (bounds don't cover it), ...
        }
        if (node->isMirror) continue;
        if (IsOutsideFrustum(*node, scene, viewProj)) continue;

        glm::vec3 localMin, localMax;
        GetLocalBounds(*node, localMin, localMax);
        const glm::vec3 center = (localMin + localMax) * 0.5f;
        const glm::mat4 world = node->GetWorldTransformMatrix(scene);
        // Padding: 5%, plus about two pixels at this distance so objects peeking out by a pixel
        // (TAA jitter shifts the view by up to half a pixel each frame) are never hidden
        const glm::vec3 axisScale = glm::max(glm::vec3(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                                                       glm::length(glm::vec3(world[2]))), glm::vec3(1e-4f));
        const float pixelPad = 0.004f * glm::distance(cameraPos, glm::vec3(world * glm::vec4(center, 1.0f)));
        const glm::vec3 size = glm::max(localMax - localMin, glm::vec3(0.001f)) * 1.05f + 2.0f * pixelPad / axisScale;
        const glm::mat4 boxModel = world * glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size);

        // A camera inside (or right against) the box would see its faces clipped away: never cull those
        const glm::vec3 local = glm::vec3(glm::inverse(boxModel) * glm::vec4(cameraPos, 1.0f));
        const float worldScale = std::max(glm::length(glm::vec3(boxModel[0])),
                                          std::max(glm::length(glm::vec3(boxModel[1])), glm::length(glm::vec3(boxModel[2]))));
        const float margin = 0.5f + 0.5f / std::max(worldScale, 0.001f); // half-size plus ~0.5 m
        if (std::abs(local.x) < margin && std::abs(local.y) < margin && std::abs(local.z) < margin) continue;

        PushConstantData pc{};
        pc.model = boxModel;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
        const uint32_t query = static_cast<uint32_t>(issued.size());
        vkCmdBeginQuery(cmd, pool, query, 0);
        vkCmdDrawIndexed(cmd, cube.indexCount, 1, 0, 0, 0);
        vkCmdEndQuery(cmd, pool, query);
        issued.push_back(node.get());
    }
}

int VulkanPipeline::MaterialExtFlags(const Node& node) {
    const bool detailAlbedo = !node.detailAlbedoPath.empty();
    const bool detailNormal = !node.detailNormalPath.empty();
    const TerrainNode* terrain = node.nodeType == NodeType::Terrain ? dynamic_cast<const TerrainNode*>(&node) : nullptr;
    if (terrain && terrain->layers.empty()) terrain = nullptr;
    if (node.materialModel == 0 && node.clearCoat <= 0.0f && !node.triplanarMapping && !detailAlbedo && !detailNormal && !node.isLiquid && !terrain) return 0;
    if (!m_materialExtStorage.mapped) return 0;

    MaterialExtData data{};
    const int features = (node.triplanarMapping ? 1 : 0) | (detailAlbedo ? 2 : 0) | (detailNormal ? 4 : 0) | (node.isLiquid ? 8 : 0) |
                         (terrain ? 16 : 0);
    if (terrain) {
        int textured = 0;
        for (size_t l = 0; l < terrain->layers.size() && l < 4; ++l) {
            const TerrainLayer& layer = terrain->layers[l];
            if (!layer.texturePath.empty()) textured |= 1 << l;
            data.terrainTiling[static_cast<int>(l)] = std::max(layer.tiling, 0.01f);
            data.terrainTint[l] = glm::vec4(layer.tint, layer.roughness);
        }
        data.terrainParams = glm::vec4(std::max(terrain->uvTiling, 1e-3f), terrain->terrainSize, static_cast<float>(textured));
    }
    data.params0 = glm::vec4(static_cast<float>(node.materialModel), node.clearCoat, node.clearCoatRoughness, static_cast<float>(features));
    if (node.materialModel == 2) {
        data.subsurface = glm::vec4(node.hairHighlightTint, node.hairHighlightShift);
    } else {
        data.subsurface = glm::vec4(node.subsurfaceColor, node.subsurfaceStrength);
    }
    data.params1 = glm::vec4(node.subsurfaceTranslucency, node.triplanarTileSize, node.triplanarSharpness, node.detailTiling);
    data.params2 = glm::vec4(node.detailStrength, node.irisRadius, node.irisDepth, node.corneaIOR);
    if (node.isLiquid) {
        data.liquidPlane = node.liquidPlane;
        data.liquidTop = node.liquidTopColor;
        const bool round = node.type == ShapeType::Cylinder || node.type == ShapeType::Sphere || node.type == ShapeType::Cone;
        data.liquidBoundsMin = glm::vec4(node.liquidWorldMin, round ? 1.0f : 0.0f);
        data.liquidBoundsMax = glm::vec4(node.liquidWorldMax, std::max(node.liquidColorDepth, 1e-4f));
        data.liquidRipple = node.liquidRipple;
    }
    if (node.materialModel == 3) {
        // The cornea is a wet, smooth layer over the whole eye
        data.params0.y = std::max(data.params0.y, 1.0f);
        data.params0.z = std::min(data.params0.z, 0.03f);
    }

    const std::string key(reinterpret_cast<const char*>(&data), sizeof(data));
    auto it = m_materialExtIndices.find(key);
    uint32_t local;
    if (it != m_materialExtIndices.end()) {
        local = it->second;
    } else {
        local = static_cast<uint32_t>(m_materialExtIndices.size());
        if (local >= kMaxMaterialExts) return 0; // budget used up: draw as a standard material
        std::memcpy(m_materialExtStorage.mapped + m_materialExtStorage.regionSize * m_frameIndex + sizeof(MaterialExtData) * local,
                    &data, sizeof(data));
        m_materialExtIndices.emplace(key, local);
    }
    // Absolute entry (this frame's region) + 1, in bits 8 and up
    const uint32_t entry = static_cast<uint32_t>(m_materialExtStorage.regionSize / sizeof(MaterialExtData)) * m_frameIndex + local;
    return static_cast<int>((entry + 1) << 8);
}

void VulkanPipeline::CreateFrameStorage() {
    auto& ctx = VulkanContext::Get();
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.GetPhysicalDevice(), &props);
    const VkDeviceSize alignment = std::max<VkDeviceSize>(props.limits.minStorageBufferOffsetAlignment, 16);
    auto create = [&](StorageRegion& storage, VkDeviceSize bytes, uint32_t regions) {
        storage.regionSize = (bytes + alignment - 1) / alignment * alignment;
        VulkanBufferUtils::CreateBuffer(storage.regionSize * regions, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        storage.buffer, storage.memory);
        void* mapped = nullptr;
        vkMapMemory(ctx.GetDevice(), storage.memory, 0, storage.regionSize * regions, 0, &mapped);
        storage.mapped = static_cast<char*>(mapped);
        std::memset(storage.mapped, 0, static_cast<size_t>(storage.regionSize * regions));
    };
    create(m_instanceStorage, sizeof(InstanceData) * kMaxInstancesPerFrame, kFramesInFlight);
    create(m_lightStorage, sizeof(LightData) * kMaxSceneLights, kFramesInFlight * kViewCount);
    // Header (counts, slice params, directional list), cluster ranges, then the index list
    const VkDeviceSize clusterBytes = 16 + 16 + 4 * kMaxDirectionalLights + 8 * kClusterCount + 4 * kMaxLightIndices;
    create(m_clusterStorage, clusterBytes, kFramesInFlight * kViewCount);
    create(m_materialExtStorage, sizeof(MaterialExtData) * kMaxMaterialExts, kFramesInFlight);
}

void VulkanPipeline::DestroyFrameStorage() {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (StorageRegion* storage : { &m_instanceStorage, &m_lightStorage, &m_clusterStorage, &m_materialExtStorage }) {
        if (storage->buffer == VK_NULL_HANDLE) continue;
        vkUnmapMemory(device, storage->memory);
        vkDestroyBuffer(device, storage->buffer, nullptr);
        vkFreeMemory(device, storage->memory, nullptr);
        *storage = StorageRegion{};
    }
}

uint32_t VulkanPipeline::WriteInstances(const InstanceData* data, uint32_t count) {
    if (count == 0 || m_instanceCursor + count > kMaxInstancesPerFrame) return UINT32_MAX;
    std::memcpy(m_instanceStorage.mapped + m_instanceStorage.regionSize * m_frameIndex + sizeof(InstanceData) * m_instanceCursor,
                data, sizeof(InstanceData) * count);
    const uint32_t first = m_instanceCursor;
    m_instanceCursor += count;
    return first;
}

// Uploads the view's lights and sorts point / spot lights into clusters: 16 x 9 screen tiles x
// 24 log-spaced depth slices. Each light's bounding sphere is projected to a tile rectangle and a
// slice range, then two passes (count, then fill) build compact per-cluster index lists.
void VulkanPipeline::BuildLightClusters(int viewSlot, const UniformBufferObject& ubo) {
    if (!m_lightStorage.mapped || !m_clusterStorage.mapped) return;
    const std::vector<LightData>& lights = m_viewLights[LightSetFor(viewSlot)];
    const uint32_t lightCount = static_cast<uint32_t>(std::min(lights.size(), static_cast<size_t>(kMaxSceneLights)));
    const VkDeviceSize region = static_cast<VkDeviceSize>(m_frameIndex) * kViewCount + viewSlot;
    if (lightCount > 0) {
        std::memcpy(m_lightStorage.mapped + m_lightStorage.regionSize * region, lights.data(), sizeof(LightData) * lightCount);
    }

    char* base = m_clusterStorage.mapped + m_clusterStorage.regionSize * region;
    glm::uvec4* counts = reinterpret_cast<glm::uvec4*>(base);
    glm::vec4* params = reinterpret_cast<glm::vec4*>(base + 16);
    uint32_t* directional = reinterpret_cast<uint32_t*>(base + 32);
    glm::uvec2* clusters = reinterpret_cast<glm::uvec2*>(base + 32 + 4 * kMaxDirectionalLights);
    uint32_t* indices = reinterpret_cast<uint32_t*>(base + 32 + 4 * kMaxDirectionalLights + 8 * kClusterCount);

    const float logRange = std::log(kClusterFar / kClusterNear);
    const float sliceScale = kClusterSlices / logRange;
    const float sliceBias = -kClusterSlices * std::log(kClusterNear) / logRange;
    auto sliceOf = [&](float depth) {
        const float s = std::log(std::max(depth, kClusterNear)) * sliceScale + sliceBias;
        return static_cast<int>(std::clamp(s, 0.0f, static_cast<float>(kClusterSlices - 1)));
    };

    struct Range { uint32_t light; int x0, x1, y0, y1, z0, z1; };
    std::vector<Range> ranges;
    ranges.reserve(lightCount);
    uint32_t directionalCount = 0;
    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightData& light = lights[i];
        if (light.position.w < 0.5f) {
            if (directionalCount < kMaxDirectionalLights) directional[directionalCount++] = i;
            continue;
        }
        const glm::vec3 center = glm::vec3(ubo.view * glm::vec4(glm::vec3(light.position), 1.0f));
        const float radius = std::max(light.spotDirAndRange.w, 0.1f);
        const float nearDepth = -center.z - radius;
        const float farDepth = -center.z + radius;
        if (farDepth < kClusterNear) continue; // entirely behind the camera

        Range r{ i, 0, static_cast<int>(kClusterTilesX) - 1, 0, static_cast<int>(kClusterTilesY) - 1, sliceOf(nearDepth), sliceOf(farDepth) };
        if (nearDepth > kClusterNear) {
            // Screen rectangle of the sphere's bounding box (all corners in front of the camera)
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (int c = 0; c < 8; ++c) {
                const glm::vec3 corner = center + glm::vec3((c & 1) ? radius : -radius, (c & 2) ? radius : -radius, (c & 4) ? radius : -radius);
                const glm::vec4 clip = ubo.proj * glm::vec4(corner, 1.0f);
                const glm::vec2 ndc = glm::vec2(clip) / std::max(clip.w, 1e-4f);
                lo = glm::min(lo, ndc);
                hi = glm::max(hi, ndc);
            }
            if (hi.x < -1.0f || hi.y < -1.0f || lo.x > 1.0f || lo.y > 1.0f) continue; // off screen
            auto tile = [](float ndc, uint32_t tiles) {
                return std::clamp(static_cast<int>((ndc * 0.5f + 0.5f) * tiles), 0, static_cast<int>(tiles) - 1);
            };
            r.x0 = tile(lo.x, kClusterTilesX); r.x1 = tile(hi.x, kClusterTilesX);
            r.y0 = tile(lo.y, kClusterTilesY); r.y1 = tile(hi.y, kClusterTilesY);
        }
        ranges.push_back(r);
    }

    auto clusterIndex = [](int x, int y, int z) { return (z * static_cast<int>(kClusterTilesY) + y) * static_cast<int>(kClusterTilesX) + x; };
    m_clusterScratch.assign(kClusterCount, 0);
    for (const Range& r : ranges)
        for (int z = r.z0; z <= r.z1; ++z)
            for (int y = r.y0; y <= r.y1; ++y)
                for (int x = r.x0; x <= r.x1; ++x) ++m_clusterScratch[clusterIndex(x, y, z)];
    uint32_t total = 0;
    for (uint32_t c = 0; c < kClusterCount; ++c) {
        const uint32_t n = std::min(m_clusterScratch[c], kMaxLightIndices - total);
        clusters[c] = glm::uvec2(total, 0);
        m_clusterScratch[c] = n; // capacity actually granted
        total += n;
    }
    for (const Range& r : ranges)
        for (int z = r.z0; z <= r.z1; ++z)
            for (int y = r.y0; y <= r.y1; ++y)
                for (int x = r.x0; x <= r.x1; ++x) {
                    glm::uvec2& cluster = clusters[clusterIndex(x, y, z)];
                    if (cluster.y < m_clusterScratch[clusterIndex(x, y, z)]) indices[cluster.x + cluster.y++] = r.light;
                }

    *counts = glm::uvec4(lightCount, directionalCount, total, 0);
    *params = glm::vec4(sliceScale, sliceBias, static_cast<float>(kClusterSlices), 0.0f);
}

void VulkanPipeline::UpdateUniformBuffer(const UniformBufferObject& ubo) {
    m_lastUBO[0] = ubo;
    if (m_uniformBuffersMapped[0]) {
        memcpy(static_cast<char*>(m_uniformBuffersMapped[0]) + CurrentUboOffset(), &ubo, sizeof(UniformBufferObject));
    }
    BuildLightClusters(0, ubo);
}

void VulkanPipeline::UpdateUniformBufferPreview(const UniformBufferObject& ubo) {
    m_lastUBO[1] = ubo;
    if (m_uniformBuffersMapped[1]) {
        memcpy(static_cast<char*>(m_uniformBuffersMapped[1]) + CurrentUboOffset(), &ubo, sizeof(UniformBufferObject));
    }
    BuildLightClusters(1, ubo);
}

void VulkanPipeline::UpdateUniformBufferSlot(int viewSlot, const UniformBufferObject& ubo) {
    m_lastUBO[viewSlot] = ubo;
    if (m_uniformBuffersMapped[viewSlot]) {
        memcpy(static_cast<char*>(m_uniformBuffersMapped[viewSlot]) + CurrentUboOffset(), &ubo, sizeof(UniformBufferObject));
    }
    BuildLightClusters(viewSlot, ubo);
}

void VulkanPipeline::CreateDescriptorPool() {
    auto& ctx = VulkanContext::Get();

    VkDescriptorPoolSize poolSizes[4]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = 256;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 256 * (kMaterialBindingCount - 1 - 4);

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    poolSizes[2].descriptorCount = 256 * 3;

    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[3].descriptorCount = 256;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 256;

    if (vkCreateDescriptorPool(ctx.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void VulkanPipeline::CreateDescriptorSets() {
    auto& ctx = VulkanContext::Get();

    std::vector<VkDescriptorSetLayout> layouts(kViewCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kViewCount;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(ctx.GetDevice(), &allocInfo, m_descriptorSets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }

    for (int i = 0; i < kViewCount; ++i) {
        const std::shared_ptr<VulkanTexture> defaults[kMaterialTextureCount] = {
            m_defaultTexture, m_defaultNormalTexture, m_defaultTexture,
            m_defaultTexture, m_defaultTexture, m_defaultTexture,
            m_defaultTexture, m_defaultNormalTexture,
            m_defaultTexture, m_defaultTexture, m_defaultTexture, m_defaultTexture, m_defaultTexture, m_defaultTexture
        };
        WriteMaterialDescriptorSet(m_descriptorSets[i], i, defaults);
    }
}

void VulkanPipeline::WriteMaterialDescriptorSet(VkDescriptorSet set, int uboIndex,
                                                const std::shared_ptr<VulkanTexture> (&textures)[kMaterialTextureCount]) {
    auto& ctx = VulkanContext::Get();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffers[uboIndex];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo imageInfos[kMaterialTextureCount]{};
    VkWriteDescriptorSet descriptorWrites[kMaterialBindingCount]{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = set;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    for (uint32_t t = 0; t < kMaterialTextureCount; ++t) {
        imageInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[t].imageView = textures[t]->GetImageView();
        imageInfos[t].sampler = textures[t]->GetSampler();

        const uint32_t binding = kMaterialTextureBindings[t];
        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = set;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pImageInfo = &imageInfos[t];
    }

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VulkanShadowMap::kSampledLayout;
    shadowInfo.imageView = m_shadowMaps[uboIndex % 2].GetImageView();
    shadowInfo.sampler = m_shadowMaps[uboIndex % 2].GetSampler();

    descriptorWrites[kShadowMapBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[kShadowMapBinding].dstSet = set;
    descriptorWrites[kShadowMapBinding].dstBinding = kShadowMapBinding;
    descriptorWrites[kShadowMapBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[kShadowMapBinding].descriptorCount = 1;
    descriptorWrites[kShadowMapBinding].pImageInfo = &shadowInfo;

    VkDescriptorImageInfo localShadowInfo{};
    localShadowInfo.imageLayout = VulkanShadowMap::kSampledLayout;
    localShadowInfo.imageView = m_localShadowMaps[uboIndex % 2].GetImageView();
    localShadowInfo.sampler = m_localShadowMaps[uboIndex % 2].GetSampler();

    descriptorWrites[kLocalShadowBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[kLocalShadowBinding].dstSet = set;
    descriptorWrites[kLocalShadowBinding].dstBinding = kLocalShadowBinding;
    descriptorWrites[kLocalShadowBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[kLocalShadowBinding].descriptorCount = 1;
    descriptorWrites[kLocalShadowBinding].pImageInfo = &localShadowInfo;

    VkDescriptorImageInfo shelterInfo{};
    shelterInfo.imageLayout = VulkanShadowMap::kSampledLayout;
    shelterInfo.imageView = m_shelterMaps[uboIndex % 2].GetImageView();
    shelterInfo.sampler = m_shelterMaps[uboIndex % 2].GetPlainSampler();

    descriptorWrites[kShelterBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[kShelterBinding].dstSet = set;
    descriptorWrites[kShelterBinding].dstBinding = kShelterBinding;
    descriptorWrites[kShelterBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[kShelterBinding].descriptorCount = 1;
    descriptorWrites[kShelterBinding].pImageInfo = &shelterInfo;

    // Sky: the view's atmosphere LUT (kept in GENERAL for its compute pass) and the cloud noise
    VkDescriptorImageInfo skyInfos[3]{};
    skyInfos[0] = { m_sky.GetLutSampler(), m_sky.GetLutView(uboIndex % 2), VK_IMAGE_LAYOUT_GENERAL };
    skyInfos[1] = { m_sky.GetRepeatSampler(), m_sky.GetDetailView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    skyInfos[2] = { m_sky.GetRepeatSampler(), m_sky.GetCoverageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const uint32_t skyBindings[3] = { kSkyViewLutBinding, kCloudDetailBinding, kCloudCoverageBinding };
    for (int k = 0; k < 3; ++k) {
        descriptorWrites[skyBindings[k]].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[skyBindings[k]].dstSet = set;
        descriptorWrites[skyBindings[k]].dstBinding = skyBindings[k];
        descriptorWrites[skyBindings[k]].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[skyBindings[k]].descriptorCount = 1;
        descriptorWrites[skyBindings[k]].pImageInfo = &skyInfos[k];
    }

    VkDescriptorImageInfo probeInfo{};
    probeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    probeInfo.imageView = m_reflectionProbes[uboIndex % 2].GetArrayView();
    probeInfo.sampler = m_reflectionProbes[uboIndex % 2].GetSampler();

    descriptorWrites[kProbeBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[kProbeBinding].dstSet = set;
    descriptorWrites[kProbeBinding].dstBinding = kProbeBinding;
    descriptorWrites[kProbeBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[kProbeBinding].descriptorCount = 1;
    descriptorWrites[kProbeBinding].pImageInfo = &probeInfo;

    VkDescriptorImageInfo viewTextureInfos[kViewTextureCount]{};
    for (uint32_t t = 0; t < kViewTextureCount; ++t) {
        const uint32_t binding = kViewTextureBindings[t];
        viewTextureInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        viewTextureInfos[t].imageView = m_viewTextureViews[t][uboIndex];
        viewTextureInfos[t].sampler = m_viewTextureSamplers[t][uboIndex];

        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = set;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pImageInfo = &viewTextureInfos[t];
    }

    // Instance and light storage: whole regions; BindMaterialSet picks the frame's / view's region
    const StorageRegion* storages[3] = { &m_instanceStorage, &m_clusterStorage, &m_lightStorage };
    VkDescriptorBufferInfo storageInfos[3]{};
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t binding = kInstanceBinding + k;
        storageInfos[k] = { storages[k]->buffer, 0, storages[k]->regionSize };
        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = set;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pBufferInfo = &storageInfos[k];
    }
    m_setSlots[set] = uboIndex;

    // Advanced materials: the whole buffer (every frame's region; entries carry absolute indices)
    VkDescriptorBufferInfo materialExtInfo{ m_materialExtStorage.buffer, 0, m_materialExtStorage.regionSize * kFramesInFlight };
    descriptorWrites[kMaterialExtBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[kMaterialExtBinding].dstSet = set;
    descriptorWrites[kMaterialExtBinding].dstBinding = kMaterialExtBinding;
    descriptorWrites[kMaterialExtBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[kMaterialExtBinding].descriptorCount = 1;
    descriptorWrites[kMaterialExtBinding].pBufferInfo = &materialExtInfo;

    vkUpdateDescriptorSets(ctx.GetDevice(), kMaterialBindingCount, descriptorWrites, 0, nullptr);
}

void VulkanPipeline::SetViewTexture(int viewSlot, uint32_t binding, VkImageView view, VkSampler sampler) {
    const int idx = viewSlot;
    const int t = ViewTextureIndex(binding);
    if (t < 0) return;
    m_viewTextureViews[t][idx] = view;
    m_viewTextureSamplers[t][idx] = sampler;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    auto rewrite = [&](VkDescriptorSet set) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), 1, &write, 0, nullptr);
    };

    rewrite(m_descriptorSets[idx]);
    // Cached material sets are keyed "<uboIndex>|..."
    const std::string prefix = std::to_string(idx) + "|";
    for (const auto& [key, set] : m_textureDescriptorSets) {
        if (key.rfind(prefix, 0) == 0) rewrite(set);
    }
}

std::shared_ptr<VulkanTexture> VulkanPipeline::GetOrLoadTexture(const std::string& path, bool normalMap) {
    if (path.empty()) return m_defaultTexture;
    if (path[0] == '@') {
        auto runtime = m_runtimeTextures.find(path);
        return (runtime != m_runtimeTextures.end() && runtime->second) ? runtime->second : m_defaultTexture;
    }

    const std::string key = normalMap ? path + "|normal" : path;
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        return it->second;
    }

    auto tex = VulkanTexture::LoadCompressedFromFile(path, normalMap ? VulkanTexture::TextureUsage::NormalMap
                                                                      : VulkanTexture::TextureUsage::Color);
    m_textures[key] = tex;
    return tex;
}

VkDescriptorSet VulkanPipeline::GetOrCreateDescriptorSet(
    int uboIndex,
    const std::string& albedoPath,
    const std::string& normalPath,
    const std::string& metallicPath,
    const std::string& roughnessPath,
    const std::string& aoPath,
    const std::string& emissivePath,
    const std::string& detailAlbedoPath,
    const std::string& detailNormalPath,
    const std::string* terrainTextures,
    const std::string& heightPath)
{
    if (albedoPath.empty() && normalPath.empty() && metallicPath.empty() && roughnessPath.empty() &&
        aoPath.empty() && emissivePath.empty() && detailAlbedoPath.empty() && detailNormalPath.empty() && !terrainTextures && heightPath.empty()) {
        return m_descriptorSets[uboIndex];
    }

    std::string key = std::to_string(uboIndex) + "|" + albedoPath + "|" + normalPath + "|" + metallicPath + "|" +
                      roughnessPath + "|" + aoPath + "|" + emissivePath + "|" + detailAlbedoPath + "|" + detailNormalPath + "|" + heightPath;
    if (terrainTextures) {
        for (int t = 0; t < 5; ++t) key += "|" + terrainTextures[t];
    }
    auto it = m_textureDescriptorSets.find(key);
    if (it != m_textureDescriptorSets.end()) {
        return it->second;
    }

    const std::shared_ptr<VulkanTexture> textures[kMaterialTextureCount] = {
        // Block-compressed (see VulkanTexture::CompressionQuality); normal maps as BC5
        !albedoPath.empty() ? GetOrLoadTexture(albedoPath) : m_defaultTexture,
        !normalPath.empty() ? GetOrLoadTexture(normalPath, true) : m_defaultNormalTexture,
        !metallicPath.empty() ? GetOrLoadTexture(metallicPath) : m_defaultTexture,
        !roughnessPath.empty() ? GetOrLoadTexture(roughnessPath) : m_defaultTexture,
        !aoPath.empty() ? GetOrLoadTexture(aoPath) : m_defaultTexture,
        !emissivePath.empty() ? GetOrLoadTexture(emissivePath) : m_defaultTexture,
        !detailAlbedoPath.empty() ? GetOrLoadTexture(detailAlbedoPath) : m_defaultTexture,
        !detailNormalPath.empty() ? GetOrLoadTexture(detailNormalPath, true) : m_defaultNormalTexture,
        terrainTextures ? GetOrLoadTexture(terrainTextures[0]) : m_defaultTexture,
        terrainTextures ? GetOrLoadTexture(terrainTextures[1]) : m_defaultTexture,
        terrainTextures ? GetOrLoadTexture(terrainTextures[2]) : m_defaultTexture,
        terrainTextures ? GetOrLoadTexture(terrainTextures[3]) : m_defaultTexture,
        terrainTextures ? GetOrLoadTexture(terrainTextures[4]) : m_defaultTexture,
        !heightPath.empty() ? GetOrLoadTexture(heightPath) : m_defaultTexture
    };

    auto& ctx = VulkanContext::Get();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    VkDescriptorSet newSet{VK_NULL_HANDLE};
    if (vkAllocateDescriptorSets(ctx.GetDevice(), &allocInfo, &newSet) != VK_SUCCESS) {
        return m_descriptorSets[uboIndex];
    }

    WriteMaterialDescriptorSet(newSet, uboIndex, textures);

    m_textureDescriptorSets[key] = newSet;
    return newSet;
}

void VulkanPipeline::SetRuntimeTexture(const std::string& key, const unsigned char* rgba, uint32_t width, uint32_t height) {
    auto it = m_runtimeTextures.find(key);
    if (it != m_runtimeTextures.end() && it->second && it->second->GetWidth() == width && it->second->GetHeight() == height) {
        it->second->UpdatePixels(rgba);
        return;
    }
    // New (or resized): material sets made with the old one are dropped and remade on next use
    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
    for (auto set = m_textureDescriptorSets.begin(); set != m_textureDescriptorSets.end();) {
        if (set->first.find(key) != std::string::npos) set = m_textureDescriptorSets.erase(set); else ++set;
    }
    if (it != m_runtimeTextures.end() && it->second) it->second->Cleanup();
    m_runtimeTextures[key] = VulkanTexture::CreateFromPixels(rgba, width, height);
}

void VulkanPipeline::CreateGraphicsPipeline(VkRenderPass renderPass, const std::string& vertShaderPath, const std::string& fragShaderPath) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    auto vertShaderCode = ReadFile(vertShaderPath);
    auto fragShaderCode = ReadFile(fragShaderPath);

    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_UINT;
    attributeDescriptions[3].offset = offsetof(Vertex, boneIndices);

    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[4].offset = offsetof(Vertex, boneWeights);

    attributeDescriptions[5].binding = 0;
    attributeDescriptions[5].location = 6;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[5].offset = offsetof(Vertex, color);

    attributeDescriptions[6].binding = 0;
    attributeDescriptions[6].location = 7;
    attributeDescriptions[6].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[6].offset = offsetof(Vertex, lightmapUV);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant range for model matrix, color, and useTexture flag
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    // Set 1 is the ray tracing set (VulkanRayTracing) when the GPU supports it, otherwise an
    // empty placeholder; every scene pipeline shares this layout either way
    VkDescriptorSetLayout set1Layout = m_rtSetLayout;
    if (set1Layout == VK_NULL_HANDLE) {
        if (m_emptySetLayout == VK_NULL_HANDLE) {
            VkDescriptorSetLayoutCreateInfo emptyInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            if (vkCreateDescriptorSetLayout(device, &emptyInfo, nullptr, &m_emptySetLayout) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create empty descriptor set layout!");
            }
        }
        set1Layout = m_emptySetLayout;
    }
    const VkDescriptorSetLayout sceneSetLayouts[2] = { m_descriptorSetLayout, set1Layout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = sceneSetLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    // 1. Create Solid Pipeline
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create solid graphics pipeline!");
    }
    // Kept for shader graph pipelines (same state, own fragment shader)
    m_sceneTemplate.binding = bindingDescription;
    m_sceneTemplate.attributes.assign(attributeDescriptions.begin(), attributeDescriptions.end());
    m_sceneTemplate.inputAssembly = inputAssembly;
    m_sceneTemplate.viewportState = viewportState;
    m_sceneTemplate.rasterizer = rasterizer;
    m_sceneTemplate.multisampling = multisampling;
    m_sceneTemplate.depthStencil = depthStencil;
    m_sceneTemplate.blendAttachment = colorBlendAttachment;
    m_sceneTemplate.renderPass = renderPass;
    m_sceneTemplate.vertShaderPath = vertShaderPath;

    // 1b. Transparent surfaces: depth test but no depth write, premultiplied alpha so
    // reflections stay at full strength however clear the surface is
    {
        VkPipelineDepthStencilStateCreateInfo transparentDepth = depthStencil;
        transparentDepth.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState premultiplied = colorBlendAttachment;
        premultiplied.blendEnable = VK_TRUE;
        premultiplied.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        premultiplied.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        premultiplied.colorBlendOp = VK_BLEND_OP_ADD;
        premultiplied.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        premultiplied.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        premultiplied.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo transparentBlending = colorBlending;
        transparentBlending.pAttachments = &premultiplied;

        VkGraphicsPipelineCreateInfo transparentInfo = pipelineInfo;
        transparentInfo.pDepthStencilState = &transparentDepth;
        transparentInfo.pColorBlendState = &transparentBlending;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &transparentInfo, nullptr, &m_graphicsPipelineTransparent) != VK_SUCCESS) {
            m_graphicsPipelineTransparent = VK_NULL_HANDLE; // falls back to the solid pipeline
        }

        // 1b1. Ray-traced variants: same state, the ray query fragment shader (frag_rt.spv)
        if (m_rtSetLayout != VK_NULL_HANDLE) {
            std::string rtFragPath = fragShaderPath;
            const size_t dot = rtFragPath.rfind(".spv");
            if (dot != std::string::npos) rtFragPath.insert(dot, "_rt");
            if (rtFragPath.find("frag_rt.spv") == std::string::npos) {
                const size_t slash = fragShaderPath.find_last_of("/\\");
                rtFragPath = (slash != std::string::npos ? fragShaderPath.substr(0, slash + 1) : std::string()) + "frag_rt.spv";
            }
            std::vector<char> rtCode;
            try { rtCode = ReadFile(rtFragPath); } catch (...) { rtCode.clear(); }
            if (!rtCode.empty()) {
                VkShaderModule rtFragModule = CreateShaderModule(rtCode);
                VkPipelineShaderStageCreateInfo rtStages[2] = { vertShaderStageInfo, fragShaderStageInfo };
                rtStages[1].module = rtFragModule;
                VkGraphicsPipelineCreateInfo rtInfo = pipelineInfo;
                rtInfo.pStages = rtStages;
                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &rtInfo, nullptr, &m_graphicsPipelineRT) != VK_SUCCESS) {
                    m_graphicsPipelineRT = VK_NULL_HANDLE;
                }
                VkGraphicsPipelineCreateInfo rtTransparentInfo = transparentInfo;
                rtTransparentInfo.pStages = rtStages;
                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &rtTransparentInfo, nullptr, &m_graphicsPipelineTransparentRT) != VK_SUCCESS) {
                    m_graphicsPipelineTransparentRT = VK_NULL_HANDLE;
                }
                vkDestroyShaderModule(device, rtFragModule, nullptr);
                std::cout << "[VulkanPipeline] Ray-traced forward pipelines "
                          << (m_graphicsPipelineRT != VK_NULL_HANDLE ? "created" : "FAILED") << " (" << rtFragPath << ")" << std::endl;
            } else {
                std::cerr << "[VulkanPipeline] " << rtFragPath << " not found: hardware ray tracing effects disabled" << std::endl;
            }
        }
    }

    // 1b2. Ocean: its own shaders, drawn in the transparent pass but opaque (it blends in what's
    // under it itself, from the scene copy) and depth-writing, both sides visible
    {
        std::string shaderDir;
        size_t slash = fragShaderPath.find_last_of("/\\");
        if (slash != std::string::npos) shaderDir = fragShaderPath.substr(0, slash + 1);
        VkShaderModule oceanVert = CreateShaderModule(ReadFile(shaderDir + "ocean_vert.spv"));
        VkShaderModule oceanFrag = CreateShaderModule(ReadFile(shaderDir + "ocean_frag.spv"));
        VkPipelineShaderStageCreateInfo oceanStages[] = { vertShaderStageInfo, fragShaderStageInfo };
        oceanStages[0].module = oceanVert;
        oceanStages[1].module = oceanFrag;
        VkPipelineRasterizationStateCreateInfo oceanRaster = rasterizer;
        oceanRaster.cullMode = VK_CULL_MODE_NONE;
        oceanRaster.polygonMode = VK_POLYGON_MODE_FILL;
        VkPipelineColorBlendAttachmentState oceanBlend = colorBlendAttachment;
        oceanBlend.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo oceanBlending = colorBlending;
        oceanBlending.pAttachments = &oceanBlend;
        VkPipelineDepthStencilStateCreateInfo oceanDepth = depthStencil;
        oceanDepth.depthWriteEnable = VK_TRUE;
        VkGraphicsPipelineCreateInfo oceanInfo = pipelineInfo;
        oceanInfo.pStages = oceanStages;
        oceanInfo.pRasterizationState = &oceanRaster;
        oceanInfo.pColorBlendState = &oceanBlending;
        oceanInfo.pDepthStencilState = &oceanDepth;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &oceanInfo, nullptr, &m_oceanPipeline) != VK_SUCCESS) {
            m_oceanPipeline = VK_NULL_HANDLE; // oceans are skipped
        }
        vkDestroyShaderModule(device, oceanVert, nullptr);
        vkDestroyShaderModule(device, oceanFrag, nullptr);
    }

    // 1c. Occlusion-query boxes: depth test only (less-or-equal, no depth or colour writes)
    {
        std::string occlusionFragPath = "occlusion_frag.spv";
        size_t slash = fragShaderPath.find_last_of("/\\");
        if (slash != std::string::npos) occlusionFragPath = fragShaderPath.substr(0, slash + 1) + occlusionFragPath;
        VkShaderModule occlusionFragModule = CreateShaderModule(ReadFile(occlusionFragPath));
        VkPipelineShaderStageCreateInfo occlusionStages[] = { vertShaderStageInfo, fragShaderStageInfo };
        occlusionStages[1].module = occlusionFragModule;

        VkPipelineDepthStencilStateCreateInfo occlusionDepth = depthStencil;
        occlusionDepth.depthWriteEnable = VK_FALSE;
        occlusionDepth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState noColor = colorBlendAttachment;
        noColor.blendEnable = VK_FALSE;
        noColor.colorWriteMask = 0;
        VkPipelineColorBlendStateCreateInfo occlusionBlending = colorBlending;
        occlusionBlending.pAttachments = &noColor;

        VkGraphicsPipelineCreateInfo occlusionInfo = pipelineInfo;
        occlusionInfo.pStages = occlusionStages;
        occlusionInfo.pDepthStencilState = &occlusionDepth;
        occlusionInfo.pColorBlendState = &occlusionBlending;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &occlusionInfo, nullptr, &m_occlusionPipeline) != VK_SUCCESS) {
            m_occlusionPipeline = VK_NULL_HANDLE; // occlusion culling stays off
        }
        vkDestroyShaderModule(device, occlusionFragModule, nullptr);
    }

    // 1a. Depth + normal prepass for SSAO: same vertex stage and layout, own fragment shader
    // and render pass, opaque output
    if (m_prepassRenderPass != VK_NULL_HANDLE) {
        std::string prepassFragPath = "prepass_frag.spv";
        size_t slash = fragShaderPath.find_last_of("/\\");
        if (slash != std::string::npos) {
            prepassFragPath = fragShaderPath.substr(0, slash + 1) + prepassFragPath;
        }
        VkShaderModule prepassFragModule = CreateShaderModule(ReadFile(prepassFragPath));
        std::string prepassVertPath = prepassFragPath;
        prepassVertPath.replace(prepassVertPath.rfind("_frag.spv"), 9, "_vert.spv");
        VkShaderModule prepassVertModule = CreateShaderModule(ReadFile(prepassVertPath));

        VkPipelineShaderStageCreateInfo prepassStages[] = { vertShaderStageInfo, fragShaderStageInfo };
        prepassStages[0].module = prepassVertModule;
        prepassStages[1].module = prepassFragModule;

        // Normals and velocity, both written without blending
        VkPipelineColorBlendAttachmentState opaqueAttachments[2] = { colorBlendAttachment, colorBlendAttachment };
        opaqueAttachments[0].blendEnable = VK_FALSE;
        opaqueAttachments[1].blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo opaqueBlending = colorBlending;
        opaqueBlending.attachmentCount = 2;
        opaqueBlending.pAttachments = opaqueAttachments;

        // Binding 1: the previous frame's vertices (skinned characters; otherwise the same buffer
        // again), whose positions give per-vertex motion vectors
        VkVertexInputBindingDescription prepassBindings[2] = {
            bindingDescription,
            { 1, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
        };
        std::array<VkVertexInputAttributeDescription, 7> prepassAttributes{};
        std::copy(attributeDescriptions.begin(), attributeDescriptions.end(), prepassAttributes.begin());
        prepassAttributes[6] = { 5, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, pos)) };
        VkPipelineVertexInputStateCreateInfo prepassVertexInput = vertexInputInfo;
        prepassVertexInput.vertexBindingDescriptionCount = 2;
        prepassVertexInput.pVertexBindingDescriptions = prepassBindings;
        prepassVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(prepassAttributes.size());
        prepassVertexInput.pVertexAttributeDescriptions = prepassAttributes.data();

        VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo;
        prepassInfo.pStages = prepassStages;
        prepassInfo.pVertexInputState = &prepassVertexInput;
        prepassInfo.pColorBlendState = &opaqueBlending;
        prepassInfo.renderPass = m_prepassRenderPass;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &prepassInfo, nullptr, &m_prepassPipeline) != VK_SUCCESS) {
            m_prepassPipeline = VK_NULL_HANDLE; // SSAO then stays off
        }
        vkDestroyShaderModule(device, prepassFragModule, nullptr);
        vkDestroyShaderModule(device, prepassVertModule, nullptr);
    }

    // 1b. Soft sprites for ocean spray and pour splashes (depthTest=ON, depthWrite=OFF, alpha blend);
    // particle emitters have their own pipelines (CreateFluidPipelines)
    depthStencil.depthWriteEnable = VK_FALSE;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipelineParticlesAlpha) != VK_SUCCESS) {
        m_graphicsPipelineParticlesAlpha = VK_NULL_HANDLE;
    }

    // Restore depth write and blend attachment for wireframe and line pipelines
    depthStencil.depthWriteEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    // 2. Create Wireframe Pipeline (VK_POLYGON_MODE_LINE)
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.5f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = -1.0f;
    rasterizer.depthBiasSlopeFactor = -1.0f;

    VkResult wireResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipelineWireframe);
    if (wireResult != VK_SUCCESS) {
        m_graphicsPipelineWireframe = VK_NULL_HANDLE;
    }

    // 3. Create Lines Pipeline for Grid & Axes
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.depthBiasEnable = VK_FALSE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipelineLines) != VK_SUCCESS) {
        m_graphicsPipelineLines = VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanPipeline::RenderScene(VkCommandBuffer cmd, const Scene& scene,
                                 const std::map<ShapeType, GPUMesh>& meshes,
                                 const std::map<std::string, GPUMesh>& customMeshes,
                                 PolygonMode mode, bool renderGizmoNodes, int viewSlot,
                                 const glm::vec3& cameraPos, ScenePhase phase) {
    if (scene.GetNodes().empty()) return;

    // Forward draws in this view use the ray-traced pipelines when it has a TLAS this frame
    struct ActiveRTViewScope {
        int& active;
        ActiveRTViewScope(int& a, int view) : active(a) { active = view; }
        ~ActiveRTViewScope() { active = -1; }
    } rtScope(m_activeRTView, viewSlot);

    const int uboIdx = viewSlot;
    const glm::mat4 viewProj = m_lastUBO[uboIdx].proj * m_lastUBO[uboIdx].view;
    // Mirror and probe-capture views: no mirror surfaces (they would need their own reflection)
    const bool isMirrorView = viewSlot >= kViewEditorMirror;
    const bool drawOpaque = (phase != ScenePhase::Transparent);
    const bool drawTransparent = (phase != ScenePhase::Opaque);

    // Procedural sky background, before any geometry
    const bool skyVisible = m_lastUBO[uboIdx].skyParams.w > 0.0f || m_lastUBO[uboIdx].envParams.x > 0.5f;
    if (drawOpaque && m_skyPipeline != VK_NULL_HANDLE && skyVisible) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
        BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[uboIdx]);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Occlusion results apply to the main views only (mirror and probe views see other things)
    const bool useOcclusion = viewSlot < 2 && OcclusionResultsUsable(viewSlot);
    const OcclusionView& occlusion = m_occlusion[viewSlot % 2];

    // Alpha-blended surfaces go in the sorted transparent pass; cut-out ones stay opaque
    auto isTransparent = [](const Node& node) {
        switch (node.nodeType) {
            case NodeType::Collider: case NodeType::Camera: case NodeType::Light: return false;
            default: return node.isLiquid || (node.color.a < 0.99f && node.alphaCutoff <= 0.0f);
        }
    };

    std::vector<std::shared_ptr<Node>> opaqueNodes;
    std::vector<std::pair<float, std::shared_ptr<Node>>> transparentByDistance;
    std::vector<std::shared_ptr<Node>> oceanNodes; // drawn first in the transparent phase
    for (const auto& node : scene.GetNodes()) {
        if (node->isOcean) {
            if (node->nodeType == NodeType::Mesh) oceanNodes.push_back(node);
            continue;
        }
        // Decals are projected onto the scene (DrawDecals), not drawn as objects
        if (node->nodeType == NodeType::Decal) continue;
        // A mirror can't show itself in its own reflection
        if (isMirrorView && node->isMirror) continue;
        // A probe object would surround its own capture camera
        if (viewSlot >= kViewEditorProbe && node->isReflectionProbe) continue;
        if (isTransparent(*node)) {
            // A liquid sorts as slightly nearer, so it draws after the glass around it: refractive
            // glass replaces what's behind it with the opaque scene, which has no liquid in it
            const float sortBias = node->isLiquid ? -0.05f : 0.0f;
            transparentByDistance.emplace_back(glm::distance(cameraPos, node->GetWorldPosition(scene)) + sortBias, node);
        } else {
            opaqueNodes.push_back(node);
        }
    }
    // Back to front, so nearer glass blends over farther glass
    std::sort(transparentByDistance.begin(), transparentByDistance.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::shared_ptr<Node>> transparentNodes;
    for (auto& entry : transparentByDistance) transparentNodes.push_back(entry.second);

    // Draws that differ only in their transform, grouped for instancing
    struct DrawBatch {
        const GPUMesh* mesh;
        VkDescriptorSet set;
        PushConstantData push; // model unused
        std::vector<InstanceData> instances;
    };

    // Helper lambda to draw nodes
    auto DrawNodes = [&](const std::vector<std::shared_ptr<Node>>& nodeList, VkPipeline pipeline,
                         const std::optional<glm::vec4>& overrideColor, bool includeColliders = true, bool onlyColliders = false) {
        BindForwardPipeline(cmd, pipeline);

        std::vector<DrawBatch> batches;
        std::unordered_map<std::string, size_t> batchIndex;
        auto AddToBatch = [&](const GPUMesh* mesh, VkDescriptorSet set, const PushConstantData& pc) {
            PushConstantData keyPush = pc;
            keyPush.model = glm::mat4(0.0f);
            std::string key(reinterpret_cast<const char*>(&mesh), sizeof(mesh));
            key.append(reinterpret_cast<const char*>(&set), sizeof(set));
            key.append(reinterpret_cast<const char*>(&keyPush), sizeof(keyPush));
            auto [it, inserted] = batchIndex.try_emplace(key, batches.size());
            if (inserted) batches.push_back(DrawBatch{ mesh, set, keyPush, {} });
            batches[it->second].instances.push_back(InstanceData{ pc.model, pc.model });
        };

        for (const auto& node : nodeList) {
            if (node->IsUINode() || node->nodeType == NodeType::Empty || node->nodeType == NodeType::AudioSource || node->nodeType == NodeType::Spline) {
                continue;
            }


            if (!renderGizmoNodes && (node->nodeType == NodeType::Camera || node->nodeType == NodeType::Light || node->nodeType == NodeType::Collider)) {
                continue;
            }


            if (!includeColliders && node->nodeType == NodeType::Collider) {
                continue;
            }


            if (onlyColliders && node->nodeType != NodeType::Collider) {
                continue;
            }

            float distToCam = glm::distance(cameraPos, node->GetWorldPosition(scene));
            if (node->IsCulledByDistance(distToCam)) {
                continue;
            }




            if (node->nodeType == NodeType::ParticleEmitter) {
                // Particle emitters are rendered in the dedicated transparent VFX pass with depth-write disabled
                continue;
            }


            if (node->nodeType == NodeType::Camera) {
                VkDescriptorSet descSet = GetOrCreateDescriptorSet(uboIdx, "", "", "", "");
                BindMaterialSet(cmd, m_pipelineLayout, descSet);
                glm::mat4 baseMat = node->GetWorldTransformMatrix(scene);
                auto drawPart = [&](ShapeType st, const glm::mat4& localMat, const glm::vec4& col, float emit) {
                    auto it = meshes.find(st);
                    if (it == meshes.end() || it->second.indexCount == 0) return;
                    VkBuffer vBuffers[] = { it->second.vertexBuffer };
                    VkDeviceSize vOffsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, vOffsets);
                    vkCmdBindIndexBuffer(cmd, it->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    PushConstantData cpc{};
                    cpc.model = baseMat * localMat;
                    cpc.color = overrideColor.has_value() ? *overrideColor : col;
                    cpc.roughness = 0.35f;
                    cpc.metallic = 0.65f;
                    cpc.emissiveColorAndIntensity = glm::vec4(glm::vec3(col), emit);
                    cpc.aoStrength = 1.0f;
                    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &cpc);
                    vkCmdDrawIndexed(cmd, it->second.indexCount, 1, 0, 0, 0);
                };
                // 1) Camera chassis
                drawPart(ShapeType::Cube, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.12f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.36f, 0.28f, 0.48f)), glm::vec4(0.22f, 0.26f, 0.32f, 1.0f), 0.25f);
                // 2) Dual top film reels
                glm::mat4 reelRot = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                drawPart(ShapeType::Cylinder, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.23f, -0.02f)) * reelRot * glm::scale(glm::mat4(1.0f), glm::vec3(0.22f, 0.10f, 0.22f)), glm::vec4(0.32f, 0.38f, 0.46f, 1.0f), 0.3f);
                drawPart(ShapeType::Cylinder, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.23f, 0.22f)) * reelRot * glm::scale(glm::mat4(1.0f), glm::vec3(0.22f, 0.10f, 0.22f)), glm::vec4(0.32f, 0.38f, 0.46f, 1.0f), 0.3f);
                // 3) Forward-pointing lens hood cone along -Z
                glm::mat4 lensRot = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                drawPart(ShapeType::Cone, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -0.24f)) * lensRot * glm::scale(glm::mat4(1.0f), glm::vec3(0.34f, 0.36f, 0.34f)), glm::vec4(0.20f, 0.80f, 1.0f, 1.0f), 1.4f);
                continue;
            }


            if (node->nodeType == NodeType::Light) {
                VkDescriptorSet descSet = GetOrCreateDescriptorSet(uboIdx, "", "", "", "");
                BindMaterialSet(cmd, m_pipelineLayout, descSet);
                glm::mat4 baseMat = node->GetWorldTransformMatrix(scene);
                glm::vec4 lCol = glm::vec4(node->lightColor, 1.0f);
                auto drawLightPart = [&](ShapeType st, const glm::mat4& localMat, const glm::vec4& col, float emit) {
                    auto it = meshes.find(st);
                    if (it == meshes.end() || it->second.indexCount == 0) return;
                    VkBuffer vBuffers[] = { it->second.vertexBuffer };
                    VkDeviceSize vOffsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, vOffsets);
                    vkCmdBindIndexBuffer(cmd, it->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    PushConstantData lpc{};
                    lpc.model = baseMat * localMat;
                    lpc.color = overrideColor.has_value() ? *overrideColor : col;
                    lpc.roughness = 0.25f;
                    lpc.metallic = 0.1f;
                    lpc.emissiveColorAndIntensity = glm::vec4(glm::vec3(col), emit);
                    lpc.aoStrength = 1.0f;
                    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &lpc);
                    vkCmdDrawIndexed(cmd, it->second.indexCount, 1, 0, 0, 0);
                };
                if (node->lightType == LightType::Spot) {
                    // Spot Light cone housing pointing down along -Y
                    drawLightPart(ShapeType::Cone, glm::scale(glm::mat4(1.0f), glm::vec3(0.46f, 0.52f, 0.46f)), lCol, 2.8f);
                } else if (node->lightType == LightType::AreaRect) {
                    drawLightPart(ShapeType::Cube, glm::scale(glm::mat4(1.0f), glm::vec3(std::max(node->areaLightSize.x, 0.2f), std::max(node->areaLightSize.y, 0.2f), 0.05f)), lCol, 3.2f);
                } else if (node->lightType == LightType::Directional) {
                    // Sun sphere + directional arrow cone pointing along -Z
                    drawLightPart(ShapeType::Sphere, glm::scale(glm::mat4(1.0f), glm::vec3(0.38f)), lCol, 3.2f);
                    glm::mat4 dirRot = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    drawLightPart(ShapeType::Cone, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -0.45f)) * dirRot * glm::scale(glm::mat4(1.0f), glm::vec3(0.24f, 0.42f, 0.24f)), lCol, 3.2f);
                } else {
                    // Point Light glowing bulb sphere + socket
                    drawLightPart(ShapeType::Sphere, glm::scale(glm::mat4(1.0f), glm::vec3(0.36f)), lCol, 3.2f);
                    drawLightPart(ShapeType::Cylinder, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.22f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.18f, 0.14f, 0.18f)), glm::vec4(0.45f, 0.48f, 0.55f, 1.0f), 0.4f);
                }
                continue;
            }


            const GPUMesh* meshPtr = ResolveNodeMesh(*node, meshes, customMeshes, distToCam);
            if (!meshPtr || meshPtr->indexCount == 0) continue;

            const GPUMesh& mesh = *meshPtr;

            // Bind descriptor set for this node (UBO + texture if assigned)
            if (node->nodeType != NodeType::Collider && IsOutsideFrustum(*node, scene, viewProj)) {
                continue;
            }
            // Hidden behind other geometry in the latest occlusion results (opaque pass only)
            if (useOcclusion && pipeline == m_graphicsPipeline && occlusion.hidden.count(node.get())) {
                continue;
            }

            // Terrains also bind their splat map and paint layer textures
            std::string terrainTextures[5];
            const TerrainNode* terrainNode = node->nodeType == NodeType::Terrain ? dynamic_cast<const TerrainNode*>(node.get()) : nullptr;
            if (terrainNode && !terrainNode->layers.empty()) {
                terrainTextures[0] = terrainNode->GetSplatTextureKey();
                for (size_t l = 0; l < terrainNode->layers.size() && l < 4; ++l) terrainTextures[l + 1] = terrainNode->layers[l].texturePath;
            } else {
                terrainNode = nullptr;
            }
            VkDescriptorSet descSet = GetOrCreateDescriptorSet(
                uboIdx,
                node->texturePath,
                node->normalMapPath,
                node->metallicMapPath,
                node->roughnessMapPath,
                node->aoMapPath,
                node->emissiveMapPath,
                node->detailAlbedoPath,
                node->detailNormalPath,
                terrainNode ? terrainTextures : nullptr,
                node->heightMapPath
            );

            PushConstantData pc{};
            pc.roughness = node->roughness;
            pc.metallic = node->metallic;
            pc.textureFlags = (!node->texturePath.empty() ? TEX_ALBEDO : 0) |
                              (!node->normalMapPath.empty() ? TEX_NORMAL : 0) |
                              (!node->metallicMapPath.empty() ? TEX_METALLIC : 0) |
                              (!node->roughnessMapPath.empty() ? TEX_ROUGHNESS : 0) |
                              (!node->aoMapPath.empty() ? TEX_AO : 0) |
                              (!node->emissiveMapPath.empty() ? TEX_EMISSIVE : 0) |
                              (!node->heightMapPath.empty() ? TEX_HEIGHT : 0);
            pc.alphaCutoff = node->alphaCutoff;
            pc.materialFlags = (node->doubleSided ? MAT_DOUBLE_SIDED : 0) |
                               (pipeline == m_graphicsPipelineTransparent ? MAT_TRANSPARENT : 0) |
                               (node->isMirror && !isMirrorView ? MAT_MIRROR : 0) |
                               (node->useVertexColors ? MAT_VERTEX_COLOR : 0) |
                               (meshPtr == FindLightmapMesh(*node) ? MAT_LIGHTMAP : 0) |
                               MaterialExtFlags(*node);
            pc.ior = (pipeline == m_graphicsPipelineTransparent) ? node->refractionIOR : 1.0f;
            pc.emissiveColorAndIntensity = glm::vec4(node->emissiveColor, node->emissiveIntensity);
            pc.aoStrength = node->aoStrength;
            pc.heightScale = node->heightScale;

            if (node->nodeType == NodeType::Collider) {
                glm::mat4 base = node->GetWorldTransformMatrix(scene) * glm::translate(glm::mat4(1.0f), node->colliderCenter);
                if (node->colliderType == ColliderType::Box) {
                    pc.model = base * glm::scale(glm::mat4(1.0f), node->colliderSize);
                } else if (node->colliderType == ColliderType::Sphere) {
                    pc.model = base * glm::scale(glm::mat4(1.0f), glm::vec3(node->colliderRadius * 2.0f));
                } else if (node->colliderType == ColliderType::Capsule) {
                    pc.model = base * glm::scale(glm::mat4(1.0f), glm::vec3(node->colliderRadius * 2.0f, node->colliderHeight, node->colliderRadius * 2.0f));
                } else {
                    pc.model = base;
                }
                pc.color = overrideColor.has_value() ? *overrideColor : glm::vec4(0.20f, 0.95f, 0.45f, 0.90f);
                pc.textureFlags = 0;
                pc.alphaCutoff = 0.0f;
                pc.materialFlags = MAT_DOUBLE_SIDED;
            } else if (node->nodeType == NodeType::Decal) {
                auto decalNode = std::dynamic_pointer_cast<DecalNode>(node);
                glm::vec3 dSize = decalNode ? glm::vec3(decalNode->decalSize.x, 0.025f, decalNode->decalSize.z) : glm::vec3(2.0f, 0.025f, 2.0f);
                float dOpacity = decalNode ? decalNode->opacity : 1.0f;
                pc.model = node->GetWorldTransformMatrix(scene) * glm::scale(glm::mat4(1.0f), dSize);
                pc.color = overrideColor.has_value() ? *overrideColor : glm::vec4(glm::vec3(node->color), node->color.a * dOpacity);
            } else {
                pc.model = node->GetWorldTransformMatrix(scene);
                pc.color = overrideColor.has_value() ? *overrideColor : node->color;
            }

            // Shader graph material: its own pipeline (same lighting), drawn on its own
            if (!node->shaderGraphPath.empty() && pipeline == m_graphicsPipeline) {
                if (VkPipeline graphPipeline = GetShaderGraphPipeline(node->shaderGraphPath)) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphPipeline);
                    BindMaterialSet(cmd, m_pipelineLayout, descSet);
                    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
                    VkBuffer graphVertexBuffers[] = { mesh.vertexBuffer };
                    VkDeviceSize graphOffsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, graphVertexBuffers, graphOffsets);
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
                    BindForwardPipeline(cmd, pipeline);
                    continue;
                }
            }

            // Opaque meshes with identical mesh, material and settings are drawn together below.
            // Sorted transparents, mirrors, colliders and decals keep their own draws.
            // Multi-material submesh draws (glTF primitives / FBX material partitions)
            if (mesh.submeshes.size() > 1 && !overrideColor.has_value() && node->nodeType != NodeType::Collider && node->nodeType != NodeType::Decal) {
                VkBuffer subVertexBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize subOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, subVertexBuffers, subOffsets);
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                for (const SubmeshSection& sub : mesh.submeshes) {
                    if (sub.indexCount == 0) continue;
                    const std::string& subAlbedo = !sub.diffuseTexturePath.empty() ? sub.diffuseTexturePath : node->texturePath;
                    const std::string& subNormal = !sub.normalMapPath.empty() ? sub.normalMapPath : node->normalMapPath;
                    const std::string& subMetal  = !sub.metallicMapPath.empty() ? sub.metallicMapPath : node->metallicMapPath;
                    const std::string& subRough  = !sub.roughnessMapPath.empty() ? sub.roughnessMapPath : node->roughnessMapPath;
                    const std::string& subAO     = !sub.aoMapPath.empty() ? sub.aoMapPath : node->aoMapPath;
                    const std::string& subEmit   = !sub.emissiveMapPath.empty() ? sub.emissiveMapPath : node->emissiveMapPath;
                    VkDescriptorSet subSet = GetOrCreateDescriptorSet(uboIdx, subAlbedo, subNormal, subMetal, subRough, subAO, subEmit,
                                                                      node->detailAlbedoPath, node->detailNormalPath, nullptr, node->heightMapPath);
                    PushConstantData subPc = pc;
                    subPc.color = sub.diffuseColor;
                    subPc.roughness = sub.roughness;
                    subPc.metallic = sub.metallic;
                    if (sub.emissiveIntensity > 0.0f) {
                        subPc.emissiveColorAndIntensity = glm::vec4(sub.emissiveColor, sub.emissiveIntensity);
                    }
                    subPc.textureFlags = (!subAlbedo.empty() ? TEX_ALBEDO : 0) |
                                         (!subNormal.empty() ? TEX_NORMAL : 0) |
                                         (!subMetal.empty() ? TEX_METALLIC : 0) |
                                         (!subRough.empty() ? TEX_ROUGHNESS : 0) |
                                         (!subAO.empty() ? TEX_AO : 0) |
                                         (!subEmit.empty() ? TEX_EMISSIVE : 0) |
                                         (!node->heightMapPath.empty() ? TEX_HEIGHT : 0);
                    BindMaterialSet(cmd, m_pipelineLayout, subSet);
                    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &subPc);
                    vkCmdDrawIndexed(cmd, sub.indexCount, 1, sub.firstIndex, 0, 0);
                }
                continue;
            }
            const bool batchable = pipeline != m_graphicsPipelineTransparent && !node->isMirror &&
                                   node->nodeType != NodeType::Collider && node->nodeType != NodeType::Decal;
            if (batchable) {
                AddToBatch(meshPtr, descSet, pc);
                continue;
            }

            BindMaterialSet(cmd, m_pipelineLayout, descSet);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);

            VkBuffer vertexBuffers[] = { mesh.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }

        // Terrain foliage, in the opaque pass: instanced like everything else
        if (pipeline == m_graphicsPipeline && !overrideColor.has_value()) {
            std::vector<FoliageDraw> foliage;
            GatherFoliage(scene, customMeshes, cameraPos, viewProj, false, foliage);
            for (FoliageDraw& draw : foliage) {
                PushConstantData pc{};
                pc.color = draw.type->color;
                pc.roughness = draw.type->builtIn == 5 ? 0.85f : 0.7f;
                pc.metallic = 0.0f;
                pc.aoStrength = 1.0f;
                pc.ior = 1.0f;
                pc.materialFlags = MAT_DOUBLE_SIDED | (draw.builtIn ? MAT_VERTEX_COLOR : 0) | (draw.type->wind ? MAT_WIND : 0);
                for (const glm::mat4& model : draw.matrices) {
                    pc.model = model;
                    AddToBatch(draw.mesh, m_descriptorSets[uboIdx], pc);
                }
            }
        }

        // Instanced draws: one per mesh + material combination
        for (DrawBatch& batch : batches) {
            BindMaterialSet(cmd, m_pipelineLayout, batch.set);
            VkBuffer vertexBuffers[] = { batch.mesh->vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, batch.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            const uint32_t count = static_cast<uint32_t>(batch.instances.size());
            const uint32_t first = (count > 1) ? WriteInstances(batch.instances.data(), count) : UINT32_MAX;
            if (first != UINT32_MAX) {
                PushConstantData pc = batch.push;
                pc.materialFlags |= MAT_INSTANCED;
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
                vkCmdDrawIndexed(cmd, batch.mesh->indexCount, count, 0, 0, first);
            } else {
                // A single object, or this frame's instance budget is used up: plain draws
                for (const InstanceData& instance : batch.instances) {
                    PushConstantData pc = batch.push;
                    pc.model = instance.model;
                    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);
                    vkCmdDrawIndexed(cmd, batch.mesh->indexCount, 1, 0, 0, 0);
                }
            }
        }
    };

    // Opaque first, then transparent surfaces sorted back to front without writing depth
    VkPipeline transparentPipeline = (m_graphicsPipelineTransparent != VK_NULL_HANDLE) ? m_graphicsPipelineTransparent : m_graphicsPipeline;
    if (mode == PolygonMode::Solid) {
        if (drawOpaque) {
            DrawNodes(opaqueNodes, m_graphicsPipeline, std::nullopt, false);
            // Decals on the opaque surfaces
            DrawDecals(cmd, scene, viewSlot, meshes);
            // Volumetric clouds over the opaque scene (before the scene colour is copied, so
            // glass and water show them)
            DrawVolumetricClouds(cmd, viewSlot);
            if (renderGizmoNodes && m_graphicsPipelineWireframe != VK_NULL_HANDLE) {
                DrawNodes(scene.GetNodes(), m_graphicsPipelineWireframe, glm::vec4(0.20f, 0.95f, 0.45f, 0.90f), true, true);
            }
            // Test this frame's opaque objects against the finished opaque depth
            if (viewSlot < 2 && m_occlusion[viewSlot].enabled) {
                IssueOcclusionQueries(cmd, viewSlot, scene, opaqueNodes, meshes, viewProj);
            }
        }
        if (drawTransparent) {
            for (const auto& ocean : oceanNodes) DrawOcean(cmd, scene, *ocean, viewSlot);
            DrawLiquidStreams(cmd, viewSlot, meshes);
            DrawNodes(transparentNodes, transparentPipeline, std::nullopt, false);
            // Simulated fluid after the see-through surfaces: refractive glass replaces what's
            // behind it with the opaque scene, which has no fluid in it
            DrawFluidComposite(cmd, viewSlot);
            DrawFluidFoam(cmd, viewSlot);
            DrawPrecipitation(cmd, viewSlot, scene.GetEnvironment());
        }
    } else if (mode == PolygonMode::Wireframe) {
        if (drawOpaque) {
            VkPipeline wirePipeline = (m_graphicsPipelineWireframe != VK_NULL_HANDLE) ? m_graphicsPipelineWireframe : m_graphicsPipeline;
            DrawNodes(scene.GetNodes(), wirePipeline, std::nullopt, true);
        }
    } else if (mode == PolygonMode::WireframeOnShaded) {
        if (drawOpaque) {
            DrawNodes(opaqueNodes, m_graphicsPipeline, std::nullopt, false);
            DrawDecals(cmd, scene, viewSlot, meshes);
            DrawVolumetricClouds(cmd, viewSlot);
        }
        if (drawTransparent) {
            for (const auto& ocean : oceanNodes) DrawOcean(cmd, scene, *ocean, viewSlot);
            DrawLiquidStreams(cmd, viewSlot, meshes);
            DrawNodes(transparentNodes, transparentPipeline, std::nullopt, false);
            // Simulated fluid after the see-through surfaces: refractive glass replaces what's
            // behind it with the opaque scene, which has no fluid in it
            DrawFluidComposite(cmd, viewSlot);
            DrawFluidFoam(cmd, viewSlot);
            DrawPrecipitation(cmd, viewSlot, scene.GetEnvironment());
            if (m_graphicsPipelineWireframe != VK_NULL_HANDLE) {
                DrawNodes(scene.GetNodes(), m_graphicsPipelineWireframe, glm::vec4(1.0f, 1.0f, 1.0f, 0.75f), true);
            }
        }
    }

    // Particles belong to the transparent phase
    if (!drawTransparent) return;

    // Transparent Particle VFX Pass (Soft camera-facing billboards with depth-write disabled)
    auto quadIt = meshes.find(ShapeType::Quad);

    glm::mat4 viewMat = m_lastUBO[uboIdx].view;
    glm::mat4 invView = glm::inverse(viewMat);
    glm::vec3 camRight = glm::vec3(invView[0]);
    glm::vec3 camUp = glm::vec3(invView[1]);
    glm::vec3 camForward = glm::vec3(-invView[2]);
    if (glm::length(camRight) > 0.0001f) camRight = glm::normalize(camRight);
    else camRight = glm::vec3(1.0f, 0.0f, 0.0f);
    if (glm::length(camUp) > 0.0001f) camUp = glm::normalize(camUp);
    else camUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::length(camForward) > 0.0001f) camForward = glm::normalize(camForward);
    else camForward = glm::vec3(0.0f, 0.0f, 1.0f);

    // Sprites, trails and ribbons, mesh particles (instanced, CPU and GPU emitters); heat haze
    // comes later (DrawDistortionParticles)
    DrawParticles(cmd, scene, uboIdx, cameraPos, meshes);

    // Ocean spray: soft alpha-blended sprites, lit like the particles above
    if (!m_spraySprites.empty() && quadIt != meshes.end() && quadIt->second.indexCount > 0) {
        VkPipeline sprayPipeline = (m_graphicsPipelineParticlesAlpha != VK_NULL_HANDLE) ? m_graphicsPipelineParticlesAlpha : m_graphicsPipeline;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprayPipeline);
        BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[uboIdx]);
        VkBuffer vBuffers[] = { quadIt->second.vertexBuffer };
        VkDeviceSize vOffsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, vOffsets);
        vkCmdBindIndexBuffer(cmd, quadIt->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        for (const SpraySprite& sprite : m_spraySprites) {
            PushConstantData ppc{};
            const float s = std::max(0.0005f, sprite.size);
            ppc.model[0] = glm::vec4(camRight * s, 0.0f);
            ppc.model[1] = glm::vec4(camUp * s, 0.0f);
            const float streak = glm::length(sprite.stretch);
            if (streak > 1e-5f) {
                // Long along the motion, as a fast droplet looks
                const glm::vec3 along = sprite.stretch / streak;
                glm::vec3 side = glm::cross(along, camForward);
                if (glm::dot(side, side) > 1e-8f) {
                    ppc.model[0] = glm::vec4(glm::normalize(side) * s, 0.0f);
                    ppc.model[1] = glm::vec4(along * (s + streak), 0.0f);
                }
            }
            ppc.model[2] = glm::vec4(camForward * s, 0.0f);
            ppc.model[3] = glm::vec4(sprite.position, 1.0f);
            ppc.color = sprite.color;
            ppc.roughness = 0.85f;
            ppc.aoStrength = 1.0f;
            ppc.heightScale = -1.0f; // soft round billboard
            if (sprite.frame >= 0.0f) {
                // Animated splash (shader.frag's splash flipbook): frame and shape in the unused slots
                ppc.heightScale = -2.0f;
                ppc.metallic = sprite.frame;
                ppc.roughness = sprite.seed;
            }
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &ppc);
            vkCmdDrawIndexed(cmd, quadIt->second.indexCount, 1, 0, 0, 0);
        }
    }
}

std::string VulkanPipeline::ParticleSheetTexture(ParticleSheet sheet) {
    const std::string key = ParticleSheets::TextureKey(sheet);
    if (m_runtimeTextures.find(key) != m_runtimeTextures.end()) return key;
    // Drawn on a worker thread (a fraction of a second); a soft dot stands in until it's ready
    auto job = m_particleSheetJobs.find(static_cast<int>(sheet));
    if (job == m_particleSheetJobs.end()) {
        m_particleSheetJobs[static_cast<int>(sheet)] = std::async(std::launch::async, [sheet]() { return ParticleSheets::Generate(sheet); });
        return "";
    }
    if (job->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return "";
    const std::vector<uint8_t> pixels = job->second.get();
    m_particleSheetJobs.erase(job);
    SetRuntimeTexture(key, pixels.data(), ParticleSheets::kSize, ParticleSheets::kSize);
    return key;
}

VkDeviceSize VulkanPipeline::UploadParticleQuads(const std::vector<ParticleQuad>& quads, VkBuffer& buffer) {
    const VkDeviceSize bytes = sizeof(ParticleQuad) * static_cast<VkDeviceSize>(quads.size());
    ParticleUpload& upload = m_particleUpload[m_frameIndex];
    if (m_particleUploadCursor + bytes > upload.capacity) {
        // A bigger buffer; the old one may still be used by this frame's commands, so it is
        // released once the frame in flight that used it has finished
        if (upload.buffer) m_retiredParticleUploads.push_back({ upload, m_frameCounter });
        VkDeviceSize capacity = std::max<VkDeviceSize>(upload.capacity * 2, VkDeviceSize(1) << 20);
        while (capacity < bytes) capacity *= 2;
        upload = ParticleUpload{};
        upload.capacity = capacity;
        VulkanBufferUtils::CreateBuffer(capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        upload.buffer, upload.memory);
        vkMapMemory(VulkanContext::Get().GetDevice(), upload.memory, 0, VK_WHOLE_SIZE, 0, &upload.mapped);
        m_particleUploadCursor = 0;
    }
    const VkDeviceSize offset = m_particleUploadCursor;
    std::memcpy(static_cast<char*>(upload.mapped) + offset, quads.data(), static_cast<size_t>(bytes));
    m_particleUploadCursor += bytes;
    buffer = upload.buffer;
    return offset;
}

void VulkanPipeline::ReleaseParticleUploads(bool all) {
    VkDevice device = VulkanContext::Get().GetDevice();
    auto release = [&](ParticleUpload& upload) {
        if (upload.mapped) vkUnmapMemory(device, upload.memory);
        if (upload.buffer) vkDestroyBuffer(device, upload.buffer, nullptr);
        if (upload.memory) vkFreeMemory(device, upload.memory, nullptr);
        upload = ParticleUpload{};
    };
    for (auto it = m_retiredParticleUploads.begin(); it != m_retiredParticleUploads.end();) {
        if (all || m_frameCounter >= it->second + kFramesInFlight) {
            release(it->first);
            it = m_retiredParticleUploads.erase(it);
        } else {
            ++it;
        }
    }
    if (all) {
        for (ParticleUpload& upload : m_particleUpload) release(upload);
    }
}

bool VulkanPipeline::HasDistortionParticles(const Scene& scene) const {
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::ParticleEmitter) continue;
        const auto* emitter = dynamic_cast<const ParticleNode*>(node.get());
        if (emitter && emitter->renderMode == ParticleRenderMode::Distortion) return true;
    }
    return false;
}

void VulkanPipeline::DrawDistortionParticles(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const glm::vec3& cameraPos) {
    if (viewSlot >= 2) return;
    static const std::map<ShapeType, GPUMesh> kNoMeshes;
    DrawParticles(cmd, scene, viewSlot, cameraPos, kNoMeshes, true);
}

void VulkanPipeline::DrawParticles(VkCommandBuffer cmd, const Scene& scene, int viewSlot, const glm::vec3& cameraPos,
                                   const std::map<ShapeType, GPUMesh>& meshes, bool distortionOnly) {
    GpuProfileScope gpuScope(cmd, "Particles");
    struct ParticlePushConstants {
        glm::vec4 sheet;
        glm::vec4 look;
        glm::ivec4 mode;
        glm::vec4 reserved[5];
    };
    static_assert(sizeof(ParticlePushConstants) == sizeof(PushConstantData), "particle push constants share the main range");
    std::vector<ParticleQuad> quads;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::ParticleEmitter) continue;
        const auto* emitter = dynamic_cast<const ParticleNode*>(node.get());
        if (!emitter) continue;
        const bool meshMode = emitter->renderMode == ParticleRenderMode::MeshSphere || emitter->renderMode == ParticleRenderMode::MeshCube;
        if (node->IsCulledByDistance(glm::distance(cameraPos, node->GetWorldPosition(scene)))) continue;
        const bool distortion = emitter->renderMode == ParticleRenderMode::Distortion;
        // Heat haze comes last (DrawDistortionParticles), bending a copy of everything else
        if (distortion != distortionOnly) continue;
        // Mesh emitters' trails are drawn alpha-blended
        const int blend = emitter->renderMode == ParticleRenderMode::BillboardAdditive ? 0 : (distortion ? 2 : 1);
        if (!m_particlePipelines[blend]) continue;

        // Quads: sprites (or mesh instances) [spriteOffset, + sprites), trail segments [offset, + count)
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        uint32_t count = 0;
        VkDeviceSize spriteOffset = 0;
        uint32_t sprites = 0;
        if (emitter->simulationMode == ParticleSimulationMode::GPU_Niagara) {
            auto it = m_gpuParticleDraws.find(node->id);
            if (it == m_gpuParticleDraws.end() || it->second.count == 0) continue;
            buffer = it->second.quads;
            count = it->second.count;
            if (meshMode) {
                // particle_sim.comp writes the sprites first, one per slot
                sprites = it->second.sprites;
                offset = sizeof(ParticleQuad) * static_cast<VkDeviceSize>(sprites);
                count -= sprites;
            }
        } else {
            quads.clear();
            emitter->BuildQuads(scene, cameraPos, blend != 0 && !meshMode, quads);
            if (quads.empty()) continue;
            if (meshMode) {
                // Sprites to the front (mesh instances), segments after
                auto split = std::stable_partition(quads.begin(), quads.end(), [](const ParticleQuad& q) { return q.params.z < -0.5f; });
                sprites = static_cast<uint32_t>(split - quads.begin());
            }
            offset = UploadParticleQuads(quads, buffer);
            spriteOffset = offset;
            offset += sizeof(ParticleQuad) * static_cast<VkDeviceSize>(sprites);
            count = static_cast<uint32_t>(quads.size()) - sprites;
        }

        if (meshMode && sprites > 0 && m_particleMeshPipeline) {
            auto mesh = meshes.find(emitter->renderMode == ParticleRenderMode::MeshCube ? ShapeType::Cube : ShapeType::Sphere);
            if (mesh != meshes.end() && mesh->second.indexCount > 0) {
                ParticlePushConstants push{};
                push.look = glm::vec4(0.0f, emitter->selfGlow, 0.0f, 0.0f);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particleMeshPipeline);
                BindMaterialSet(cmd, m_pipelineLayout, m_descriptorSets[viewSlot]);
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                const VkBuffer streams[2] = { mesh->second.vertexBuffer, buffer };
                const VkDeviceSize streamOffsets[2] = { 0, spriteOffset };
                vkCmdBindVertexBuffers(cmd, 0, 2, streams, streamOffsets);
                vkCmdBindIndexBuffer(cmd, mesh->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, mesh->second.indexCount, sprites, 0, 0, 0);
            }
        }
        if (count == 0) continue;

        std::string texture = emitter->texturePath;
        if (emitter->UsesBuiltinSheet()) texture = ParticleSheetTexture(emitter->builtinSheet);
        ParticlePushConstants push{};
        push.sheet = glm::vec4(static_cast<float>(std::max(emitter->flipbookColumns, 1)), static_cast<float>(std::max(emitter->flipbookRows, 1)),
                               static_cast<float>(emitter->FlipbookFrameCount()), emitter->trailTextureTiling);
        push.look = glm::vec4(emitter->softness, emitter->glowIntensity, emitter->cameraFade, emitter->distortionStrength);
        int flags = 0;
        if (!texture.empty()) flags |= 1;
        if (emitter->flipbookBlend) flags |= 2;
        if (emitter->softness > 0.0f && viewSlot < 2 && m_viewHasSceneDepth[viewSlot]) flags |= 4;
        if (blend == 1 && emitter->lit) flags |= 8;
        push.mode = glm::ivec4(blend, flags, 0, 0);
        push.reserved[0] = glm::vec4(emitter->selfGlow, 0.0f, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipelines[blend]);
        VkDescriptorSet descSet = texture.empty() ? m_descriptorSets[viewSlot] : GetOrCreateDescriptorSet(viewSlot, texture, "", "", "");
        BindMaterialSet(cmd, m_pipelineLayout, descSet);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
        vkCmdDraw(cmd, 6, count, 0, 0);
    }
}

void VulkanPipeline::GetLocalBounds(const Node& node, glm::vec3& localMin, glm::vec3& localMax) const {
    if (node.type == ShapeType::Custom) {
        localMin = node.boundsMin;
        localMax = node.boundsMax;
        if (node.hasSkeleton) {
            // Animation moves vertices outside the rest-pose box
            glm::vec3 center = (localMin + localMax) * 0.5f;
            glm::vec3 half = (localMax - localMin) * 0.75f;
            localMin = center - half;
            localMax = center + half;
        }
    } else if (node.type == ShapeType::Plane) {
        localMin = glm::vec3(-1.0f);
        localMax = glm::vec3(1.0f);
    } else {
        localMin = glm::vec3(-0.5f);
        localMax = glm::vec3(0.5f);
    }
}

bool VulkanPipeline::IsOutsideFrustum(const Node& node, const Scene& scene, const glm::mat4& viewProj) const {
    // Terrain node bounds don't describe the generated grid, so never cull it
    if (node.nodeType == NodeType::Terrain) return false;

    glm::vec3 localMin;
    glm::vec3 localMax;
    GetLocalBounds(node, localMin, localMax);

    const glm::mat4 mvp = viewProj * node.GetWorldTransformMatrix(scene);

    // Outside only if all 8 corners lie beyond the same clip plane
    int outside[6] = {0, 0, 0, 0, 0, 0};
    for (int c = 0; c < 8; ++c) {
        glm::vec3 corner((c & 1) ? localMax.x : localMin.x,
                         (c & 2) ? localMax.y : localMin.y,
                         (c & 4) ? localMax.z : localMin.z);
        glm::vec4 clip = mvp * glm::vec4(corner, 1.0f);
        if (clip.x < -clip.w) ++outside[0];
        if (clip.x >  clip.w) ++outside[1];
        if (clip.y < -clip.w) ++outside[2];
        if (clip.y >  clip.w) ++outside[3];
        if (clip.z < -clip.w) ++outside[4]; // conservative for both [0,1] and [-1,1] depth
        if (clip.z >  clip.w) ++outside[5];
    }
    for (int p = 0; p < 6; ++p) {
        if (outside[p] == 8) return true;
    }
    return false;
}

bool VulkanPipeline::SetEnvironmentHDRI(const std::string& path) {
    if (path == m_environmentPath) return m_environmentTexture != nullptr || path.empty();

    std::shared_ptr<VulkanTexture> texture;
    if (!path.empty()) {
        texture = VulkanTexture::LoadHDRFromFile(path);
        if (!texture) {
            m_environmentPath = path; // don't retry a bad file every frame
            return false;
        }
    }

    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
    if (m_environmentTexture) m_environmentTexture->Cleanup();
    m_environmentTexture = texture;
    m_environmentPath = path;

    VkImageView view = texture ? texture->GetImageView() : m_defaultViewTexture->GetImageView();
    VkSampler sampler = texture ? texture->GetSampler() : m_defaultViewTexture->GetSampler();
    for (int slot = 0; slot < kViewCount; ++slot) {
        SetViewTexture(slot, kEnvironmentBinding, view, sampler);
    }
    return true;
}

bool VulkanPipeline::PrepareMirrorView(const Scene& scene, int viewSlot, UniformBufferObject& outMirrorUbo) const {
    const UniformBufferObject& parent = m_lastUBO[viewSlot];
    const glm::mat4 viewProj = parent.proj * parent.view;
    const glm::vec3 camPos = glm::vec3(parent.cameraPos);

    // Nearest visible mirror that the camera is in front of
    const Node* mirror = nullptr;
    glm::vec3 planePoint(0.0f);
    glm::vec3 planeNormal(0.0f, 1.0f, 0.0f);
    float bestDist = std::numeric_limits<float>::max();
    for (const auto& node : scene.GetNodes()) {
        if (!node || !node->isMirror || node->IsUINode()) continue;
        if (IsOutsideFrustum(*node, scene, viewProj)) continue;

        const glm::mat4 world = node->GetWorldTransformMatrix(scene);
        // Planes face local +Y; quads and other shapes use local +Z
        glm::vec3 axis = glm::vec3(node->type == ShapeType::Plane ? world[1] : world[2]);
        if (glm::length(axis) < 1e-5f) continue;
        glm::vec3 n = glm::normalize(axis);
        glm::vec3 p = glm::vec3(world[3]);
        if (glm::dot(camPos - p, n) <= 0.0f) continue; // looking at the back

        float dist = glm::distance(camPos, p);
        if (dist < bestDist) {
            bestDist = dist;
            mirror = node.get();
            planePoint = p;
            planeNormal = n;
        }
    }
    if (!mirror) return false;

    // Reflection across the plane n.x + d = 0
    const glm::vec3 n = planeNormal;
    const float d = -glm::dot(n, planePoint);
    glm::mat4 reflection(1.0f);
    reflection[0][0] = 1.0f - 2.0f * n.x * n.x; reflection[1][0] = -2.0f * n.x * n.y;        reflection[2][0] = -2.0f * n.x * n.z;        reflection[3][0] = -2.0f * d * n.x;
    reflection[0][1] = -2.0f * n.y * n.x;        reflection[1][1] = 1.0f - 2.0f * n.y * n.y; reflection[2][1] = -2.0f * n.y * n.z;        reflection[3][1] = -2.0f * d * n.y;
    reflection[0][2] = -2.0f * n.z * n.x;        reflection[1][2] = -2.0f * n.z * n.y;        reflection[2][2] = 1.0f - 2.0f * n.z * n.z; reflection[3][2] = -2.0f * d * n.z;
    const glm::mat4 mirrorView = parent.view * reflection;

    // Rebuild the parent's lens as a [0,1]-depth projection (the editor cameras use [-1,1]),
    // so the oblique near plane below maps cleanly onto Vulkan's depth range
    const glm::mat4& P = parent.proj;
    const float nearZ = P[3][2] / (P[2][2] - 1.0f);
    const float farZ = P[3][2] / (P[2][2] + 1.0f);
    const float fovY = 2.0f * std::atan(1.0f / std::abs(P[1][1]));
    const float aspect = std::abs(P[1][1]) / P[0][0];
    glm::mat4 proj = glm::perspectiveRH_ZO(fovY, aspect, nearZ, farZ);
    proj[1][1] *= -1.0f; // Vulkan clip space Y

    // Oblique near plane on the mirror (Lengyel), so nothing behind the mirror is reflected.
    // It sits 1 cm in front of the mirror: the surface a mirror is mounted on (a wall just
    // behind it) would otherwise land on the mirror plane and hide the whole reflection.
    const glm::vec4 planeWorld(n, d - 0.01f);
    const glm::vec4 planeView = glm::transpose(glm::inverse(mirrorView)) * planeWorld;
    auto sgn = [](float v) { return v >= 0.0f ? 1.0f : -1.0f; };
    const glm::vec4 corner = glm::inverse(proj) * glm::vec4(sgn(planeView.x) * sgn(proj[0][0]),
                                                            sgn(planeView.y) * sgn(proj[1][1]), 1.0f, 1.0f);
    const glm::vec4 c = planeView * (1.0f / glm::dot(planeView, corner));
    proj[0][2] = c.x;
    proj[1][2] = c.y;
    proj[2][2] = c.z;
    proj[3][2] = c.w;

    outMirrorUbo = parent;
    outMirrorUbo.view = mirrorView;
    outMirrorUbo.proj = proj;
    outMirrorUbo.cameraPos = glm::vec4(camPos - 2.0f * (glm::dot(n, camPos) + d) * n, 1.0f);
    outMirrorUbo.timeParams.z = 1.0f;        // winding is reversed in the reflection
    outMirrorUbo.volumetricCloudParams.x = 0.0f; // reflections show the flat cloud layer
    outMirrorUbo.postProcessParams.y = 0.0f; // no SSAO in reflections
    return true;
}

const GPUMesh* VulkanPipeline::ResolveNodeMesh(const Node& node,
                                               const std::map<ShapeType, GPUMesh>& meshes,
                                               const std::map<std::string, GPUMesh>& customMeshes,
                                               float distToCam) const {
    auto findCustom = [&](const std::string& key) -> const GPUMesh* {
        auto it = customMeshes.find(key);
        if (it == customMeshes.end()) {
            it = customMeshes.find(std::filesystem::path(key).filename().string());
        }
        return (it != customMeshes.end()) ? &it->second : nullptr;
    };

    // Animated characters draw their own GPU-skinned copy of the mesh
    if (node.hasSkeleton && m_skinning) {
        if (const GPUMesh* skinned = m_skinning->Find(&node)) return skinned;
    }
    // Lightmapped objects draw their own copy with lightmap UVs (at every distance)
    if (const GPUMesh* lightmapped = FindLightmapMesh(node)) return lightmapped;

    if (node.type == ShapeType::Custom) {
        const GPUMesh* mesh = findCustom(node.GetActiveLODMeshKey(distToCam));
        return mesh ? mesh : findCustom(node.customMeshName);
    }
    auto it = meshes.find(node.type);
    return (it != meshes.end()) ? &it->second : nullptr;
}

void VulkanPipeline::CreateShadowPipeline(const std::string& shadowVertShaderPath) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();

    // shadow_frag.spv (alpha cutout) sits next to shadow_vert.spv
    std::string shadowFragShaderPath = shadowVertShaderPath;
    size_t pos = shadowFragShaderPath.rfind("_vert.spv");
    if (pos != std::string::npos) shadowFragShaderPath.replace(pos, 9, "_frag.spv");

    VkShaderModule vertShaderModule = CreateShaderModule(ReadFile(shadowVertShaderPath));
    VkShaderModule fragShaderModule = CreateShaderModule(ReadFile(shadowFragShaderPath));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShaderModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShaderModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, pos);
    attributes[1].binding = 0;
    attributes[1].location = 2;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // matches the main pass, so open meshes still cast
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // Slope-scaled bias against shadow acne on surfaces angled away from the light
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    // Set 0 is the material layout, so cutout casters can bind their own material set (albedo map)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1; // depth only for opaque casters
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_shadowPipelineLayout;
    // Both shadow maps share one format, so their render passes are compatible
    pipelineInfo.renderPass = m_shadowMaps[0].GetRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline!");
    }

    // Alpha-cutout casters: same state plus the discard fragment shader
    pipelineInfo.stageCount = 2;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowCutoutPipeline) != VK_SUCCESS) {
        m_shadowCutoutPipeline = VK_NULL_HANDLE; // cutout casters then cast solid shadows
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanPipeline::CreateSkyPipeline(VkRenderPass renderPass, const std::string& skyVertShaderPath, const std::string& skyFragShaderPath) {
    VkDevice device = VulkanContext::Get().GetDevice();

    VkShaderModule vertModule = CreateShaderModule(ReadFile(skyVertShaderPath));
    VkShaderModule fragModule = CreateShaderModule(ReadFile(skyFragShaderPath));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Vertices are generated from gl_VertexIndex, so there is no vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Drawn first as the background: no depth test or write, so geometry draws over it
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_skyPipeline) != VK_SUCCESS) {
        m_skyPipeline = VK_NULL_HANDLE; // the viewport falls back to its clear colour
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void VulkanPipeline::RenderNormalDepthPrepass(VkCommandBuffer cmd, const Scene& scene,
                                              const std::map<ShapeType, GPUMesh>& meshes,
                                              const std::map<std::string, GPUMesh>& customMeshes,
                                              int viewSlot) {
    if (m_prepassPipeline == VK_NULL_HANDLE) return;

    const int uboIdx = viewSlot;
    const glm::mat4 viewProj = m_lastUBO[uboIdx].proj * m_lastUBO[uboIdx].view;
    const glm::vec3 cameraPos = glm::vec3(m_lastUBO[uboIdx].cameraPos);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prepassPipeline);
    const bool useOcclusion = viewSlot < 2 && OcclusionResultsUsable(viewSlot);

    // Must match prepass.vert. The bottom row of an affine model matrix is (0, 0, 0, 1), so
    // its slots carry the material values the cutout test and SSR need.
    struct PrepassPushConstants {
        glm::mat4 model;
        glm::mat4 prevModel;
    };
    static_assert(sizeof(PrepassPushConstants) == sizeof(PushConstantData), "prepass push constants share the main range");

    // Draws that differ only in their transforms, grouped for instancing (see RenderScene)
    struct PrepassBatch {
        const GPUMesh* mesh;
        VkBuffer previousVertices; // last frame's vertices (skinned) or the mesh's own
        VkDescriptorSet set;
        PrepassPushConstants push; // only the packed bottom-row values are set
        std::vector<InstanceData> instances;
    };
    std::vector<PrepassBatch> batches;
    std::unordered_map<std::string, size_t> batchIndex;

    for (const auto& node : scene.GetNodes()) {
        if (!node || node->IsUINode() || node->isOcean || node->isLiquid) continue;
        // Only real, opaque scene geometry occludes: no editor icons, helpers or particles
        switch (node->nodeType) {
            case NodeType::Empty: case NodeType::AudioSource: case NodeType::Spline:
            case NodeType::ParticleEmitter: case NodeType::Camera: case NodeType::Light:
            case NodeType::Collider: case NodeType::Decal:
                continue;
            default:
                break;
        }
        // Alpha-blended surfaces don't occlude; cut-out ones do (holes are discarded in the shader)
        if (node->color.a < 0.99f && node->alphaCutoff <= 0.0f) continue;

        float distToCam = glm::distance(cameraPos, node->GetWorldPosition(scene));
        if (node->IsCulledByDistance(distToCam)) continue;
        if (IsOutsideFrustum(*node, scene, viewProj)) continue;
        // Same occlusion decision as the main pass, so SSAO / SSR see what's drawn
        if (useOcclusion && m_occlusion[viewSlot % 2].hidden.count(node.get())) continue;

        const GPUMesh* mesh = ResolveNodeMesh(*node, meshes, customMeshes, distToCam);
        if (!mesh || mesh->indexCount == 0) continue;

        // Only the albedo map matters here (for alpha cutout)
        const bool hasRoughnessMap = !node->roughnessMapPath.empty();
        const bool hasMetallicMap = !node->metallicMapPath.empty();
        VkDescriptorSet descSet = (node->alphaCutoff > 0.0f || hasRoughnessMap || hasMetallicMap)
            ? GetOrCreateDescriptorSet(uboIdx, node->texturePath, node->normalMapPath, node->metallicMapPath,
                                       node->roughnessMapPath, node->aoMapPath, node->emissiveMapPath)
            : m_descriptorSets[uboIdx];

        const glm::mat4 model = node->GetWorldTransformMatrix(scene);
        const auto key = std::make_pair(viewSlot, node->id);
        auto prevIt = m_prevModelMatrices.find(key);
        PrepassPushConstants pc{};
        pc.model = model;
        pc.prevModel = (prevIt != m_prevModelMatrices.end()) ? prevIt->second : model;
        m_prevModelMatrices[key] = model;

        const bool hasAlbedo = node->alphaCutoff > 0.0f && !node->texturePath.empty();
        pc.model[0][3] = node->color.a;
        pc.model[1][3] = node->alphaCutoff;
        pc.model[2][3] = static_cast<float>((hasAlbedo ? 1 : 0) | (node->doubleSided ? 2 : 0) | (hasRoughnessMap ? 16 : 0) | (hasMetallicMap ? 32 : 0));
        pc.prevModel[0][3] = node->roughness; // surface values for screen-space reflections
        pc.prevModel[1][3] = node->metallic;

        // Group by mesh, set and the packed material values (the bottom rows); see the main pass
        PrepassPushConstants keyPush{};
        keyPush.model[0][3] = pc.model[0][3]; keyPush.model[1][3] = pc.model[1][3]; keyPush.model[2][3] = pc.model[2][3];
        keyPush.prevModel[0][3] = pc.prevModel[0][3]; keyPush.prevModel[1][3] = pc.prevModel[1][3];
        // Animated characters: last frame's skinned vertices, so limbs get their own motion
        VkBuffer previousVertices = (node->hasSkeleton && m_skinning) ? m_skinning->FindPrevious(node.get()) : VK_NULL_HANDLE;
        if (!previousVertices) previousVertices = mesh->vertexBuffer;
        std::string batchKey(reinterpret_cast<const char*>(&mesh), sizeof(mesh));
        batchKey.append(reinterpret_cast<const char*>(&previousVertices), sizeof(previousVertices));
        batchKey.append(reinterpret_cast<const char*>(&descSet), sizeof(descSet));
        batchKey.append(reinterpret_cast<const char*>(&keyPush), sizeof(keyPush));
        auto [it, inserted] = batchIndex.try_emplace(batchKey, batches.size());
        if (inserted) batches.push_back(PrepassBatch{ mesh, previousVertices, descSet, keyPush, {} });
        glm::mat4 cleanPrev = pc.prevModel;
        cleanPrev[0][3] = 0.0f; cleanPrev[1][3] = 0.0f;
        batches[it->second].instances.push_back(InstanceData{ model, cleanPrev });
    }

    // Terrain foliage
    {
        std::vector<FoliageDraw> foliage;
        GatherFoliage(scene, customMeshes, cameraPos, viewProj, false, foliage);
        for (FoliageDraw& draw : foliage) {
            PrepassPushConstants keyPush{};
            keyPush.model[0][3] = 1.0f;                                          // alpha
            keyPush.model[2][3] = static_cast<float>(2 | (draw.type->wind ? 8 : 0)); // double-sided, wind
            keyPush.prevModel[0][3] = 0.7f;                                      // roughness
            std::string batchKey(reinterpret_cast<const char*>(&draw.mesh), sizeof(draw.mesh));
            batchKey.append(reinterpret_cast<const char*>(&keyPush), sizeof(keyPush));
            auto [it, inserted] = batchIndex.try_emplace(batchKey, batches.size());
            if (inserted) batches.push_back(PrepassBatch{ draw.mesh, draw.mesh->vertexBuffer, m_descriptorSets[uboIdx], keyPush, {} });
            for (const glm::mat4& model : draw.matrices) batches[it->second].instances.push_back(InstanceData{ model, model });
        }
    }

    for (PrepassBatch& batch : batches) {
        BindMaterialSet(cmd, m_pipelineLayout, batch.set);
        VkBuffer vertexBuffers[] = { batch.mesh->vertexBuffer, batch.previousVertices };
        VkDeviceSize offsets[] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, batch.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        const uint32_t count = static_cast<uint32_t>(batch.instances.size());
        const uint32_t first = (count > 1) ? WriteInstances(batch.instances.data(), count) : UINT32_MAX;
        if (first != UINT32_MAX) {
            PrepassPushConstants pc = batch.push; // flag 4: matrices from the instance buffer
            pc.model[2][3] = static_cast<float>(static_cast<int>(pc.model[2][3] + 0.5f) | 4);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PrepassPushConstants), &pc);
            vkCmdDrawIndexed(cmd, batch.mesh->indexCount, count, 0, 0, first);
        } else {
            for (const InstanceData& instance : batch.instances) {
                PrepassPushConstants pc = batch.push;
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 3; ++r) {
                        pc.model[c][r] = instance.model[c][r];
                        pc.prevModel[c][r] = instance.prevModel[c][r];
                    }
                }
                pc.prevModel[3][3] = 1.0f;
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PrepassPushConstants), &pc);
                vkCmdDrawIndexed(cmd, batch.mesh->indexCount, 1, 0, 0, 0);
            }
        }
    }
}

void VulkanPipeline::RenderShadowPass(VkCommandBuffer cmd, const Scene& scene,
                                      const std::map<ShapeType, GPUMesh>& meshes,
                                      const std::map<std::string, GPUMesh>& customMeshes,
                                      int viewSlot, float shadowDistance,
                                      UniformBufferObject& ubo) {
    ubo.shadowParams = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    ubo.localShadowParams = glm::vec4(0.0f, 1.0f / kLocalShadowResolution, 0.0f, 0.0f);
    for (auto& entry : ubo.localShadowLights) entry = glm::vec4(-1.0f);
    if (m_shadowPipeline == VK_NULL_HANDLE) return;

    const std::vector<LightData>& lights = m_viewLights[LightSetFor(viewSlot)];
    const int numLights = static_cast<int>(lights.size());
    const glm::mat4 invView = glm::inverse(ubo.view);
    const glm::vec3 cameraPos = glm::vec3(invView[3]);
    const int uboIdx = viewSlot;

    auto pickUp = [](const glm::vec3& dir) {
        return (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    };

    // Draws every shadow caster into one layer of a shadow map, as seen from lightViewProj.
    // shelter: every solid surface instead (glass and non-casters too), for the weather shelter map.
    auto drawCasters = [&](VulkanShadowMap& shadowMap, uint32_t layer, const glm::mat4& lightViewProj, bool shelter = false) {
        shadowMap.BeginRenderPass(cmd, layer);

        // Casters that differ only in their transform are drawn instanced (see RenderScene)
        struct ShadowBatch {
            const GPUMesh* mesh;
            bool cutout;
            VkDescriptorSet set; // material set (cutout) or the view's set (instance buffer only)
            ShadowPushConstants push;
            std::vector<InstanceData> instances;
        };
        std::vector<ShadowBatch> batches;
        std::unordered_map<std::string, size_t> batchIndex;

        for (const auto& node : scene.GetNodes()) {
            if (!node || node->IsUINode() || (!node->castShadows && !shelter) || node->isOcean || node->isLiquid) continue;
            switch (node->nodeType) {
                case NodeType::Empty: case NodeType::AudioSource: case NodeType::Spline:
                case NodeType::ParticleEmitter: case NodeType::Camera: case NodeType::Light:
                case NodeType::Collider: case NodeType::Decal:
                    continue;
                default:
                    break;
            }
            // Alpha-blended surfaces (glass) would cast solid black shadows; leave them out
            if (node->color.a < 0.99f && node->alphaCutoff <= 0.0f && !shelter) continue;

            float distToCam = glm::distance(cameraPos, node->GetWorldPosition(scene));
            if (node->IsCulledByDistance(distToCam)) continue;
            if (IsOutsideFrustum(*node, scene, lightViewProj)) continue;

            const GPUMesh* mesh = ResolveNodeMesh(*node, meshes, customMeshes, distToCam);
            if (!mesh || mesh->indexCount == 0) continue;

            // Cutout casters run the discard shader with their material set (albedo alpha)
            const bool cutout = node->alphaCutoff > 0.0f && m_shadowCutoutPipeline != VK_NULL_HANDLE;
            ShadowPushConstants spc{};
            spc.lightModelViewProj = lightViewProj;
            VkDescriptorSet descSet = m_descriptorSets[uboIdx];
            if (cutout) {
                descSet = GetOrCreateDescriptorSet(uboIdx, node->texturePath, node->normalMapPath,
                                                   node->metallicMapPath, node->roughnessMapPath,
                                                   node->aoMapPath, node->emissiveMapPath);
                spc.color = node->color;
                spc.alphaCutoff = node->alphaCutoff;
                spc.textureFlags = node->texturePath.empty() ? 0 : TEX_ALBEDO;
            }
            std::string batchKey(reinterpret_cast<const char*>(&mesh), sizeof(mesh));
            batchKey.append(reinterpret_cast<const char*>(&descSet), sizeof(descSet));
            batchKey.append(reinterpret_cast<const char*>(&spc), sizeof(spc));
            auto [it, inserted] = batchIndex.try_emplace(batchKey, batches.size());
            if (inserted) batches.push_back(ShadowBatch{ mesh, cutout, descSet, spc, {} });
            const glm::mat4 model = node->GetWorldTransformMatrix(scene);
            batches[it->second].instances.push_back(InstanceData{ model, model });
        }

        // Terrain foliage that casts shadows (not into the weather shelter map: roofs only)
        if (!shelter) {
            std::vector<FoliageDraw> foliage;
            GatherFoliage(scene, customMeshes, cameraPos, lightViewProj, true, foliage);
            for (FoliageDraw& draw : foliage) {
                ShadowPushConstants spc{};
                spc.lightModelViewProj = lightViewProj;
                spc._pad = draw.type->wind ? 1.0f : 0.0f; // sways (shadow.vert)
                std::string batchKey(reinterpret_cast<const char*>(&draw.mesh), sizeof(draw.mesh));
                batchKey.append(reinterpret_cast<const char*>(&spc), sizeof(spc));
                auto [it, inserted] = batchIndex.try_emplace(batchKey, batches.size());
                if (inserted) batches.push_back(ShadowBatch{ draw.mesh, false, m_descriptorSets[uboIdx], spc, {} });
                for (const glm::mat4& model : draw.matrices) batches[it->second].instances.push_back(InstanceData{ model, model });
            }
        }

        VkPipeline boundPipeline = VK_NULL_HANDLE;
        for (ShadowBatch& batch : batches) {
            VkPipeline pipeline = batch.cutout ? m_shadowCutoutPipeline : m_shadowPipeline;
            if (pipeline != boundPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                boundPipeline = pipeline;
            }
            // Bound for the instance buffer too, not just cutout textures
            BindMaterialSet(cmd, m_shadowPipelineLayout, batch.set);
            VkBuffer vertexBuffers[] = { batch.mesh->vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, batch.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            const uint32_t count = static_cast<uint32_t>(batch.instances.size());
            const uint32_t first = (count > 1) ? WriteInstances(batch.instances.data(), count) : UINT32_MAX;
            if (first != UINT32_MAX) {
                ShadowPushConstants spc = batch.push;
                spc.instanced = 1.0f;
                vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(ShadowPushConstants), &spc);
                vkCmdDrawIndexed(cmd, batch.mesh->indexCount, count, 0, 0, first);
            } else {
                for (const InstanceData& instance : batch.instances) {
                    ShadowPushConstants spc = batch.push;
                    spc.lightModelViewProj = batch.push.lightModelViewProj * instance.model;
                    vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(ShadowPushConstants), &spc);
                    vkCmdDrawIndexed(cmd, batch.mesh->indexCount, 1, 0, 0, 0);
                }
            }
        }

        shadowMap.EndRenderPass(cmd);
    };

    // ---- Physical atmosphere: this view's sky-view LUT and the sun through the air ----
    {
        const SceneEnvironment& env = scene.GetEnvironment();
        if (ubo.atmosphereParams.x > 0.5f) {
            const glm::vec3 sunDir = glm::normalize(glm::vec3(ubo.skyParams));
            const float height = std::max(cameraPos.y, 1.0f);
            if (viewSlot < 2) m_sky.RecordSkyViewLut(cmd, viewSlot, sunDir, height);
            const float horizon = -std::acos(std::clamp(6360.0f / (6360.0f + height / 1000.0f), -1.0f, 1.0f));
            ubo.atmosphereParams = glm::vec4(1.0f, horizon, height / 1000.0f, 0.0f);
            const glm::vec3 noon = VulkanSkyResources::SunTransmittance(glm::vec3(0.0f, 1.0f, 0.0f), height);
            ubo.sunTransmittance = glm::vec4(VulkanSkyResources::SunTransmittance(sunDir, height),
                                             glm::dot(noon, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
        }
        (void)env;
    }

    // ---- Weather shelter: the topmost surfaces around the camera, from straight above ----
    {
        const SceneEnvironment& env = scene.GetEnvironment();
        const bool weather = env.rainAmount > 0.001f || env.snowAmount > 0.001f || env.wetness > 0.001f || env.snowCover > 0.001f;
        const int slot = viewSlot % 2;
        if (!weather) {
            m_shelterParams[slot] = glm::vec4(0.0f);
        } else if (viewSlot < 2) {
            // Centred on the camera, snapped to whole texels so edges don't crawl
            const float texel = kShelterSize / static_cast<float>(kShelterResolution);
            const glm::vec2 center = glm::floor(glm::vec2(cameraPos.x, cameraPos.z) / texel) * texel;
            const float top = std::floor(cameraPos.y) + kShelterRange * 0.5f;
            const glm::mat4 view = glm::lookAt(glm::vec3(center.x, top, center.y), glm::vec3(center.x, top - 1.0f, center.y), glm::vec3(0.0f, 0.0f, -1.0f));
            const float half = kShelterSize * 0.5f;
            m_shelterMatrix[slot] = glm::orthoRH_ZO(-half, half, -half, half, 0.0f, kShelterRange) * view;
            m_shelterParams[slot] = glm::vec4(1.0f, top, kShelterRange, texel);
            drawCasters(m_shelterMaps[slot], 0, m_shelterMatrix[slot], true);
        }
        ubo.shelterMatrix = m_shelterMatrix[slot];
        ubo.shelterParams = m_shelterParams[slot];
    }
    if (ubo.enableShadows != 1) return;

    // ---- Sun: the first shadow-casting directional light, with cascades ----
    int sunIndex = -1;
    for (int i = 0; i < numLights; ++i) {
        if (lights[i].position.w < 0.5f && lights[i].spotAnglesAndShadow.z > 0.5f) {
            sunIndex = i;
            break;
        }
    }

    if (sunIndex >= 0) {
        const float resolution = static_cast<float>(kShadowMapResolution);
        const glm::vec3 toLight = glm::normalize(glm::vec3(lights[sunIndex].position));
        const glm::vec3 up = pickUp(toLight);
        const glm::vec3 camRight = glm::normalize(glm::vec3(invView[0]));
        const glm::vec3 camUp = glm::normalize(glm::vec3(invView[1]));
        const glm::vec3 camForward = -glm::normalize(glm::vec3(invView[2]));

        // Camera lens from its (OpenGL-style) projection
        const glm::mat4& P = ubo.proj;
        float nearZ = P[3][2] / (P[2][2] - 1.0f);
        if (!(nearZ > 0.0f) || nearZ > 10.0f) nearZ = 0.1f;
        const float tanX = 1.0f / std::abs(P[0][0]);
        const float tanY = 1.0f / std::abs(P[1][1]);
        const float farZ = std::max(shadowDistance, nearZ + 1.0f);

        // "Practical" split: mostly logarithmic (detail near the camera), partly even
        const int cascadeCount = static_cast<int>(VulkanShadowMap::kMaxCascades);
        float splits[VulkanShadowMap::kMaxCascades + 1];
        splits[0] = nearZ;
        for (int i = 1; i <= cascadeCount; ++i) {
            float t = static_cast<float>(i) / cascadeCount;
            float logSplit = nearZ * std::pow(farZ / nearZ, t);
            float evenSplit = nearZ + (farZ - nearZ) * t;
            splits[i] = glm::mix(evenSplit, logSplit, 0.75f);
        }

        const float casterPadding = 100.0f; // tall casters outside a slice still cast into it
        float cascadeTexel[VulkanShadowMap::kMaxCascades] = {};
        for (int c = 0; c < cascadeCount; ++c) {
            // Bounding sphere of this slice of the view frustum (a sphere keeps the map's size
            // constant as the camera turns, which stops edges swimming)
            glm::vec3 corners[8];
            int k = 0;
            for (float d : { splits[c], splits[c + 1] }) {
                for (float sx : { -1.0f, 1.0f }) {
                    for (float sy : { -1.0f, 1.0f }) {
                        corners[k++] = cameraPos + camForward * d + camRight * (sx * d * tanX) + camUp * (sy * d * tanY);
                    }
                }
            }
            glm::vec3 center(0.0f);
            for (const glm::vec3& corner : corners) center += corner;
            center /= 8.0f;
            float radius = 0.0f;
            for (const glm::vec3& corner : corners) radius = std::max(radius, glm::distance(center, corner));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            // Snap the centre to whole shadow texels so edges don't crawl as the camera moves
            const float worldTexel = (2.0f * radius) / resolution;
            const glm::mat4 lightRotation = glm::lookAt(glm::vec3(0.0f), -toLight, up);
            glm::vec3 centerLS = glm::vec3(lightRotation * glm::vec4(center, 1.0f));
            centerLS.x = std::floor(centerLS.x / worldTexel) * worldTexel;
            centerLS.y = std::floor(centerLS.y / worldTexel) * worldTexel;
            center = glm::vec3(glm::inverse(lightRotation) * glm::vec4(centerLS, 1.0f));

            const glm::mat4 lightView = glm::lookAt(center + toLight * (radius + casterPadding), center, up);
            const glm::mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterPadding);
            ubo.shadowMatrices[c] = lightProj * lightView;
            cascadeTexel[c] = worldTexel;

            drawCasters(m_shadowMaps[viewSlot % 2], static_cast<uint32_t>(c), ubo.shadowMatrices[c]);
        }
        ubo.shadowCascadeTexel = glm::vec4(cascadeTexel[0], cascadeTexel[1], cascadeTexel[2], cascadeTexel[3]);
        ubo.shadowParams = glm::vec4(1.0f, static_cast<float>(sunIndex), 1.0f / resolution, static_cast<float>(cascadeCount));
    }

    // ---- Point and spot lights: the nearest shadow casters, within the layer budget ----
    std::vector<std::pair<float, int>> candidates;
    for (int i = 0; i < numLights; ++i) {
        const LightData& l = lights[i];
        if (l.position.w > 0.5f && l.spotAnglesAndShadow.z > 0.5f) {
            candidates.emplace_back(glm::distance(cameraPos, glm::vec3(l.position)), i);
        }
    }
    std::sort(candidates.begin(), candidates.end());

    // Cube faces in the order the shader expects: +X, -X, +Y, -Y, +Z, -Z
    static const glm::vec3 kFaceDirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    VulkanShadowMap& localMap = m_localShadowMaps[viewSlot % 2];
    uint32_t nextLayer = 0;
    int entries = 0;
    for (const auto& [dist, lightIndex] : candidates) {
        if (entries >= kMaxLocalShadowLights) break;
        const LightData& light = lights[lightIndex];
        const bool isPoint = light.position.w < 1.5f;
        const uint32_t layersNeeded = isPoint ? 6u : 1u;
        if (nextLayer + layersNeeded > kLocalShadowLayers) continue; // a spot may still fit

        const glm::vec3 pos = glm::vec3(light.position);
        const float range = std::max(light.spotDirAndRange.w, 1.0f);
        float fov;
        if (isPoint) {
            // Slightly over 90 degrees so soft-edge taps near a face border stay on that face
            fov = glm::radians(92.0f);
            const glm::mat4 proj = glm::perspectiveRH_ZO(fov, 1.0f, 0.05f, range);
            for (uint32_t f = 0; f < 6; ++f) {
                const glm::mat4 viewProj = proj * glm::lookAt(pos, pos + kFaceDirs[f], pickUp(kFaceDirs[f]));
                ubo.localShadowMatrices[nextLayer + f] = viewProj;
                drawCasters(localMap, nextLayer + f, viewProj);
            }
        } else {
            const glm::vec3 dir = glm::normalize(glm::vec3(light.spotDirAndRange));
            const float outerAngle = std::acos(std::clamp(light.spotAnglesAndShadow.y, -1.0f, 1.0f));
            fov = std::clamp(2.0f * outerAngle + glm::radians(4.0f), glm::radians(10.0f), glm::radians(170.0f));
            const glm::mat4 viewProj = glm::perspectiveRH_ZO(fov, 1.0f, 0.05f, range) * glm::lookAt(pos, pos + dir, pickUp(dir));
            ubo.localShadowMatrices[nextLayer] = viewProj;
            drawCasters(localMap, nextLayer, viewProj);
        }

        // World size of one shadow texel per metre of distance from the light (normal offset)
        const float texelPerMetre = 2.0f * std::tan(fov * 0.5f) / static_cast<float>(kLocalShadowResolution);
        ubo.localShadowLights[entries] = glm::vec4(static_cast<float>(lightIndex), static_cast<float>(nextLayer),
                                                   isPoint ? 1.0f : 0.0f, texelPerMetre);
        nextLayer += layersNeeded;
        ++entries;
    }
    ubo.localShadowParams.x = static_cast<float>(entries);
}

void VulkanPipeline::RenderGrid(VkCommandBuffer cmd, const GPUMesh& gridMesh, const GPUMesh& axesMesh, bool isPreview) {
    if (m_overlayPipeline == VK_NULL_HANDLE) return;

    VkDescriptorSet descriptorSet = m_descriptorSets[isPreview ? 1 : 0];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_overlayPipeline);
    BindMaterialSet(cmd, m_pipelineLayout, descriptorSet);

    // 1. Draw Grid lines
    if (gridMesh.indexCount > 0) {
        PushConstantData pc{};
        pc.model = glm::mat4(1.0f);
        pc.color = glm::vec4(0.35f, 0.35f, 0.40f, 0.70f);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pc);

        VkBuffer vertexBuffers[] = { gridMesh.vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, gridMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, gridMesh.indexCount, 1, 0, 0, 0);
    }

    // 2. Draw Center Axes
    if (axesMesh.indexCount >= 4) {
        VkBuffer vertexBuffers[] = { axesMesh.vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, axesMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // X axis (Red)
        PushConstantData pcX{};
        pcX.model = glm::mat4(1.0f);
        pcX.color = glm::vec4(0.90f, 0.25f, 0.25f, 0.95f);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pcX);
        vkCmdDrawIndexed(cmd, 2, 1, 0, 0, 0);

        // Z axis (Blue)
        PushConstantData pcZ{};
        pcZ.model = glm::mat4(1.0f);
        pcZ.color = glm::vec4(0.25f, 0.50f, 0.95f, 0.95f);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pcZ);
        vkCmdDrawIndexed(cmd, 2, 1, 2, 0, 0);
    }
}

VkShaderModule VulkanPipeline::CreateShaderModule(const std::vector<char>& code) {
    auto& ctx = VulkanContext::Get();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(ctx.GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    return shaderModule;
}
