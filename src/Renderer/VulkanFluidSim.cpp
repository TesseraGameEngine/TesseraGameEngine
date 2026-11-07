#include "VulkanFluidSim.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include "Scene/Scene.h"
#include "Scene/Liquids.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

constexpr float kPi = 3.14159265f;
// Step size for 2.5 cm particles; finer fluid steps proportionally shorter so a particle moves
// the same share of its spacing per step (longer steps let fine fluid blow apart)
constexpr float kSubstep = 1.0f / 120.0f;
constexpr float kSubstepSpacing = 0.025f;
constexpr float kMaxStepTime = 4.0f / 120.0f; // simulated time per frame at most (slow frames run slow)
constexpr int kMaxSubsteps = 16;               // the shortest substeps (fine fluid) fit this many in kMaxStepTime
// Obstacles move through the fluid smoothly, following their nodes (which jump with each fixed
// physics step) with this time constant
constexpr float kObstacleSmoothing = 1.0f / 60.0f;
constexpr int kSolverIterations = 3;
constexpr uint32_t kWorkgroupSize = 256; // local_size_x in fluid_sim.comp
constexpr uint32_t kMaxCells = 8u * 1024u * 1024u;

// Must match the stages in fluid_sim.comp
enum Stage : uint32_t {
    kClearCells = 0, kPredict = 1, kScan = 2, kScatter = 3, kLambda = 4, kDelta = 5, kApply = 6,
    kVelocity = 7, kViscosity = 8, kFinish = 9, kSpawn = 10
};

// Must match Params in fluid_sim.comp
struct FluidSimPushConstants {
    glm::vec4 boxMin;   // w = particle radius
    glm::vec4 boxMax;   // w = kernel radius
    glm::ivec4 grid;    // w = total cells
    uint32_t count;
    uint32_t stage;
    uint32_t obstacleCount;
    float dt;
    float invRestDensity;
    float viscosity;
    float drawRadius;
    float relaxation;
    uint32_t substep;
    uint32_t pad[3];
};

// Must match FrameData in fluid_sim.comp (std430)
struct GpuObstacle {
    glm::mat4 toLocal;
    glm::mat4 toWorld;
    glm::vec4 info;
};
struct GpuSink {
    glm::mat4 toLocal;
    glm::vec4 info;
};
struct GpuFrameData {
    glm::vec4 spawnPoint;
    glm::vec4 spawnVelocity;
    glm::uvec4 counts;
    GpuSink sinks[VulkanFluidSim::kMaxSinks];
    GpuObstacle obstacles[kMaxSubsteps * VulkanFluidSim::kMaxObstacles];
};
static_assert(sizeof(GpuFrameData) == 48 + 80 * 16 + 144 * 64 * 16, "FrameData layout");

// Must match Feedback in fluid_sim.comp (std430)
struct GpuFeedback {
    uint32_t sinkCounts[VulkanFluidSim::kMaxSinks];
    uint32_t spawnTicket;
    uint32_t pad[15];
    int32_t contacts[VulkanFluidSim::kMaxObstacles * 8];
};
static_assert(sizeof(GpuFeedback) == 128 + 2048, "Feedback layout");

float Poly6(float r2, float h) {
    const float h2 = h * h;
    if (r2 >= h2) return 0.0f;
    const float d = h2 - r2;
    return 315.0f / (64.0f * kPi * std::pow(h, 9.0f)) * d * d * d;
}

void HashFloat(uint64_t& h, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    h = (h ^ bits) * 1099511628211ull;
}

std::vector<char> ReadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open shader: " + path);
    std::vector<char> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
}

void ComputeBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// A node's mesh as a transform of the unit box (models: their bounds)
glm::mat4 UnitBoxFrame(const Node& node, const Scene& scene) {
    glm::mat4 frame = node.GetWorldTransformMatrix(scene);
    if (node.type == ShapeType::Custom) {
        const glm::vec3 boundsSize = glm::max(node.boundsMax - node.boundsMin, glm::vec3(1e-4f));
        frame = frame * glm::mat4(glm::vec4(boundsSize.x, 0, 0, 0), glm::vec4(0, boundsSize.y, 0, 0),
                                  glm::vec4(0, 0, boundsSize.z, 0), glm::vec4((node.boundsMax + node.boundsMin) * 0.5f, 1));
    }
    return frame;
}

bool OverlapsBox(const glm::mat4& frame, const glm::vec3& boxMin, const glm::vec3& boxMax) {
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f);
        const glm::vec3 p = glm::vec3(frame * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    return !(glm::any(glm::lessThan(hi, boxMin)) || glm::any(glm::greaterThan(lo, boxMax)));
}

} // namespace

uint64_t VulkanFluidSim::KeyFor(const Node& node) {
    uint64_t h = 1469598103934665603ull;
    HashFloat(h, node.fluidSize.x);
    HashFloat(h, node.fluidSize.y);
    HashFloat(h, node.fluidSize.z);
    HashFloat(h, node.fluidFill);
    HashFloat(h, node.fluidSpacing);
    HashFloat(h, static_cast<float>(node.fluidStart));
    return h;
}

void VulkanFluidSim::Init() {
    VkDevice device = VulkanContext::Get().GetDevice();
    VkDescriptorSetLayoutBinding bindings[12]{};
    for (uint32_t b = 0; b < 12; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 12;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid simulation descriptor set layout!");
    }
    VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FluidSimPushConstants) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid simulation pipeline layout!");
    }
    const std::vector<char> code = ReadSpirv("shaders/fluid_sim_comp.spv");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid simulation shader module!");
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
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create fluid simulation pipeline!");
}

void VulkanFluidSim::Destroy(Volume& volume) {
    VkDevice device = VulkanContext::Get().GetDevice();
    for (Buffer& b : volume.buffers) {
        if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(device, b.memory, nullptr);
        b = Buffer{};
    }
    for (int f = 0; f < 2; ++f) {
        for (auto [buffer, mapped] : { std::pair<Buffer*, void**>{ &volume.frameData[f], &volume.frameDataMapped[f] },
                                       std::pair<Buffer*, void**>{ &volume.feedback[f], &volume.feedbackMapped[f] } }) {
            if (buffer->memory && *mapped) vkUnmapMemory(device, buffer->memory);
            if (buffer->buffer) vkDestroyBuffer(device, buffer->buffer, nullptr);
            if (buffer->memory) vkFreeMemory(device, buffer->memory, nullptr);
            *buffer = Buffer{};
            *mapped = nullptr;
        }
        volume.records[f] = FrameRecord{};
    }
    if (volume.pool) vkDestroyDescriptorPool(device, volume.pool, nullptr);
    volume.pool = VK_NULL_HANDLE;
    volume.count = 0;
}

void VulkanFluidSim::Cleanup() {
    VkDevice device = VulkanContext::Get().GetDevice();
    vkDeviceWaitIdle(device);
    for (auto& entry : m_volumes) Destroy(entry.second);
    m_volumes.clear();
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
}

