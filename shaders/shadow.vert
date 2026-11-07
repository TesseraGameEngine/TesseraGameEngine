#version 450
#extension GL_GOOGLE_include_directive : require

// Depth-only pass that renders the scene from the shadow-casting light, one cascade at a time.
#include "common_ubo.glsl"
#include "shadow_common.glsl"
#include "instancing_common.glsl"
#include "wind_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;

void main() {
    if (pc.instanced > 0.5) {
        mat4 model = instances[gl_InstanceIndex].model;
        vec4 worldPos = model * vec4(inPosition, 1.0);
        if (pc.wind > 0.5) worldPos.xyz = WindSway(worldPos.xyz, model[3].xyz, inPosition.y);
        gl_Position = pc.lightModelViewProj * worldPos;
    } else {
        gl_Position = pc.lightModelViewProj * vec4(inPosition, 1.0);
    }
    outTexCoord = inTexCoord;
}
