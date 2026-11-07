// Screen-space fluid rendering (VulkanPipeline::DrawFluid*, VulkanFluidTargets). Must match
// FluidPushConstants in VulkanPipeline.cpp (the same 128-byte range as PushConstantData).
#ifndef FLUID_COMMON_GLSL
#define FLUID_COMMON_GLSL

layout(push_constant) uniform FluidParams {
    vec4 color;  // rgb = the fluid's colour
    vec4 params; // x = 1 in the thickness pass, y = colour depth (m), z = particle radius (m)
    vec4 reserved[6];
} fluid;

// Distance along the view axis for a depth-buffer value (any perspective projection)
float FluidViewDistance(float depth) {
    return ubo.proj[3][2] / (depth + ubo.proj[2][2]);
}

#endif
