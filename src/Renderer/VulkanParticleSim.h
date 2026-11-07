#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <vector>

class Scene;
class ParticleNode;

// Particle Emitters in GPU mode (ParticleSimulationMode::GPU_Niagara): the particles live on the
// GPU and shaders/particle_sim.comp moves them, with the same forces, collision (scene shapes,
// terrain and water, sent from ParticleColliders), flipbooks, trails and ribbons as the CPU
// emitters. It writes the quads that VulkanPipeline::DrawParticles draws, so they never come back
// to the CPU; only the number alive and the hits do (a frame or two late).
class VulkanParticleSim {
public:
    static constexpr uint32_t kMaxQuads = 2u * 1024u * 1024u; // per emitter (particles x (1 + trail points))

    void Init();
    void Cleanup();

    // Records this frame's steps for every GPU emitter in the scene (outside render passes)
    void Update(VkCommandBuffer cmd, Scene& scene, uint32_t frameIndex);

    struct Draw {
        VkBuffer quads;
        uint32_t count;   // all quads
        uint32_t sprites; // the first ones: one per particle
    };
    // Each GPU emitter's quads, by node id
    std::map<uint32_t, Draw> Draws() const;

private:
    struct Buffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        void* mapped{nullptr};
        VkDeviceSize size{0};
    };
    struct Emitter {
        uint32_t slots{0};
        uint32_t trailPoints{0}; // per particle (per-particle trails)
        uint32_t quadStride{1};  // quads per particle
        Buffer particles, trails, quads, state;
        Buffer frameData[2], heights[2], feedback[2];
        std::vector<uint64_t> heightVersions[2]; // the terrains each frame's heights hold
        bool feedbackValid[2]{};
        VkDescriptorPool pool{VK_NULL_HANDLE};
        VkDescriptorSet sets[2]{};
        float clock{0.0f};
        uint32_t frameSeed{1u};
        bool clear{true};
        bool seen{false};
    };
    void Create(Emitter& emitter, uint32_t slots, uint32_t trailPoints, uint32_t quadStride);
    void Destroy(Emitter& emitter);
    void WriteSets(Emitter& emitter, int frame);
    void Step(VkCommandBuffer cmd, Emitter& emitter, ParticleNode& node, Scene& scene, uint32_t frameIndex);

    std::map<uint32_t, Emitter> m_emitters; // by node id
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
};