void VulkanFluidSim::Reset(Volume& volume, const Node& node, const Scene& scene) {
    auto& ctx = VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    vkDeviceWaitIdle(device); // the old buffers may still be in use by frames in flight
    Destroy(volume);
    volume.settingsKey = KeyFor(node);
    volume.accumulator = 0.0f;
    volume.spawnCarry = 0.0f;

    // The starting block: a layer on the floor, or a column against one side (a dam break)
    const glm::vec3 center = node.GetWorldPosition(scene);
    const glm::vec3 size = glm::max(node.fluidSize, glm::vec3(0.05f));
    const glm::vec3 boxMin = center - size * 0.5f;
    float spacing = std::max(node.fluidSpacing, 0.003f);
    const float fill = glm::clamp(node.fluidFill, 0.01f, 0.95f);
    glm::vec3 blockMax = center + size * 0.5f;
    if (node.fluidStart == 1) {
        blockMax.y = boxMin.y + size.y * 0.8f;
        blockMax.x = boxMin.x + std::min(size.x * fill / 0.8f, size.x);
    } else {
        blockMax.y = boxMin.y + size.y * fill;
    }
    const glm::vec3 block = blockMax - boxMin;
    // Coarser spacing if the block (and room to pour more in), or the neighbour grid, would be too big
    for (;;) {
        const double particles = static_cast<double>(block.x / spacing) * (block.y / spacing) * (block.z / spacing);
        const glm::vec3 cells = glm::ceil(size / (spacing * 2.0f)) + 1.0f;
        if (particles * 1.25 <= kMaxParticles && static_cast<double>(cells.x) * cells.y * cells.z < kMaxCells) break;
        spacing *= 1.1f;
    }
    volume.spacing = spacing;
    volume.h = spacing * 2.0f;
    volume.gridDims = glm::max(glm::ivec3(glm::ceil(size / volume.h)) + 1, glm::ivec3(1));
    const uint32_t cells = static_cast<uint32_t>(volume.gridDims.x * volume.gridDims.y * volume.gridDims.z);

    std::vector<glm::vec4> positions;
    uint32_t seed = 1234u;
    auto jitter = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * spacing * 0.02f;
    };
    const float drawRadius = spacing * 0.75f;
    for (float y = boxMin.y + spacing * 0.5f; y < blockMax.y; y += spacing) {
        for (float z = boxMin.z + spacing * 0.5f; z < blockMax.z; z += spacing) {
            for (float x = boxMin.x + spacing * 0.5f; x < blockMax.x && positions.size() < kMaxParticles; x += spacing) {
                positions.push_back(glm::vec4(x + jitter(), y + jitter(), z + jitter(), drawRadius));
            }
        }
    }
    // Free slots for fluid poured in later: a quarter as many again (at least a few thousand),
    // within what the box can hold
    const double boxCapacity = static_cast<double>(size.x / spacing) * (size.y / spacing) * (size.z / spacing);
    const size_t reserve = static_cast<size_t>(std::clamp(boxCapacity - static_cast<double>(positions.size()), 0.0,
                                                          std::max(static_cast<double>(positions.size()) * 0.25, 4096.0)));
    const size_t slots = std::min(positions.size() + reserve, static_cast<size_t>(kMaxParticles));
    positions.resize(slots, glm::vec4(boxMin, 0.0f));
    volume.count = static_cast<uint32_t>(positions.size());
    if (volume.count == 0) return;

    // Density of a particle in the starting lattice: the rest density
    float restDensity = 0.0f;
    const int reach = static_cast<int>(std::ceil(volume.h / spacing));
    for (int i = -reach; i <= reach; ++i) {
        for (int j = -reach; j <= reach; ++j) {
            for (int k = -reach; k <= reach; ++k) {
                const glm::vec3 r = glm::vec3(i, j, k) * spacing;
                restDensity += Poly6(glm::dot(r, r), volume.h);
            }
        }
    }
    volume.invRestDensity = 1.0f / std::max(restDensity, 1e-6f);

    // GPU buffers (the cell counts and starts have one more entry: the free slots' cell)
    const VkDeviceSize vec4Bytes = sizeof(glm::vec4) * volume.count;
    const VkDeviceSize uintBytes = sizeof(uint32_t) * volume.count;
    const VkDeviceSize sizes[10] = { vec4Bytes, vec4Bytes, vec4Bytes, vec4Bytes, uintBytes, uintBytes, uintBytes, uintBytes,
                                     sizeof(uint32_t) * (cells + 1), sizeof(uint32_t) * (cells + 1) };
    for (int b = 0; b < 10; ++b) {
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (b == 0 || b == 2) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; // drawn directly (spheres, foam)
        VulkanBufferUtils::CreateBuffer(sizes[b], usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, volume.buffers[b].buffer, volume.buffers[b].memory);
    }
    for (int f = 0; f < 2; ++f) {
        VulkanBufferUtils::CreateBuffer(sizeof(GpuFrameData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        volume.frameData[f].buffer, volume.frameData[f].memory);
        vkMapMemory(device, volume.frameData[f].memory, 0, VK_WHOLE_SIZE, 0, &volume.frameDataMapped[f]);
        std::memset(volume.frameDataMapped[f], 0, sizeof(GpuFrameData));
        VulkanBufferUtils::CreateBuffer(sizeof(GpuFeedback), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        volume.feedback[f].buffer, volume.feedback[f].memory);
        vkMapMemory(device, volume.feedback[f].memory, 0, VK_WHOLE_SIZE, 0, &volume.feedbackMapped[f]);
        std::memset(volume.feedbackMapped[f], 0, sizeof(GpuFeedback));
    }

    // Starting positions (current and predicted), zero velocities
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VulkanBufferUtils::CreateBuffer(vec4Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, vec4Bytes, 0, &mapped);
    std::memcpy(mapped, positions.data(), static_cast<size_t>(vec4Bytes));
    vkUnmapMemory(device, stagingMemory);
    VkCommandBuffer upload = ctx.BeginSingleTimeCommands();
    VkBufferCopy region{ 0, 0, vec4Bytes };
    vkCmdCopyBuffer(upload, staging, volume.buffers[0].buffer, 1, &region);
    vkCmdCopyBuffer(upload, staging, volume.buffers[1].buffer, 1, &region);
    vkCmdFillBuffer(upload, volume.buffers[2].buffer, 0, VK_WHOLE_SIZE, 0);
    ctx.EndSingleTimeCommands(upload);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Descriptor sets: one per frame in flight (they differ in the frame data and feedback)
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 24 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &volume.pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fluid descriptor pool!");
    }
    const VkDescriptorSetLayout layouts[2] = { m_setLayout, m_setLayout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = volume.pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &allocInfo, volume.sets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate fluid descriptor sets!");
    }
    for (int f = 0; f < 2; ++f) {
        VkDescriptorBufferInfo infos[12]{};
        VkWriteDescriptorSet writes[12]{};
        for (uint32_t b = 0; b < 12; ++b) {
            const VkBuffer buffer = b < 10 ? volume.buffers[b].buffer : (b == 10 ? volume.frameData[f].buffer : volume.feedback[f].buffer);
            infos[b] = { buffer, 0, VK_WHOLE_SIZE };
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = volume.sets[f];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(device, 12, writes, 0, nullptr);
    }
}

