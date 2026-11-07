#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "common_material.glsl"

// Editor overlay lines (grid, axes), drawn after post-processing with the unjittered camera so
// they stay crisp and still under TAA
layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = ubo.unjitteredViewProj * pc.model * vec4(inPosition, 1.0);
    // Nudge toward the camera so lines on a floor at y = 0 win the depth test instead of flickering
    gl_Position.z -= 0.0002 * gl_Position.w;
}
