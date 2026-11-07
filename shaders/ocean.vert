#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "ocean_common.glsl"

// Radial grid around the camera (VulkanPipeline::CreateOceanGrid): xz = offset from the camera
// (metres), y = the grid's spacing there (metres)
layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outRestPos; // where the water would be without waves (x, z)

void main() {
    // The grid follows the camera and stops at the ocean's edges. Waves move the water sideways
    // too, so the grid is centred where the water that ends up under the camera comes from
    // (otherwise the displaced surface can leave a gap right under a camera near the water)
    vec2 center = ubo.cameraPos.xz;
    for (int i = 0; i < 3; ++i) center = ubo.cameraPos.xz - OceanDisplacement(center, 0.05).xz;
    vec2 restPos = clamp(center + inPosition.xz, ocean.bounds.xy, ocean.bounds.zw);
    // Waves shorter than a few grid cells can't be shown by the mesh; the shading still has them
    vec3 offset = OceanDisplacement(restPos, inPosition.y);
    vec3 world = vec3(restPos.x, ocean.surface.x, restPos.y) + offset;
    outWorldPos = world;
    outRestPos = restPos;
    gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
}
