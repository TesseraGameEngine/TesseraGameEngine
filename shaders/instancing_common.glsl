// Per-instance transforms for instanced draws (VulkanPipeline groups identical draws). Must match
// InstanceData in src/Renderer/VulkanPipeline.h. gl_InstanceIndex (which includes the draw's
// firstInstance) indexes this frame's region of the buffer.
#ifndef INSTANCING_COMMON_GLSL
#define INSTANCING_COMMON_GLSL

struct InstanceData {
    mat4 model;
    mat4 prevModel; // last frame's model matrix (motion vectors)
};

layout(std430, binding = 14) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

#endif
