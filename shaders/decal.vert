#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "common_material.glsl"

// Projected decal (VulkanPipeline::DrawDecals): the decal's box (a unit cube scaled by pc.model)
// is drawn to find the pixels it may cover; decal.frag reconstructs the surface under each one
// from the prepass depth and projects the decal onto it.
layout(location = 0) in vec3 inPosition;

layout(location = 0) flat out mat4 outInvViewProj; // clip -> world, for rebuilding the surface position
layout(location = 4) flat out mat4 outToDecal;     // world -> the decal's unit box

void main() {
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
    outInvViewProj = inverse(ubo.proj * ubo.view);
    outToDecal = inverse(pc.model);
}
