#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "particle_common.glsl"

// Particles drawn as small meshes (spheres, cubes): one instance per particle, read from the
// particle's sprite quad (ParticleQuad: a = position and half size, b.w = rotation, colorA)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inA;
layout(location = 3) in vec4 inB;
layout(location = 4) in vec4 inColorA;
layout(location = 5) in vec4 inColorB;
layout(location = 6) in vec4 inParams;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec3 outWorldPos;

void main() {
    float size = inA.w * 2.0; // the sprite's half size: the mesh is a unit shape
    float c = cos(inB.w), s = sin(inB.w);
    // Spun about the vertical and a tilted axis, so tumbling cubes look it
    mat3 spin = mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c) * mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c);
    vec3 world = inA.xyz + spin * inPosition * size;
    outNormal = spin * inNormal;
    outColor = inColorA;
    outWorldPos = world;
    // Dead slots (size 0) collapse to a point
    gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
}
