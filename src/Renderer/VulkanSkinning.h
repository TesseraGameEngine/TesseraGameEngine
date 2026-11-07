#pragma once

#include "VulkanBuffer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// GPU skinning. Each animated character (keyed by its Node) gets its own deformed vertex buffer,
// written every frame by a compute shader (skinning.comp) from its mesh's bind pose and its bone
// matrices. Draws then use that buffer instead of the shared mesh, so characters that share a
// model can hold different poses, and every pass (shadows, prepass, mirrors, probes) sees the
// same deformation.
//
// Per frame: BeginFrame() after the frame's fence wait, Submit() each character, then Record()
// into the command buffer before any pass that draws them.
class VulkanSkinning {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxBoneMatrices = 16384; // per frame, across all characters

    // Must match SourceVertex in skinning.comp (std430, 80 bytes)
    struct SourceVertex {
        glm::vec4 positionU{0.0f};  // xyz = raw bind-pose position (or final position if unweighted), w = u
        glm::vec4 normalV{0.0f};    // xyz = bind-pose normal, w = v
        glm::ivec4 bones{-1};       // bone indices (-1 = unused)
        glm::vec4 weights{0.0f};    // all 0 = not skinned
        glm::vec4 color{1.0f};      // vertex colour, copied through
    };

    void Init();
    void Cleanup();

    // Bind-pose data for a mesh, uploaded once (meshKey = the custom mesh's name)
    bool HasSource(const std::string& meshKey) const { return m_sources.count(meshKey) != 0; }
    void SetSource(const std::string& meshKey, const std::vector<SourceVertex>& vertices);

    // Starts a frame (frameInFlight = 0 .. kFramesInFlight - 1). Call after waiting for that
    // frame's fence: it reuses the frame's bone region and frees buffers no longer drawn.
    void BeginFrame(uint32_t frameInFlight);

    // Queues one character for this frame. indexSource supplies the index buffer (the shared
    // mesh). centerScale = (mesh centre, scale factor); morphs = procedural morph weights.
    void Submit(const void* nodeKey, const std::string& meshKey, const GPUMesh& indexSource,
                const std::vector<glm::mat4>& boneMatrices, const glm::vec4& centerScale, const glm::vec4& morphs);

    // Records this frame's dispatches with the barriers around them. Outside any render pass.
    void Record(VkCommandBuffer cmd);

    // The character's deformed mesh, or nullptr if it has never been skinned
    const GPUMesh* Find(const void* nodeKey);

    // Last frame's deformed vertices (same layout), for per-vertex motion vectors, or nullptr if
    // there is no previous pose this frame (then the current one should be used: no motion)
    VkBuffer FindPrevious(const void* nodeKey) const;

private:
    struct Source {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        uint32_t vertexCount{0};
    };
    struct Instance {
        std::string meshKey;
        GPUMesh mesh;                // own vertex buffer, the shared mesh's index buffer
        VkDeviceMemory outputMemory{VK_NULL_HANDLE};
        VkBuffer previousBuffer{VK_NULL_HANDLE};     // last frame's pose (copied before skinning)
        VkDeviceMemory previousMemory{VK_NULL_HANDLE};
        bool previousValid{false};
        uint64_t lastSkinnedFrame{0};
        VkDescriptorSet set{VK_NULL_HANDLE};
        uint32_t boneCapacity{0};    // bone range the set was written for
        uint64_t lastUsedFrame{0};
    };
    struct Dispatch {
        Instance* instance;
        bool firstPose;           // no previous pose yet: copy this one after skinning
        VkDeviceSize vertexBytes;
        uint32_t boneOffsetBytes;
        uint32_t vertexCount;
        uint32_t boneCount;
        glm::vec4 centerScale;
        glm::vec4 morphs;
    };

    void DestroyInstance(Instance& instance);
    void WriteInstanceSet(Instance& instance, const Source& source);

    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    VkBuffer m_boneBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_boneMemory{VK_NULL_HANDLE};
    char* m_boneMapped{nullptr};
    VkDeviceSize m_boneAlignment{256};
    VkDeviceSize m_boneRegionSize{0};
    VkDeviceSize m_boneCursor{0};

    std::unordered_map<std::string, Source> m_sources;
    std::unordered_map<const void*, Instance> m_instances;
    std::vector<Dispatch> m_dispatches;
    uint32_t m_frameIndex{0};
    uint64_t m_frameNumber{0};
};
