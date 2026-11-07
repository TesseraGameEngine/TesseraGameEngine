#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"

// World-space view ray for this corner; normalized per pixel in sky.frag
layout(location = 0) out vec3 outViewDir;

void main() {
    // One triangle covering the whole screen, generated from the vertex index
    vec2 ndc = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    // Built from the projection's x/y terms only, so it stays correct when a mirror view
    // replaces the depth row with an oblique near plane
    vec3 viewRay = vec3(ndc.x / ubo.proj[0][0], ndc.y / ubo.proj[1][1], -1.0);
    outViewDir = inverse(mat3(ubo.view)) * viewRay;
}
