#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "fluid_common.glsl"

layout(binding = 24) uniform sampler2D sceneDepthMap; // opaque scene depth (this view's prepass)
layout(binding = 26) uniform sampler2D fluidSurface;  // smoothed fluid surface distance (0 = none)

layout(location = 0) in vec2 inCorner;
layout(location = 1) in float inFoam;
layout(location = 2) in float inDistance;
layout(location = 3) in float inSpray;
layout(location = 0) out vec4 outColor;

// Foam lies on the surface (sunk below it, it fades out); spray is a white droplet in the air.
// Both are hidden behind the opaque scene, lit by the sky and the sun.
void main() {
    float r2 = dot(inCorner, inCorner);
    if (r2 > 1.0) discard;
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneDepthMap, 0));
    float sceneDistance = FluidViewDistance(texture(sceneDepthMap, uv).r);
    if (inDistance > sceneDistance + 0.01) discard;
    float surface = texelFetch(fluidSurface, ivec2(gl_FragCoord.xy), 0).r;
    float sunk = (surface > 0.0 && inSpray < 0.5) ? smoothstep(0.02, 0.10, inDistance - surface) : 0.0;
    float alpha = (1.0 - r2) * (inSpray > 0.5 ? 0.8 : inFoam * 0.45) * (1.0 - sunk);
    if (alpha <= 0.003) discard;

    vec3 skySunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 light = (skyOrHdri ? EnvIrradiance(vec3(0.0, 1.0, 0.0), skySunDir) : ubo.giSkyColor.rgb) * giStrength;
    uint directional = lightClusters.lightCounts.y;
    for (uint k = 0u; k < directional; ++k) {
        uint i = lightClusters.directionalLights[k];
        vec3 L = normalize(lightList.lights[i].position.xyz);
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        if (ubo.skyParams.w > 0.0 && dot(L, skySunDir) > 0.9999) radiance *= SkySunTransmittance(L);
        light += radiance * (0.5 + 0.5 * max(L.y, 0.0)) / 3.14159;
    }
    vec3 color = vec3(0.92, 0.95, 0.97) * light;
    outColor = vec4(color * alpha, alpha);
}
