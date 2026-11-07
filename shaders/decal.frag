#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "common_material.glsl"
#include "material_ext.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap;
#include "sun_shadow_common.glsl"

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 24) uniform sampler2D sceneDepthMap;  // opaque depth from this view's prepass
layout(binding = 39) uniform sampler2D sceneNormalMap; // view-space normals from the prepass

layout(location = 0) flat in mat4 inInvViewProj;
layout(location = 4) flat in mat4 inToDecal;
layout(location = 0) out vec4 outColor;

// Push constants (PushConstantData): model = the decal's box, color = tint (a = opacity),
// emissiveColorAndIntensity = glow (additive mode), roughness = cos of the angle cutoff,
// metallic = fade distance (m), aoStrength = blend mode (0 alpha, 1 multiply, 2 additive),
// heightScale = the decal's own roughness
void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneDepthMap, 0));
    float depth = texture(sceneDepthMap, uv).r;
    if (depth >= 0.99999) discard; // sky
    vec4 world4 = inInvViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPos = world4.xyz / world4.w;

    // Into the box: outside it, nothing
    vec3 local = (inToDecal * vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    // The surface must face along the projection (the box's y axis) closely enough
    vec3 viewNormal = texture(sceneNormalMap, uv).xyz;
    vec3 N = normalize(transpose(mat3(ubo.view)) * viewNormal);
    mat3 boxAxes = mat3(inverse(inToDecal));
    vec3 axisX = normalize(boxAxes[0]);
    vec3 axisY = normalize(boxAxes[1]);
    vec3 axisZ = normalize(boxAxes[2]);
    float facing = abs(dot(N, axisY));
    if (facing < pc.roughness) discard;

    vec2 decalUV = vec2(local.x + 0.5, 0.5 - local.z);
    vec4 albedo = pc.color;
    if ((pc.textureFlags & TEX_ALBEDO) != 0) albedo *= texture(albedoMap, decalUV);
    // Softened at the box's ends along the projection, at grazing angles and far away
    float fade = (1.0 - smoothstep(0.35, 0.5, abs(local.y))) * smoothstep(pc.roughness, min(pc.roughness + 0.15, 1.0), facing);
    float distanceFade = 1.0 - smoothstep(pc.metallic * 0.8, pc.metallic, length(ubo.cameraPos.xyz - worldPos));
    float alpha = clamp(albedo.a * fade * distanceFade, 0.0, 1.0);
    if (alpha <= 0.003) discard;

    int mode = int(pc.aoStrength + 0.5);
    if (mode == 1) {
        // Stain: darkens what's there by the decal's colour (blend: dst * src)
        outColor = vec4(mix(vec3(1.0), albedo.rgb, alpha), 1.0);
        return;
    }
    if (mode == 2) {
        // Glow: added light (blend: one, one)
        outColor = vec4(albedo.rgb * pc.emissiveColorAndIntensity.rgb * pc.emissiveColorAndIntensity.a * alpha, 0.0);
        return;
    }

    // Paint: lit like the surface it lies on, its own normal map bending the light
    if ((pc.textureFlags & TEX_NORMAL) != 0) {
        vec3 n = DecodeNormalXY(texture(normalMap, decalUV).xy);
        vec3 T = normalize(axisX - N * dot(axisX, N));
        vec3 B = normalize(cross(N, T)) * sign(dot(cross(N, T), -axisZ));
        N = normalize(T * n.x + B * n.y + N * n.z);
    }
    vec3 skySunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 light = (skyOrHdri ? EnvIrradiance(N, skySunDir) : ubo.giSkyColor.rgb) * giStrength;
    uvec2 cluster = lightClusters.clusters[ClusterIndex(worldPos)];
    int lightTotal = LightCountAt(cluster);
    for (int k = 0; k < lightTotal; ++k) {
        int i = LightIndexAt(cluster, k);
        vec3 lightPos = lightList.lights[i].position.xyz;
        float lightType = lightList.lights[i].position.w;
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        vec3 L;
        if (lightType > 0.5) {
            vec3 lightVec = lightPos - worldPos;
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
            if (ubo.skyParams.w > 0.0 && dot(L, skySunDir) > 0.9999) radiance *= SkySunTransmittance(L);
            if (ubo.shadowParams.x > 0.5 && i == int(ubo.shadowParams.y + 0.5)) {
                radiance *= SampleShadow(worldPos, N, L, lightList.lights[i].spotAnglesAndShadow.w);
            }
            radiance *= CloudShadow(worldPos, L);
        }
        light += radiance * max(dot(N, L), 0.0) / 3.14159265;
    }
    vec3 color = albedo.rgb * light + pc.emissiveColorAndIntensity.rgb * pc.emissiveColorAndIntensity.a;
    outColor = vec4(min(color, vec3(60000.0)) * alpha, alpha);
}
