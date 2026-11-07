#include "VulkanParticleSim.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include "Scene/Scene.h"
#include "Scene/ParticleCollision.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

constexpr uint32_t kWorkgroupSize = 64;   // local_size_x in particle_sim.comp
constexpr uint32_t kMaxHitRequests = 256; // kMaxRequests in particle_sim.comp
constexpr uint32_t kWaterGridResolution = 48;
constexpr uint32_t kBindings = 7;

// Must match particle_sim.comp (std430)
struct GpuParticle {
    glm::vec4 position, velocity, spin, misc;
    glm::uvec4 info;
};
static_assert(sizeof(GpuParticle) == 80, "Particle layout");

struct GpuSolid {
    glm::mat4 toLocal;
    glm::mat4 toWorld;
    glm::vec4 info;
};
struct GpuHeightField {
    glm::mat4 toLocal;
    glm::mat4 toWorld;
    glm::vec4 size;
    glm::uvec4 data;
};
struct GpuFrameData {
    glm::mat4 toWorld;
    glm::mat4 toLocal;
    glm::vec4 origin, shape, boxExtents, life, startColor, endColor, forces, vortexAxis, attractor, rotation, flipbook, trail, trail2,
        collision, collision2, collision3;
    glm::uvec4 counts;
    glm::vec4 waterGrid;
    GpuSolid solids[ParticleColliders::kMaxSolids];
    GpuHeightField heightFields[5];
};
static_assert(sizeof(GpuFrameData) == 128 + 18 * 16 + 144 * 64 + 160 * 5, "FrameData layout");

struct GpuFeedback {
    uint32_t alive;
    uint32_t hits;
    uint32_t pad[2];
    glm::vec4 lastHit;
};
constexpr VkDeviceSize kStateBytes = 16 + 16 * 2 * kMaxHitRequests;

struct PushConstants {
    uint32_t stage;
    uint32_t waterField;
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

void VulkanParticleSim::Init() {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkDescriptorSetLayoutBinding bindings[kBindings]{};
    for (uint32_t b = 0; b < kBindings; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kBindings;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle simulation descriptor set layout!");
    }
    VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle simulation pipeline layout!");
    }
    const std::vector<char> code = ReadSpirv("shaders/particle_sim_comp.spv");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle simulation shader module!");
    }
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_pipelineLayout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create particle simulation pipeline!");
}

void VulkanParticleSim::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    vkDeviceWaitIdle(device);
    for (auto& entry : m_emitters) Destroy(entry.second);
    m_emitters.clear();
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
}

void VulkanParticleSim::Destroy(Emitter& emitter) {
    VkDevice device = VulkanContext::Get().GetDevice();
    auto release = [&](Buffer& b) {
        if (b.mapped) vkUnmapMemory(device, b.memory);
        if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(device, b.memory, nullptr);
        b = Buffer{};
    };
    release(emitter.particles);
    release(emitter.trails);
    release(emitter.quads);
    release(emitter.state);
    for (int f = 0; f < 2; ++f) {
        release(emitter.frameData[f]);
        release(emitter.heights[f]);
        release(emitter.feedback[f]);
        emitter.heightVersions[f].clear();
        emitter.feedbackValid[f] = false;
    }
    if (emitter.pool) vkDestroyDescriptorPool(device, emitter.pool, nullptr);
    emitter.pool = VK_NULL_HANDLE;
    emitter.slots = 0;
}

void VulkanParticleSim::Create(Emitter& emitter, uint32_t slots, uint32_t trailPoints, uint32_t quadStride) {
    VkDevice device = VulkanContext::Get().GetDevice();
    vkDeviceWaitIdle(device); // the old buffers may still be in use by frames in flight
    Destroy(emitter);
    emitter.slots = slots;
    emitter.trailPoints = trailPoints;
    emitter.quadStride = quadStride;
    emitter.clear = true;
    auto deviceBuffer = [&](Buffer& b, VkDeviceSize size, VkBufferUsageFlags extra) {
        b.size = std::max<VkDeviceSize>(size, 16);
        VulkanBufferUtils::CreateBuffer(b.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, b.buffer, b.memory);
    };
    auto hostBuffer = [&](Buffer& b, VkDeviceSize size) {
        b.size = std::max<VkDeviceSize>(size, 16);
        VulkanBufferUtils::CreateBuffer(b.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b.buffer, b.memory);
        vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
        std::memset(b.mapped, 0, static_cast<size_t>(b.size));
    };
    deviceBuffer(emitter.particles, sizeof(GpuParticle) * static_cast<VkDeviceSize>(slots), 0);
    deviceBuffer(emitter.trails, sizeof(glm::vec4) * static_cast<VkDeviceSize>(slots) * std::max(trailPoints, 1u), 0);
    deviceBuffer(emitter.quads, sizeof(ParticleQuad) * static_cast<VkDeviceSize>(slots) * quadStride, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    deviceBuffer(emitter.state, kStateBytes, 0);
    for (int f = 0; f < 2; ++f) {
        hostBuffer(emitter.frameData[f], sizeof(GpuFrameData));
        hostBuffer(emitter.heights[f], 1024 * sizeof(float));
        hostBuffer(emitter.feedback[f], sizeof(GpuFeedback));
    }

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBindings * 2 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &emitter.pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle simulation descriptor pool!");
    }
    VkDescriptorSetLayout layouts[2] = { m_setLayout, m_setLayout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = emitter.pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &allocInfo, emitter.sets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate particle simulation descriptor sets!");
    }
    for (int f = 0; f < 2; ++f) WriteSets(emitter, f);
}

