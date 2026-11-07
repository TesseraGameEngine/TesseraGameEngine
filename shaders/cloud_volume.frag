#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "weather_common.glsl"

// Volumetric clouds (VulkanPipeline::DrawVolumetricClouds), drawn over the finished opaque scene:
// each pixel's view ray is marched through the cloud slab (cloudParams.z up to that plus the
// thickness), stopping at the opaque surface in front (the prepass depth), so clouds sit in front
// of distant mountains and the camera can fly into, through and above them. The cloud shapes are
// the same coverage map as the flat clouds and cloud shadows, rounded by a height profile and
// eroded by 3D Worley detail; light comes from the sun (or moon) through the cloud toward it and
// from the sky above.
layout(binding = 24) uniform sampler2D sceneDepthMap;  // opaque depth from this view's prepass
layout(binding = 30) uniform sampler3D cloudDetailMap; // tiling 3D detail (VulkanSkyResources)

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (12.566371 * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

// Cloud density at a point (0..1)
float CloudDensity(vec3 p, float base, float thickness, bool detail) {
    float fraction = (p.y - base) / thickness;
    if (fraction < 0.0 || fraction > 1.0) return 0.0;
    // A wider ramp than the flat layer's, so clouds swell gently from their edges
    float coverage = clamp((CloudCoverageNoise(p.xz) - CloudCoverageThreshold() + 0.02) / 0.4, 0.0, 1.0);
    if (coverage <= 0.001) return 0.0;
    // Domes: a cloud rises as high as its cover is thick (thin edges stay low), with a flat-ish
    // base that lifts a little at the edges
    float height = pow(coverage, 0.7);
    float bottom = smoothstep(0.0, 0.06 + 0.12 * (1.0 - coverage), fraction);
    float top = smoothstep(height, height * 0.5, fraction);
    float density = min(coverage * 1.6, 1.0) * bottom * top;
    if (detail && density > 0.0) {
        // Billows eat into the edges (more toward the bottom, where clouds look wispier); two
        // scales of detail keep the bubbles from repeating
        vec3 drift = vec3(ubo.cloudWind.x, 0.0, ubo.cloudWind.y) * ubo.timeParams.x * 1.2;
        vec3 q = (p + drift) / (ubo.cloudParams.w * 0.45);
        float billow = mix(texture(cloudDetailMap, q).r, texture(cloudDetailMap, q * 2.7 + 0.37).r, 0.35);
        float erosion = ubo.volumetricCloudParams.z * mix(0.6, 0.35, fraction);
        density = clamp((density - (1.0 - billow) * erosion) / max(1.0 - erosion, 0.05), 0.0, 1.0);
    }
    return density;
}

void main() {
    float base = ubo.cloudParams.z;
    float thickness = max(ubo.volumetricCloudParams.y, 50.0);
    float top = base + thickness;

    // The view ray through this pixel
    vec4 clip = vec4(inUV * 2.0 - 1.0, 1.0, 1.0);
    mat4 invViewProj = inverse(ubo.proj * ubo.view);
    vec4 farPoint = invViewProj * clip;
    vec3 origin = ubo.cameraPos.xyz;
    vec3 dir = normalize(farPoint.xyz / farPoint.w - origin);

    // How far the opaque scene is along it (sky: very far)
    float depth = texture(sceneDepthMap, inUV).r;
    float sceneDistance = 1e9;
    if (depth > 0.0 && depth < 0.99999) {
        vec3 forward = -vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);
        sceneDistance = (ubo.proj[3][2] / (depth + ubo.proj[2][2])) / max(dot(dir, forward), 1e-3);
    }

    // The part of the ray inside the slab
    float t0, t1;
    if (abs(dir.y) < 1e-4) {
        if (origin.y < base || origin.y > top) discard;
        t0 = 0.0;
        t1 = 60000.0;
    } else {
        float ta = (base - origin.y) / dir.y;
        float tb = (top - origin.y) / dir.y;
        t0 = max(min(ta, tb), 0.0);
        t1 = max(ta, tb);
    }
    t1 = min(min(t1, sceneDistance), t0 + 60000.0);
    if (t1 <= t0) discard;

    vec3 sunDir = normalize(ubo.skyParams.xyz);
    vec3 moonDir = ubo.moonParams.w > 0.0 ? normalize(ubo.moonParams.xyz) : vec3(0.0, -1.0, 0.0);
    bool sunUp = sunDir.y > -0.04;
    vec3 L = sunUp ? sunDir : moonDir;
    vec3 lightColor = sunUp ? SkySunColor(sunDir) * SkySunTransmittance(sunDir) * 1.6
                            : vec3(0.5, 0.6, 0.85) * 0.03 * ubo.moonParams.w * max(moonDir.y, 0.0);
    lightColor *= SkyLightScale();
    lightColor += vec3(0.6, 0.65, 0.85) * ubo.lightningParams.x * 3.0;
    // Light from the sky (half its colour: inside a cloud the sky's blue is mixed with white
    // light scattered from the rest of the cloud) and from the sunlit ground below
    vec3 skyUp = HdriActive() ? HdriRadiance(vec3(0.0, 1.0, 0.0), max(HdriMaxLod() - 1.0, 0.0))
                              : SkyRadiance(vec3(0.0, 1.0, 0.0), sunDir, false);
    vec3 ambientTop = mix(skyUp, vec3(dot(skyUp, vec3(0.3, 0.5, 0.2))), 0.5) * 1.6;
    vec3 ambientBottom = lightColor * max(L.y, 0.0) * 0.12 + ambientTop * 0.35;
    float cosTheta = dot(dir, L);
    float phase = mix(HenyeyGreenstein(cosTheta, 0.6), HenyeyGreenstein(cosTheta, -0.25), 0.35) * 12.566371;

    // March: fewer, longer steps far away; jittered per pixel and frame (TAA blends the noise)
    const int kSteps = 48;
    float jitter = fract(WeatherHash(gl_FragCoord.xy) + ubo.timeParams.w * 0.618034);
    float span = t1 - t0;
    float dt = span / float(kSteps);
    float extinctionScale = 0.04 * ubo.cloudParams.y; // per metre at full density
    vec3 radiance = vec3(0.0);
    float transmittance = 1.0;
    float firstHit = -1.0;
    for (int i = 0; i < kSteps; ++i) {
        float t = t0 + (float(i) + jitter) * dt;
        vec3 p = origin + dir * t;
        float density = CloudDensity(p, base, thickness, true);
        if (density <= 0.002) continue;
        if (firstHit < 0.0) firstHit = t;
        // Light reaching this point through the cloud toward the light
        float lightDepth = 0.0;
        float lightStep = thickness * 0.08;
        for (int j = 1; j <= 5; ++j) {
            lightDepth += CloudDensity(p + L * lightStep * float(j) * float(j) * 0.4, base, thickness, j < 3) * lightStep * float(j) * 0.8;
        }
        float sigma = density * extinctionScale;
        // Light scattered many times reaches deep into a cloud: a few octaves of ever weaker
        // extinction and a flatter phase (Wrenninge's approximation)
        float od = lightDepth * extinctionScale;
        float lightTrans = (exp(-od) * phase + 0.5 * exp(-od * 0.25) * mix(phase, 1.0, 0.5) +
                            0.25 * exp(-od * 0.0625)) / 1.4;
        float powder = mix(1.0, 1.0 - exp(-density * 6.0), 0.4); // darker edges looking away from the light
        float heightFraction = clamp((p.y - base) / thickness, 0.0, 1.0);
        vec3 scattered = lightColor * lightTrans * powder + mix(ambientBottom, ambientTop, heightFraction);
        float stepTrans = exp(-sigma * dt);
        radiance += transmittance * scattered * (1.0 - stepTrans);
        transmittance *= stepTrans;
        if (transmittance < 0.01) break;
    }
    float alpha = 1.0 - transmittance;
    if (alpha < 0.003) discard;

    // Distant cloud fades into the sky's haze toward the horizon
    float haze = 1.0 - exp(-max(firstHit, t0) / 45000.0);
    vec3 skyBehind = HdriActive() ? HdriRadiance(dir, 0.0) : SkyRadiance(dir, sunDir, false);
    radiance = mix(radiance, skyBehind * alpha, haze);

    // Where the cloud is solid, it becomes the depth for anything drawn after it (the ocean)
    float hitDistance = firstHit < 0.0 ? t0 : firstHit;
    vec4 hitClip = ubo.proj * ubo.view * vec4(origin + dir * hitDistance, 1.0);
    gl_FragDepth = alpha > 0.5 ? clamp(hitClip.z / hitClip.w, 0.0, 1.0) : (depth > 0.0 ? depth : 1.0);
    outColor = vec4(radiance, alpha);
}
