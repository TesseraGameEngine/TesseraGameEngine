#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "water_common.glsl"
#include "particle_common.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap;
#include "sun_shadow_common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

// Mesh particles: lit like a rough surface (sky, sun with its shadow, lights), plus the
// emitter's self glow (particle.look.y), faded by the particle's alpha (premultiplied)
void main() {
    if (inColor.a <= 0.002) discard;
    vec3 N = normalize(inNormal);
    vec3 sunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 light = (skyOrHdri ? EnvIrradiance(N, sunDir) : ubo.giSkyColor.rgb) * giStrength;
    uvec2 cluster = lightClusters.clusters[ClusterIndex(inWorldPos)];
    int lightTotal = LightCountAt(cluster);
    for (int k = 0; k < lightTotal; ++k) {
        int i = LightIndexAt(cluster, k);
        vec3 lightPos = lightList.lights[i].position.xyz;
        float lightType = lightList.lights[i].position.w;
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        vec3 L;
        if (lightType > 0.5) {
            vec3 lightVec = lightPos - inWorldPos;
            float dist = length(lightVec);
            float range = max(lightList.lights[i].spotDirAndRange.w, 0.1);
            L = lightVec / max(dist, 1e-4);
            float rangeFactor = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            float attenuation = rangeFactor * rangeFactor / (1.0 + 0.09 * dist + 0.032 * dist * dist);
            if (lightType > 1.5) {
                float theta = dot(-L, normalize(lightList.lights[i].spotDirAndRange.xyz));
                float cosInner = lightList.lights[i].spotAnglesAndShadow.x;
                float cosOuter = lightList.lights[i].spotAnglesAndShadow.y;
                float spot = clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
                attenuation *= spot * spot;
            }
            radiance *= attenuation;
        } else {
            L = normalize(lightPos);
            if (ubo.skyParams.w > 0.0 && dot(L, sunDir) > 0.9999) radiance *= SkySunTransmittance(L);
            if (ubo.shadowParams.x > 0.5 && i == int(ubo.shadowParams.y + 0.5)) {
                radiance *= SampleShadow(inWorldPos, N, L, lightList.lights[i].spotAnglesAndShadow.w);
            }
            radiance *= CloudShadow(inWorldPos, L);
        }
        light += radiance * max(dot(N, L), 0.0) / 3.14159265;
    }
    vec3 color = inColor.rgb * light;
    color += inColor.rgb * particle.look.y; // self glow (embers, sparks)

    float camDist = length(ubo.cameraPos.xyz - inWorldPos);
    color = AerialPerspective(color, inWorldPos);
    float fogDensity = ubo.fogColorAndDensity.a;
    if (fogDensity > 0.0001) {
        float heightFactor = exp(-max(inWorldPos.y, 0.0) * 0.15);
        color = mix(color, ubo.fogColorAndDensity.rgb, clamp(1.0 - exp(-camDist * fogDensity * heightFactor), 0.0, 1.0));
    }
    if (ViewUnderwater(inWorldPos) && inWorldPos.y < WaterLevel()) color = UnderwaterFog(color, camDist);
    outColor = vec4(min(color, vec3(60000.0)) * inColor.a, inColor.a);
}