void VulkanParticleSim::WriteSets(Emitter& emitter, int frame) {
    const Buffer* buffers[kBindings] = { &emitter.particles, &emitter.trails, &emitter.quads, &emitter.frameData[frame],
                                         &emitter.heights[frame], &emitter.feedback[frame], &emitter.state };
    VkDescriptorBufferInfo infos[kBindings]{};
    VkWriteDescriptorSet writes[kBindings]{};
    for (uint32_t b = 0; b < kBindings; ++b) {
        infos[b] = { buffers[b]->buffer, 0, VK_WHOLE_SIZE };
        writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[b].dstSet = emitter.sets[frame];
        writes[b].dstBinding = b;
        writes[b].descriptorCount = 1;
        writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[b].pBufferInfo = &infos[b];
    }
    vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), kBindings, writes, 0, nullptr);
}

void VulkanParticleSim::Step(VkCommandBuffer cmd, Emitter& emitter, ParticleNode& node, Scene& scene, uint32_t frameIndex) {
    VkDevice device = VulkanContext::Get().GetDevice();

    // What the GPU reported for this frame slot last time (its fence has been waited on)
    auto* feedback = static_cast<GpuFeedback*>(emitter.feedback[frameIndex].mapped);
    if (emitter.feedbackValid[frameIndex]) {
        node.SetGpuFeedback(feedback->alive, feedback->hits, glm::vec3(feedback->lastHit));
    }
    std::memset(feedback, 0, sizeof(GpuFeedback));
    emitter.feedbackValid[frameIndex] = true;

    const float dt = std::min(node.TakeGpuTime(), 0.1f);
    const uint32_t spawns = std::min(node.TakeGpuSpawns(), emitter.slots);
    emitter.clock += dt;
    emitter.frameSeed = emitter.frameSeed * 1664525u + 1013904223u;

    const bool world = node.worldSpace;
    const glm::mat4 toWorld = world ? glm::mat4(1.0f) : node.GetWorldTransformMatrix(scene);
    const glm::mat4 toLocal = world ? glm::mat4(1.0f) : glm::inverse(toWorld);
    const glm::vec3 origin = world ? node.GetWorldPosition(scene) : glm::vec3(0.0f);

    auto* frame = static_cast<GpuFrameData*>(emitter.frameData[frameIndex].mapped);
    std::memset(frame, 0, sizeof(GpuFrameData));
    frame->toWorld = toWorld;
    frame->toLocal = toLocal;
    frame->origin = glm::vec4(origin, dt);
    frame->shape = glm::vec4(static_cast<float>(node.emitterShape), node.emitterRadius, glm::radians(node.coneAngle), node.startSpeed);
    frame->boxExtents = glm::vec4(node.emitterBoxExtents, emitter.clock);
    frame->life = glm::vec4(node.lifetimeMin, std::max(node.lifetimeMax, node.lifetimeMin), node.startSize, node.endSize);
    frame->startColor = node.startColor;
    frame->endColor = node.endColor;
    frame->forces = glm::vec4(-9.81f * node.gravityMultiplier, node.drag, node.enableVortexModule ? node.vortexStrength : 0.0f,
                              node.enableAttractorModule ? node.attractorStrength : 0.0f);
    frame->vortexAxis = glm::vec4(node.vortexAxis, node.enableCurlNoiseModule ? node.curlNoiseStrength : 0.0f);
    frame->attractor = glm::vec4(origin + node.attractorOffset, node.curlNoiseFrequency);
    frame->rotation = glm::vec4(glm::radians(node.startRotationMin), glm::radians(node.startRotationMax),
                                glm::radians(node.angularVelocityMin), glm::radians(node.angularVelocityMax));
    frame->flipbook = glm::vec4(static_cast<float>(node.FlipbookFrameCount()), node.flipbookMode == ParticleFlipbookMode::FixedRate ? 1.0f : 0.0f,
                                node.flipbookMode == ParticleFlipbookMode::FixedRate ? node.flipbookFps : node.flipbookCycles,
                                node.flipbookRandomStart ? 1.0f : 0.0f);
    const float trailMode = emitter.trailPoints >= 2 && node.trailMode == ParticleTrailMode::PerParticle ? 1.0f
                          : (node.trailMode == ParticleTrailMode::Ribbon ? 2.0f : 0.0f);
    frame->trail = glm::vec4(trailMode, static_cast<float>(emitter.trailPoints), std::max(node.trailInterval, 0.001f), node.trailWidth);
    frame->trail2 = glm::vec4(node.trailTaper ? 1.0f : 0.0f, node.trailDrawParticles ? 1.0f : 0.0f, node.trailTextureTiling, node.velocityStretch);
    frame->collision = glm::vec4(static_cast<float>(node.collisionMode), node.collisionPlaneHeight, node.collisionRadiusScale, node.bounce);
    frame->collision2 = glm::vec4(node.friction, node.killOnCollision ? 1.0f : 0.0f, node.lifetimeLossOnHit, static_cast<float>(std::max(node.hitSpawnCount, 0)));
    frame->collision3 = glm::vec4(node.hitSpawnSpeed, node.hitSpawnSize, 0.0f, 0.0f);
    frame->counts = glm::uvec4(emitter.slots, spawns, emitter.frameSeed, emitter.quadStride);

    // The scene's shapes around the emitter, as far as its particles can travel
    uint32_t waterField = 0;
    if (node.collisionMode == ParticleCollisionMode::Scene) {
        const glm::vec3 center = node.GetWorldPosition(scene);
        const float reach = std::clamp(node.startSpeed * node.lifetimeMax + std::max(node.emitterRadius, glm::length(node.emitterBoxExtents)) +
                                       std::abs(node.gravityMultiplier) * 4.9f * node.lifetimeMax * node.lifetimeMax + 2.0f, 2.0f, 250.0f);
        ParticleColliders colliders;
        ParticleColliders::Settings settings;
        settings.excludeNodeId = node.id;
        settings.water = node.collideWithWater;
        colliders.Build(scene, center - glm::vec3(reach), center + glm::vec3(reach), settings);
        const auto& solids = colliders.Solids();
        for (size_t s = 0; s < solids.size(); ++s) {
            frame->solids[s] = { solids[s].toLocal, solids[s].toWorld, glm::vec4(solids[s].sphere ? 1.0f : 0.0f, solids[s].water ? 1.0f : 0.0f, 0.0f, 0.0f) };
        }
        const auto& terrains = colliders.Terrains();
        std::vector<float> water;
        glm::vec2 waterOrigin(0.0f), waterExtent(0.0f);
        const bool hasWater = colliders.SampleWaterGrid(kWaterGridResolution, waterOrigin, waterExtent, water);
        // Heights: the terrains', then the water grid's
        size_t floats = 0;
        for (const auto& t : terrains) floats += t.heights->size();
        const size_t waterOffset = floats;
        floats += hasWater ? water.size() : 0;
        Buffer& heights = emitter.heights[frameIndex];
        if (floats * sizeof(float) > heights.size) {
            if (heights.mapped) vkUnmapMemory(device, heights.memory);
            vkDestroyBuffer(device, heights.buffer, nullptr);
            vkFreeMemory(device, heights.memory, nullptr);
            heights.size = static_cast<VkDeviceSize>(floats * sizeof(float) * 3 / 2);
            VulkanBufferUtils::CreateBuffer(heights.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, heights.buffer, heights.memory);
            vkMapMemory(device, heights.memory, 0, VK_WHOLE_SIZE, 0, &heights.mapped);
            emitter.heightVersions[frameIndex].clear();
            WriteSets(emitter, static_cast<int>(frameIndex));
        }
        float* out = static_cast<float*>(heights.mapped);
        std::vector<uint64_t> versions;
        for (const auto& t : terrains) versions.push_back(t.version ^ (static_cast<uint64_t>(t.nodeId) << 40));
        const bool terrainsCurrent = versions == emitter.heightVersions[frameIndex];
        size_t offset = 0;
        for (size_t t = 0; t < terrains.size(); ++t) {
            const auto& terrain = terrains[t];
            if (!terrainsCurrent) std::memcpy(out + offset, terrain.heights->data(), terrain.heights->size() * sizeof(float));
            frame->heightFields[t] = { terrain.toLocal, terrain.toWorld, glm::vec4(terrain.size, terrain.heightScale, static_cast<float>(terrain.resolution)),
                                       glm::uvec4(static_cast<uint32_t>(offset), 0, 0, 0) };
            offset += terrain.heights->size();
        }
        emitter.heightVersions[frameIndex] = versions;
        if (hasWater) {
            std::memcpy(out + waterOffset, water.data(), water.size() * sizeof(float));
            const glm::vec3 waterCenter(waterOrigin.x + waterExtent.x * 0.5f, 0.0f, waterOrigin.y + waterExtent.y * 0.5f);
            const glm::mat4 waterWorld = glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(waterCenter, 1));
            frame->heightFields[4] = { glm::inverse(waterWorld), waterWorld, glm::vec4(waterExtent, 1.0f, static_cast<float>(kWaterGridResolution)),
                                       glm::uvec4(static_cast<uint32_t>(waterOffset), 0, 0, 0) };
            frame->waterGrid = glm::vec4(waterOrigin, waterExtent);
            waterField = 1;
        }
        frame->collision3.z = static_cast<float>(solids.size());
        frame->collision3.w = static_cast<float>(terrains.size());
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &emitter.sets[frameIndex], 0, nullptr);
    // Earlier frames may still be drawing the quads
    VkMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
    if (emitter.clear || node.TakeGpuReset()) {
        // Start empty: every slot dead, no trails, the ring at its start
        for (Buffer* b : { &emitter.particles, &emitter.trails, &emitter.quads, &emitter.state }) vkCmdFillBuffer(cmd, b->buffer, 0, VK_WHOLE_SIZE, 0);
        VkMemoryBarrier cleared{};
        cleared.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cleared.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cleared, 0, nullptr, 0, nullptr);
        emitter.clear = false;
    }

    PushConstants push{};
    push.waterField = waterField;
    auto run = [&](uint32_t stage, uint32_t threads) {
        push.stage = stage;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, std::max((threads + kWorkgroupSize - 1) / kWorkgroupSize, 1u), 1, 1);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    };
    if (dt > 0.0f) run(0, emitter.slots);
    const uint32_t spawnThreads = std::min(spawns + kMaxHitRequests * static_cast<uint32_t>(std::max(node.hitSpawnCount, 0)), emitter.slots);
    run(1, spawnThreads);
    run(2, 1);
    run(3, emitter.slots);

    // The quads are drawn next; the feedback is read on the CPU
    VkMemoryBarrier after{};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &after, 0, nullptr, 0, nullptr);
}

