#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "instancing_common.glsl"
#include "wind_common.glsl"

// Depth + normal + motion prepass. Must match PrepassPushConstants in VulkanPipeline.cpp.
// The bottom row of an affine model matrix is always (0, 0, 0, 1), so three of its slots carry
// material values instead: model[0][3] = colour alpha, model[1][3] = alpha cutoff,
// model[2][3] = flags (1 = has albedo map, 2 = double-sided, 4 = instanced: both matrices come
// from the instance buffer and only these bottom-row values are used from the push constants). prevModel's bottom row carries
// prevModel[0][3] = roughness and prevModel[1][3] = metallic (for screen-space reflections).
layout(push_constant) uniform PrepassPushConstants {
    mat4 model;
    mat4 prevModel; // last frame's model matrix (for per-object motion)
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 5) in vec3 inPreviousPosition; // last frame's position (differs for animated characters)

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec4 outCurrentClip;  // unjittered, this frame
layout(location = 3) out vec4 outPreviousClip; // unjittered, last frame
layout(location = 4) flat out vec3 outMaterial; // alpha, cutoff, flags
layout(location = 5) flat out vec2 outSurface;  // roughness, metallic

void main() {
    mat4 model = pc.model;
    outMaterial = vec3(model[0][3], model[1][3], model[2][3]);
    bool instanced = (int(model[2][3] + 0.5) & 4) != 0;
    bool wind = (int(model[2][3] + 0.5) & 8) != 0;
    model[0][3] = 0.0;
    model[1][3] = 0.0;
    model[2][3] = 0.0;
    model[3][3] = 1.0;

    mat4 prevModel = pc.prevModel;
    outSurface = vec2(prevModel[0][3], prevModel[1][3]);
    if (instanced) {
        model = instances[gl_InstanceIndex].model;
        prevModel = instances[gl_InstanceIndex].prevModel;
    }
    prevModel[0][3] = 0.0;
    prevModel[1][3] = 0.0;
    prevModel[2][3] = 0.0;
    prevModel[3][3] = 1.0;

    vec4 worldPos = model * vec4(inPosition, 1.0);
    if (wind) worldPos.xyz = WindSway(worldPos.xyz, model[3].xyz, inPosition.y);
    gl_Position = ubo.proj * ubo.view * worldPos;

    outWorldNormal = transpose(inverse(mat3(model))) * inNormal;
    outTexCoord = inTexCoord;
    outCurrentClip = ubo.unjitteredViewProj * worldPos;
    outPreviousClip = ubo.prevViewProj * (prevModel * vec4(inPreviousPosition, 1.0));
}
