#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "fluid_common.glsl"

// One instance per particle: xyz = position, w = radius (spray < 0 and unused slots 0 are left
// out of the surface; the foam pass draws spray)
layout(location = 0) in vec4 inParticle;

layout(location = 0) out vec2 outCorner;      // -1..1 across the sphere's outline
layout(location = 1) out vec3 outCenterView;  // sphere centre in view space
layout(location = 2) out float outRadius;

void main() {
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                   vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = corners[gl_VertexIndex];
    if (inParticle.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, -1.0, 1.0); // outside the depth range: dropped
        outCorner = vec2(0.0);
        outCenterView = vec3(0.0);
        outRadius = 0.0;
        return;
    }
    vec3 centerView = (ubo.view * vec4(inParticle.xyz, 1.0)).xyz;
    outCorner = corner;
    outCenterView = centerView;
    outRadius = inParticle.w;
    gl_Position = ubo.proj * vec4(centerView + vec3(corner * inParticle.w, 0.0), 1.0);
}