// What the simulation reported for this frame in flight (its fence has been waited on): the
// fluid around solid objects, and how much it poured into Liquid containers
void VulkanFluidSim::ReadFeedback(Volume& volume, Scene& scene, uint32_t frameIndex, bool transfer, std::vector<Contact>& contacts) {
    FrameRecord& record = volume.records[frameIndex];
    GpuFeedback* feedback = static_cast<GpuFeedback*>(volume.feedbackMapped[frameIndex]);
    if (!feedback) return;
    if (record.valid) {
        const float particleVolume = volume.spacing * volume.spacing * volume.spacing;
        volume.contacts.clear();
        for (uint32_t k = 0; k < record.obstacleCount; ++k) {
            const int32_t* c = &feedback->contacts[k * 8];
            if (c[4] <= 0) continue;
            const float waterline = (c[0] - 1000000) * 0.001f + volume.spacing * 0.5f;
            const glm::vec3 velocity = glm::vec3(c[1], c[2], c[3]) * (0.001f / static_cast<float>(c[4]));
            volume.contacts.push_back({ record.obstacleIds[k], waterline, velocity });
        }
        if (transfer) {
            for (uint32_t s = 0; s < record.sinkCount; ++s) {
                const uint32_t absorbed = std::min(feedback->sinkCounts[s], record.sinkCapacity[s]);
                if (absorbed == 0) continue;
                auto liquid = scene.GetNodeById(record.sinkIds[s]);
                if (!liquid) continue;
                const float added = absorbed * particleVolume / std::max(Liquids::ContainerVolume(*liquid, scene), 1e-9f);
                liquid->liquidFill = std::min(liquid->liquidFill + added, 1.0f);
            }
        }
    }
    std::memset(feedback, 0, sizeof(GpuFeedback));
    record.valid = false;
    contacts.insert(contacts.end(), volume.contacts.begin(), volume.contacts.end());
}

