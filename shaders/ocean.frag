#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "ocean_common.glsl"
#include "water_common.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap; // sun cascades
#include "sun_shadow_common.glsl"
#include "weather_common.glsl"

layout(binding = 9) uniform sampler2D sceneColorMap;  // opaque scene: what's under and behind the water
layout(binding = 24) uniform sampler2D sceneDepthMap; // opaque scene depth from this view's prepass

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inRestPos;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// Distance along the view axis for a depth-buffer value (any perspective projection)
float ViewDistance(float depth) {
    return ubo.proj[3][2] / (depth + ubo.proj[2][2]);
}

float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1, 0)), u.x), mix(Hash(i + vec2(0, 1)), Hash(i + vec2(1, 1)), u.x), u.y);
}

// Broken-up foam pattern drifting with the wind
float FoamPattern(vec2 p, float time) {
    vec2 drift = normalize(ocean.wind.xy + vec2(1e-5, 0.0)) * time * 0.4;
    // Octaves rotated against each other so the noise grid doesn't show
    vec2 q = mat2(0.8, -0.6, 0.6, 0.8) * p;
    float n = ValueNoise(p * 0.9 - drift) * 0.6 + ValueNoise(q * 2.7 + drift * 1.3) * 0.4;
    return smoothstep(0.35, 0.75, n);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

void main() {
    vec3 V = normalize(ubo.cameraPos.xyz - inWorldPos);
    float time = ocean.surface.w;
    // Pixel footprint on the water: waves smaller than this would only sparkle, so they fade
    float footprint = max(length(fwidth(inRestPos)), 1e-3);
    float compression;
    vec3 N = OceanNormal(inRestPos, footprint, compression);
    // Rain: rings from the drops pock the surface (where it isn't sheltered), fading with distance
    float rain = ubo.weatherParams.x;
    if (rain > 0.01) {
        vec2 rings = RainRipples(inWorldPos.xz, rain) * SkyExposure(inWorldPos, vec3(0.0, 1.0, 0.0)) * smoothstep(0.05, 0.01, footprint);
        N = normalize(N + vec3(rings.x, 0.0, rings.y) * 1.5);
    }
    if (ViewUnderwater(inWorldPos)) {
        // Seen from below: the world above shows through a window overhead (Snell's window);
        // further out the surface mirrors the water itself (total internal reflection)
        vec3 down = -N;
        float cosI = clamp(dot(down, V), 0.0, 1.0);
        float sinT2 = 1.7689 * (1.0 - cosI * cosI); // (1.33 sin i)^2
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColorMap, 0));
        vec3 viewNormal = normalize(mat3(ubo.view) * down);
        vec3 above = texture(sceneColorMap, clamp(uv + vec2(viewNormal.x, -viewNormal.y) * 0.04, vec2(0.001), vec2(0.999))).rgb;
        float reflectance = 1.0;
        if (sinT2 < 1.0) reflectance = 0.02 + 0.98 * pow(1.0 - sqrt(1.0 - sinT2), 5.0);
        vec3 color = mix(above, WaterScatterColor(), reflectance);
        color = UnderwaterFog(color, length(ubo.cameraPos.xyz - inWorldPos));
        outColor = vec4(min(color, vec3(60000.0)), 1.0);
        return;
    }
    if (dot(N, V) < 0.0 && V.y < 0.0) N = -N; // seen from below
    float NdotV = max(dot(N, V), 1e-3);
    // Distant water looks rougher (many waves per pixel)
    float roughness = clamp(sqrt(ocean.foamParams.z * ocean.foamParams.z + footprint * 0.02), 0.02, 0.6);

    // How much water the view passes through before reaching what's behind it
    vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneColorMap, 0));
    float waterDistance = ViewDistance(gl_FragCoord.z);
    // View-axis distances to distances along this pixel's ray
    float rayScale = length(inWorldPos - ubo.cameraPos.xyz) / max(waterDistance, 1e-4);
    float thickness = 1000.0; // metres the view travels through water
    float depth = 1000.0;     // how deep the water is here (vertically)
    vec2 behindUV = screenUV;
    if (ocean.foamParams.y > 0.5) {
        float sceneDistance = ViewDistance(texture(sceneDepthMap, screenUV).r);
        thickness = max(sceneDistance - waterDistance, 0.0) * rayScale;
        // The bottom seen through this pixel lies further along the same view ray
        vec3 bottom = ubo.cameraPos.xyz + (inWorldPos - ubo.cameraPos.xyz) * (sceneDistance / max(waterDistance, 1e-4));
        depth = max(inWorldPos.y - bottom.y, 0.0);
        // Refraction: bend the view by the surface, less where the water is shallow. Only take the
        // bent sample when what it shows really is under the water.
        vec3 viewNormal = normalize(mat3(ubo.view) * N);
        vec2 offset = vec2(viewNormal.x, -viewNormal.y) * 0.05 * clamp(depth, 0.0, 1.0);
        vec2 bentUV = clamp(screenUV + offset, vec2(0.001), vec2(0.999));
        float bentThickness = (ViewDistance(texture(sceneDepthMap, bentUV).r) - waterDistance) * rayScale;
        if (bentThickness > 0.0) {
            behindUV = bentUV;
            thickness = bentThickness;
        }
    }

    // Light: the environment, plus each light's highlight and diffuse light (for foam and the
    // light scattered inside the water)
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    vec3 skySunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 groundGI = vec3(0.05, 0.04, 0.03);
    vec3 ambient = (skyOrHdri ? EnvIrradiance(vec3(0.0, 1.0, 0.0), skySunDir) : ubo.giSkyColor.rgb) * giStrength;

    vec3 specular = vec3(0.0);
    vec3 diffuse = vec3(0.0); // light reaching a surface facing N (times NdotL)
    vec3 downwelling = vec3(0.0); // light entering the water from above
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
            if (ubo.skyParams.w > 0.0 && dot(L, skySunDir) > 0.9999) radiance *= SkySunTransmittance(L);
            // Shadows of rocks, boats and piers fall on the water
            if (ubo.shadowParams.x > 0.5 && i == int(ubo.shadowParams.y + 0.5)) {
                radiance *= SampleShadow(inWorldPos, vec3(0.0, 1.0, 0.0), L, lightList.lights[i].spotAnglesAndShadow.w);
            }
            radiance *= CloudShadow(inWorldPos, L);
        }
        float NdotL = max(dot(N, L), 0.0);
        vec3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float F = 0.02 + 0.98 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
        float k2 = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float G = (NdotV / (NdotV * (1.0 - k2) + k2)) * (NdotL / (NdotL * (1.0 - k2) + k2));
        specular += DistributionGGX(NdotH, roughness) * G * F / (4.0 * NdotV * NdotL + 1e-4) * radiance * NdotL;
        diffuse += radiance * NdotL;
        downwelling += radiance * max(L.y, 0.0);
    }

    // Reflection of the sky / HDRI, refined by a reflection probe
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 reflection = skyOrHdri ? EnvSpecular(R, roughness, skySunDir)
                                : mix(groundGI, ubo.giSkyColor.rgb, R.y * 0.5 + 0.5);
    int probe;
    float probeWeight = ProbeInfluence(inWorldPos, probe);
    if (probeWeight > 0.0) {
        reflection = mix(reflection, SampleProbe(probe, ProbeDirection(probe, inWorldPos, R), roughness * ProbeMaxLod()), probeWeight);
    }
    reflection *= giStrength;
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // The water body: what's behind, tinted and dimmed with depth, plus light scattered back
    // out of the water (the water's own colour, strongest where it's deep)
    float clarity = max(ocean.shallowColor.a, 0.05);
    float transmittance = exp(-thickness / clarity);
    vec3 behind = texture(sceneColorMap, behindUV).rgb;
    vec3 tint = mix(ocean.shallowColor.rgb, vec3(1.0), exp(-thickness * 0.7));
    vec3 scattered = ocean.deepColor.rgb * (ambient + downwelling / PI);
    vec3 body = behind * tint * transmittance + scattered * (1.0 - transmittance);

    // Reflections fade out where the water is very thin, so the waterline isn't a hard edge
    float edge = smoothstep(0.0, 0.12, depth);
    vec3 color = body * (1.0 - fresnel * edge) + (reflection * fresnel + specular) * edge;

    // Foam: whitecaps where crests bunch up, and a band along shores and around objects
    float pattern = FoamPattern(inRestPos, time);
    float whitecaps = smoothstep(0.75, 0.25, compression) * clamp(ocean.deepColor.a, 0.0, 1.0) * 1.6;
    float shore = 0.0;
    if (ocean.foamParams.y > 0.5 && ocean.foamParams.x > 0.0) {
        // Distance to the waterline, estimated from how fast the depth changes across the water:
        // a gentle beach gets a band of the set width, a rock or wall a thin ring around it
        float width = ocean.foamParams.x;
        float slope = length(vec2(dFdx(depth), dFdy(depth))) / footprint;
        float shoreDistance = depth / clamp(slope, 0.02, 20.0);
        float band = (1.0 - smoothstep(0.0, width, shoreDistance)) * (1.0 - smoothstep(0.0, width * 0.5, depth));
        // Solid at the waterline, breaking into lace further out that washes in and out
        float washing = 0.5 + 0.5 * sin(shoreDistance / width * 8.0 - time * 1.6);
        float lace = smoothstep(0.4, 0.7, pattern * 0.8 + washing * 0.3);
        shore = band * mix(lace, 1.0, band * band);
    }
    float foam = clamp(whitecaps * mix(0.35, 1.0, pattern) + shore, 0.0, 1.0) * smoothstep(0.0, 0.03, depth);
    vec3 foamColor = vec3(0.9) * (ambient + diffuse / PI);
    color = mix(color, foamColor, foam);

    color = AerialPerspective(color, inWorldPos);

    // Exponential height fog (same as shader.frag)
    float fogDensity = ubo.fogColorAndDensity.a;
    if (fogDensity > 0.0001) {
        float camDist = length(ubo.cameraPos.xyz - inWorldPos);
        float heightFactor = exp(-max(inWorldPos.y, 0.0) * 0.15);
        float fogAmount = clamp(1.0 - exp(-camDist * fogDensity * heightFactor), 0.0, 1.0);
        color = mix(color, ubo.fogColorAndDensity.rgb, fogAmount);
    }

    outColor = vec4(min(color, vec3(60000.0)), 1.0);
}
