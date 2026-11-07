#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "fluid_common.glsl"

layout(binding = 24) uniform sampler2D sceneDepthMap; // opaque scene depth (this view's prepass)

layout(location = 0) in vec2 inCorner;
layout(location = 1) in vec3 inCenterView;
layout(location = 2) in float inRadius;

// Distance pass: nearest fluid surface (view distance, 0 = none) and the particle's radius, and
// its body's colour and colour depth. Thickness pass: the fluid through this sphere, summed
// (fluid.params.x = 1; the second output has no attachment there)
layout(location = 0) out vec2 outValue;
layout(location = 1) out vec4 outBody;

// Each particle is a sphere: its front surface goes into the distance image (nearest wins
// through the depth test), and the length of fluid through it is added to the thickness
void main() {
    float d2 = dot(inCorner, inCorner);
    if (d2 > 1.0) discard;
    float nz = sqrt(1.0 - d2);
    vec3 surface = inCenterView + vec3(inCorner * inRadius, nz * inRadius);
    float distance = -surface.z;
    // Hidden behind the opaque scene
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneDepthMap, 0));
    float sceneDistance = FluidViewDistance(texture(sceneDepthMap, uv).r);
    if (distance > sceneDistance) discard;
    vec4 clip = ubo.proj * vec4(surface, 1.0);
    gl_FragDepth = clip.z / clip.w;
    outValue = fluid.params.x > 0.5 ? vec2(2.0 * nz * inRadius, 0.0) : vec2(distance, inRadius);
    outBody = vec4(fluid.color.rgb, fluid.params.y);
}