void VulkanFluidSim::Step(VkCommandBuffer cmd, Volume& volume, const Node& node, const Scene& scene, uint32_t frameIndex, float dt,
                          const std::vector<Feed>& feeds, bool transfer) {
    const float substep = kSubstep * glm::clamp(volume.spacing / kSubstepSpacing, 0.25f, 1.0f);
    volume.accumulator = std::min(volume.accumulator + std::max(dt, 0.0f), kMaxStepTime);
    const int substeps = std::min(static_cast<int>(volume.accumulator / substep), kMaxSubsteps);
    if (substeps == 0 || volume.count == 0) return;
    volume.accumulator -= substeps * substep;

    // The box follows the node
    const glm::vec3 center = node.GetWorldPosition(scene);
    const glm::vec3 size = glm::max(node.fluidSize, glm::vec3(0.05f));
    const glm::vec3 boxMin = center - size * 0.5f;
    const glm::vec3 boxMax = center + size * 0.5f;

    GpuFrameData* frame = static_cast<GpuFrameData*>(volume.frameDataMapped[frameIndex]);
    FrameRecord& record = volume.records[frameIndex];
    record = FrameRecord{};
    record.substep = substep;

    // Liquid containers in the box take in the fluid that reaches them (sinks); the glasses
    // around them are open, not solid
    std::vector<glm::vec3> liquidCentres;
    for (const auto& other : scene.GetNodes()) {
        if (!other || !other->isLiquid || other->nodeType != NodeType::Mesh) continue;
        const glm::mat4 sinkFrame = UnitBoxFrame(*other, scene);
        liquidCentres.push_back(glm::vec3(sinkFrame[3]));
        if (record.sinkCount >= kMaxSinks || std::abs(glm::determinant(glm::mat3(sinkFrame))) < 1e-12f) continue;
        if (!OverlapsBox(sinkFrame, boxMin, boxMax)) continue;
        // It takes particles until full (none while the fluid can't fill it: in the editor)
        const float particleVolume = volume.spacing * volume.spacing * volume.spacing;
        const float room = transfer ? std::max(1.0f - other->liquidFill, 0.0f) * Liquids::ContainerVolume(*other, scene) / particleVolume : 0.0f;
        record.sinkCapacity[record.sinkCount] = static_cast<uint32_t>(room);
        frame->sinks[record.sinkCount] = { glm::inverse(sinkFrame), glm::vec4(other->type == ShapeType::Cylinder ? 1.0f : 0.0f, std::floor(room), 0, 0) };
        record.sinkIds[record.sinkCount++] = other->id;
    }

    // Solid objects in the box push the fluid around (not ones the whole box sits inside, such
    // as a tank model around it, nor containers holding a Liquid)
    glm::vec3 boxCorners[8];
    for (int c = 0; c < 8; ++c) {
        boxCorners[c] = glm::vec3((c & 1) ? boxMax.x : boxMin.x, (c & 2) ? boxMax.y : boxMin.y, (c & 4) ? boxMax.z : boxMin.z);
    }
    for (const auto& other : scene.GetNodes()) {
        if (record.obstacleCount >= kMaxObstacles) break;
        if (!other || other->nodeType != NodeType::Mesh || other->isLiquid || other->isOcean || other->IsUINode()) continue;
        if (other->type == ShapeType::Plane || other->type == ShapeType::Quad) continue;
        const glm::mat4 obstacleFrame = UnitBoxFrame(*other, scene);
        if (std::abs(glm::determinant(glm::mat3(obstacleFrame))) < 1e-12f) continue;
        const glm::mat4 toLocal = glm::inverse(obstacleFrame);
        auto insideUnit = [&](const glm::vec3& p, float limit) {
            return glm::all(glm::lessThan(glm::abs(glm::vec3(toLocal * glm::vec4(p, 1.0f))), glm::vec3(limit)));
        };
        // A tank: it encloses the whole box (give or take a little)
        if (std::all_of(std::begin(boxCorners), std::end(boxCorners), [&](const glm::vec3& p) { return insideUnit(p, 0.56f); })) continue;
        if (std::any_of(liquidCentres.begin(), liquidCentres.end(), [&](const glm::vec3& p) { return insideUnit(p, 0.5f); })) continue;
        if (!OverlapsBox(obstacleFrame, boxMin, boxMax)) continue;
        // Where the fluid sees it at each substep: easing toward where the node is now
        auto smoothed = volume.obstacleFrames.find(other->id);
        if (smoothed == volume.obstacleFrames.end()) smoothed = volume.obstacleFrames.emplace(other->id, obstacleFrame).first;
        const float ease = 1.0f - std::exp(-substep / kObstacleSmoothing);
        const glm::vec4 info(other->type == ShapeType::Sphere ? 1.0f : 0.0f, 0, 0, 0);
        for (int s = 0; s < substeps; ++s) {
            glm::mat4& eased = smoothed->second;
            for (int c = 0; c < 4; ++c) eased[c] = glm::mix(eased[c], obstacleFrame[c], ease);
            const glm::mat4 easedToLocal = std::abs(glm::determinant(glm::mat3(eased))) > 1e-12f ? glm::inverse(eased) : toLocal;
            frame->obstacles[s * kMaxObstacles + record.obstacleCount] = { easedToLocal, eased, info };
        }
        record.obstacleIds[record.obstacleCount++] = other->id;
    }

    // Pour streams landing in this volume: particles enough for the poured volume, spread over
    // the substeps
    const float particleVolume = volume.spacing * volume.spacing * volume.spacing;
    glm::vec3 spawnPoint(0.0f), spawnVelocity(0.0f);
    float pouredVolume = 0.0f;
    for (const Feed& feed : feeds) {
        if (feed.fluidNodeId != node.id) continue;
        const float v = std::max(feed.volumePerSecond, 0.0f) * substeps * substep;
        spawnPoint += feed.position * v;
        spawnVelocity += feed.velocity * v;
        pouredVolume += v;
    }
    uint32_t spawnPerSubstep = 0;
    if (pouredVolume > 0.0f) {
        spawnPoint /= pouredVolume;
        spawnVelocity /= pouredVolume;
        volume.spawnCarry += pouredVolume / particleVolume;
        spawnPerSubstep = static_cast<uint32_t>(volume.spawnCarry / substeps);
        volume.spawnCarry -= static_cast<float>(spawnPerSubstep) * substeps;
    }
    frame->spawnPoint = glm::vec4(spawnPoint, static_cast<float>(spawnPerSubstep));
    frame->spawnVelocity = glm::vec4(spawnVelocity, volume.spacing * 2.0f);
    frame->counts = glm::uvec4(record.sinkCount, 0, 0, 0);
    record.valid = true;

    const uint32_t cells = static_cast<uint32_t>(volume.gridDims.x * volume.gridDims.y * volume.gridDims.z);
    FluidSimPushConstants push{};
    push.boxMin = glm::vec4(boxMin, volume.spacing * 0.5f);
    push.boxMax = glm::vec4(boxMax, volume.h);
    push.grid = glm::ivec4(volume.gridDims, static_cast<int>(cells));
    push.count = volume.count;
    push.obstacleCount = record.obstacleCount;
    push.dt = substep;
    push.invRestDensity = volume.invRestDensity;
    push.viscosity = glm::clamp(node.fluidViscosity, 0.0f, 1.0f);
    push.drawRadius = volume.spacing * 0.75f;
    push.relaxation = 1.0f / (volume.h * volume.h);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &volume.sets[frameIndex], 0, nullptr);
    // Earlier frames may still be drawing the particles
    VkMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &before, 0, nullptr, 0, nullptr);

    const uint32_t particleGroups = (volume.count + kWorkgroupSize - 1) / kWorkgroupSize;
    const uint32_t cellGroups = (cells + 1 + kWorkgroupSize - 1) / kWorkgroupSize;
    auto run = [&](uint32_t stage, uint32_t groups) {
        push.stage = stage;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, groups, 1, 1);
        ComputeBarrier(cmd);
    };
    for (int s = 0; s < substeps; ++s) {
        push.substep = static_cast<uint32_t>(s);
        run(kClearCells, cellGroups);
        if (spawnPerSubstep > 0) run(kSpawn, particleGroups);
        run(kPredict, particleGroups);
        run(kScan, 1);
        run(kScatter, particleGroups);
        for (int iteration = 0; iteration < kSolverIterations; ++iteration) {
            run(kLambda, particleGroups);
            run(kDelta, particleGroups);
            run(kApply, particleGroups);
        }
        run(kVelocity, particleGroups);
        run(kViscosity, particleGroups);
        run(kFinish, particleGroups);
    }

    // The positions are drawn next, as the fluid's spheres and foam; the feedback is read on the CPU
    VkMemoryBarrier after{};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &after, 0, nullptr, 0, nullptr);
}

