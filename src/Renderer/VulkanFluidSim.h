#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <vector>

class Scene;
struct Node;

// Real fluid on the GPU: every Fluid Volume node simulated with Position Based Fluids (Macklin &
// Mueller 2013) in compute shaders (shaders/fluid_sim.comp). The particles never leave the GPU:
// the positions buffer is drawn directly as the fluid's spheres (VulkanPipeline::DrawFluid*).
// The CPU only lays out the starting particles and passes the box, obstacles and step size.
//
// Two-way coupling: around each solid object the fluid reports its waterline and flow (Contact:
// dynamic rigid bodies float and drift in it), Liquid containers it flows into absorb it until
// full, and pour streams landing in the box pour fluid in (Feed).
class VulkanFluidSim {
public:
    static constexpr uint32_t kMaxParticles = 262144; // per volume, free slots included
    static constexpr uint32_t kMaxObstacles = 64;
    static constexpr uint32_t kMaxSinks = 16;

    void Init();
    void Cleanup();

    // A pour stream entering a fluid volume (Liquids::Simulation::FluidFeeds)
    struct Feed {
        uint32_t fluidNodeId;
        glm::vec3 position;
        glm::vec3 velocity;
        float volumePerSecond; // m^3/s
    };
    // The fluid around a solid node: its waterline (highest fluid beside it, m) and the fluid's
    // average velocity there
    struct Contact {
        uint32_t nodeId;
        float waterline;
        glm::vec3 fluidVelocity;
    };

    // Records this frame's simulation steps for every fluid volume in the scene (call outside
    // render passes, before the fluid is drawn). Volumes restart when their settings change or
    // on Reset Fluid. frameIndex: the frame in flight (per-frame buffers). transfer: let the fluid
    // fill Liquid nodes (Play). Returns the fluid around solid nodes, measured two frames ago.
    std::vector<Contact> Update(VkCommandBuffer cmd, Scene& scene, float dt, uint32_t frameIndex, bool transfer,
                                const std::vector<Feed>& feeds);

    struct Body {
        VkBuffer positions;  // vec4 per particle: xyz, w = radius (< 0 spray, 0 free slot)
        VkBuffer velocities; // vec4 per particle: xyz, w = foam
        uint32_t count;
        glm::vec4 color;
        float colorDepth;
        float radius;
    };
    std::vector<Body> Bodies(const Scene& scene) const;

private:
    struct Buffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
    };
    // What one frame in flight sent, to read its feedback back
    struct FrameRecord {
        bool valid{false};
        uint32_t obstacleIds[kMaxObstacles]{};
        uint32_t obstacleCount{0};
        uint32_t sinkIds[kMaxSinks]{};
        uint32_t sinkCapacity[kMaxSinks]{}; // particles each could still take
        uint32_t sinkCount{0};
        float substep{1.0f / 120.0f};
    };
    struct Volume {
        uint64_t settingsKey{0};
        uint32_t count{0};    // particle slots (the starting fluid plus free slots for pouring in)
        float spacing{0.04f};
        float h{0.08f};
        float invRestDensity{1.0f};
        glm::ivec3 gridDims{1};
        float accumulator{0.0f};
        float spawnCarry{0.0f}; // poured-in particles not spawned yet
        // positions, predicted, velocities, scratch, lambdas, cellOf, localIndex, sorted, cellCount, cellStart
        Buffer buffers[10];
        Buffer frameData[2];  // host-visible, per frame in flight: obstacles, sinks, spawning
        void* frameDataMapped[2]{};
        Buffer feedback[2];   // host-visible, per frame in flight: forces, absorbed particles
        void* feedbackMapped[2]{};
        FrameRecord records[2];
        // The fluid around solid objects as last measured: reported every frame, also the ones
        // too short for a simulation step, so bodies are held up all the time
        std::vector<Contact> contacts;
        std::map<uint32_t, glm::mat4> obstacleFrames; // each obstacle as the fluid sees it (eased)
        VkDescriptorPool pool{VK_NULL_HANDLE};
        VkDescriptorSet sets[2]{};
    };
    void Reset(Volume& volume, const Node& node, const Scene& scene);
    void Destroy(Volume& volume);
    void ReadFeedback(Volume& volume, Scene& scene, uint32_t frameIndex, bool transfer, std::vector<Contact>& contacts);
    void Step(VkCommandBuffer cmd, Volume& volume, const Node& node, const Scene& scene, uint32_t frameIndex, float dt,
              const std::vector<Feed>& feeds, bool transfer);
    static uint64_t KeyFor(const Node& node);

    std::map<uint32_t, Volume> m_volumes; // by node id
    const Scene* m_scene{nullptr};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
};