void VulkanParticleSim::Update(VkCommandBuffer cmd, Scene& scene, uint32_t frameIndex) {
    frameIndex %= 2;
    for (auto& entry : m_emitters) entry.second.seen = false;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::ParticleEmitter) continue;
        auto* particleNode = dynamic_cast<ParticleNode*>(node.get());
        if (!particleNode || particleNode->simulationMode != ParticleSimulationMode::GPU_Niagara) continue;
        const uint32_t slots = std::clamp(particleNode->maxParticles, 16u, 1000000u);
        // Trail points within the emitter's quad budget
        uint32_t trailPoints = 0;
        if (particleNode->trailMode == ParticleTrailMode::PerParticle) {
            const uint32_t room = kMaxQuads / slots;
            trailPoints = std::min(static_cast<uint32_t>(std::clamp(particleNode->trailLength, 2, 64)), room > 1 ? room - 1 : 0u);
            if (trailPoints < 2) trailPoints = 0;
        }
        const uint32_t quadStride = 1 + std::max(trailPoints, particleNode->trailMode == ParticleTrailMode::Ribbon ? 1u : 0u);
        Emitter& emitter = m_emitters[node->id];
        emitter.seen = true;
        if (emitter.slots != slots || emitter.trailPoints != trailPoints || emitter.quadStride != quadStride) {
            Create(emitter, slots, trailPoints, quadStride);
        }
        Step(cmd, emitter, *particleNode, scene, frameIndex);
    }
    for (auto it = m_emitters.begin(); it != m_emitters.end();) {
        if (!it->second.seen) {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            Destroy(it->second);
            it = m_emitters.erase(it);
        } else {
            ++it;
        }
    }
}

std::map<uint32_t, VulkanParticleSim::Draw> VulkanParticleSim::Draws() const {
    std::map<uint32_t, Draw> draws;
    for (const auto& [id, emitter] : m_emitters) {
        if (emitter.slots > 0) draws[id] = { emitter.quads.buffer, emitter.slots * emitter.quadStride, emitter.slots };
    }
    return draws;
}