std::vector<VulkanFluidSim::Contact> VulkanFluidSim::Update(VkCommandBuffer cmd, Scene& scene, float dt, uint32_t frameIndex, bool transfer,
                                                            const std::vector<Feed>& feeds) {
    std::vector<Contact> contacts;
    frameIndex %= 2;
    if (m_scene != &scene) {
        if (!m_volumes.empty()) {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            for (auto& entry : m_volumes) Destroy(entry.second);
            m_volumes.clear();
        }
        m_scene = &scene;
    }
    std::vector<uint32_t> alive;
    for (const auto& node : scene.GetNodes()) {
        if (!node || !node->isFluidVolume) continue;
        alive.push_back(node->id);
        Volume& volume = m_volumes[node->id];
        if (volume.count == 0 || volume.settingsKey != KeyFor(*node) || node->fluidResetRequested) {
            Reset(volume, *node, scene);
            node->fluidResetRequested = false;
        }
        ReadFeedback(volume, scene, frameIndex, transfer, contacts);
        Step(cmd, volume, *node, scene, frameIndex, dt, feeds, transfer);
    }
    for (auto it = m_volumes.begin(); it != m_volumes.end();) {
        if (std::find(alive.begin(), alive.end(), it->first) == alive.end()) {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            Destroy(it->second);
            it = m_volumes.erase(it);
        } else {
            ++it;
        }
    }
    return contacts;
}

std::vector<VulkanFluidSim::Body> VulkanFluidSim::Bodies(const Scene& scene) const {
    std::vector<Body> bodies;
    for (const auto& [id, volume] : m_volumes) {
        auto node = scene.GetNodeById(id);
        if (!node || volume.count == 0 || &scene != m_scene) continue;
        bodies.push_back({ volume.buffers[0].buffer, volume.buffers[2].buffer, volume.count, node->fluidColor,
                           std::max(node->fluidColorDepth, 1e-3f), volume.spacing * 0.75f });
    }
    return bodies;
}
