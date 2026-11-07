#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "water_common.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap;       // sun cascades (volumetric shafts)
layout(binding = 11) uniform sampler2DArrayShadow localShadowMap; // point / spot shadows
#include "volumetric_common.glsl"

layout(location = 0) in vec3 inViewDir;
layout(location = 0) out vec4 outColor;

float Hash(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

// Turns v around the unit axis k by angle (radians)
vec3 RotateAround(vec3 v, vec3 k, float angle) {
    float c = cos(angle), s = sin(angle);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

// Sparse twinkling stars, only visible once the sun is well below the horizon
vec3 Stars(vec3 viewDir, vec3 sunDir) {
    float night = 1.0 - smoothstep(-0.2, -0.02, sunDir.y);
    if (night <= 0.0 || viewDir.y <= 0.0) return vec3(0.0);
    // With the clock on, the stars turn around the celestial pole with the time of day
    vec3 dir = viewDir;
    if (ubo.skyClockParams.w > 0.5) {
        float latitude = ubo.skyClockParams.y, north = ubo.skyClockParams.z;
        vec3 pole = vec3(0.0, sin(latitude), -cos(latitude));
        pole = vec3(cos(north) * pole.x + sin(north) * pole.z, pole.y, -sin(north) * pole.x + cos(north) * pole.z);
        dir = RotateAround(viewDir, normalize(pole), ubo.skyClockParams.x);
    }

    vec3 cell = floor(dir * 220.0);
    float h = Hash(cell);
    if (h < 0.9975) return vec3(0.0);

    // Round star centred in its cell
    vec3 center = (cell + 0.5) / 220.0;
    float d = length(normalize(center) - dir) * 220.0;
    float shape = smoothstep(0.55, 0.0, d);
    float twinkle = 0.7 + 0.3 * sin(ubo.timeParams.x * (1.5 + h * 3.0) + h * 40.0);
    float brightness = (h - 0.9975) / 0.0025;
    return vec3(0.85, 0.9, 1.0) * shape * twinkle * (0.4 + brightness * 1.6) * night * smoothstep(0.0, 0.15, viewDir.y);
}

// A lightning bolt below the cloud base while ubo.lightningParams.w is set: a jagged channel at
// the strike's azimuth from the ground up to about 17 degrees, with one fork
vec3 LightningBolt(vec3 dir) {
    if (ubo.lightningParams.w < 0.5) return vec3(0.0);
    const float kTop = 0.3;
    float e = asin(clamp(dir.y, -1.0, 1.0));
    if (e < -0.01 || e > kTop) return vec3(0.0);
    float seed = ubo.lightningParams.z;
    float azimuth = atan(dir.x, -dir.z);
    float boltAz = ubo.lightningParams.y + (CloudNoise(vec2(e * 18.0, seed)) - 0.5) * 0.08 +
                   (CloudNoise(vec2(e * 70.0, seed + 7.0)) - 0.5) * 0.02;
    float d = abs(mod(azimuth - boltAz + 3.14159265, 6.2831853) - 3.14159265) * cos(e);
    float strength = 1.0;
    float forkStart = 0.12 + 0.08 * fract(seed * 0.37);
    if (e < forkStart && e > forkStart - 0.08) {
        float forkAz = boltAz + (forkStart - e) * (fract(seed * 0.71) > 0.5 ? 0.7 : -0.7) +
                       (CloudNoise(vec2(e * 55.0, seed + 3.0)) - 0.5) * 0.02;
        float forkD = abs(mod(azimuth - forkAz + 3.14159265, 6.2831853) - 3.14159265) * cos(e);
        if (forkD * 1.6 < d) { d = forkD * 1.6; strength = 0.6; }
    }
    float core = exp(-d / 0.0009);
    float glow = exp(-d / 0.012) * 0.15;
    float brightness = ubo.lightningParams.x + 0.3;
    return vec3(0.8, 0.85, 1.0) * (core * 25.0 * strength + glow) * smoothstep(kTop, kTop - 0.03, e) * brightness;
}

// The moon: a sphere lit by the sun, so its phase follows where the sun is (ubo.moonParams)
vec3 Moon(vec3 dir, vec3 sunDir) {
    if (ubo.moonParams.w <= 0.0) return vec3(0.0);
    vec3 m = normalize(ubo.moonParams.xyz);
    float cosAngle = dot(dir, m);
    if (cosAngle < 0.99) return vec3(0.0);
    const float radius = 0.0175; // about twice the real moon, as with the sun
    vec3 right = normalize(abs(m.y) < 0.99 ? cross(vec3(0.0, 1.0, 0.0), m) : vec3(1.0, 0.0, 0.0));
    vec3 up = cross(m, right);
    vec3 offset = dir - m * cosAngle;
    vec2 uv = vec2(dot(offset, right), dot(offset, up)) / radius;
    float r2 = dot(uv, uv);
    float visible = smoothstep(-0.01, 0.02, dir.y);
    // A faint halo around the bright part
    float lit = 0.5 * (1.0 - dot(sunDir, m));
    vec3 halo = vec3(0.6, 0.7, 0.9) * pow(max(cosAngle, 0.0), 3000.0) * 0.08 * lit;
    if (r2 >= 1.0) return halo * visible * ubo.moonParams.w * ubo.skyParams.w;
    vec3 n = right * uv.x + up * uv.y - m * sqrt(1.0 - r2); // the face turned toward us
    float sunlit = smoothstep(-0.03, 0.08, dot(n, sunDir));
    float maria = 0.5 + 0.5 * smoothstep(0.35, 0.65, CloudFbm(uv * 1.6 + 11.0, 4)); // darker 'seas'
    float edge = smoothstep(1.0, 0.92, r2);
    vec3 surface = vec3(0.95, 0.93, 0.88) * (sunlit * maria * 1.1 + 0.004); // + earthshine
    return (surface * edge + halo) * visible * ubo.moonParams.w * ubo.skyParams.w;
}

void main() {
    vec3 dir = normalize(inViewDir);
    vec3 sunDir = normalize(ubo.skyParams.xyz);

    // Clouds in front of the sun, moon and stars, over the procedural sky or an HDRI. The stars and
    // moon are so much brighter than a night cloud that the little light a cloud lets through
    // would still show them, so they dim with the cover itself (and vanish under a full deck).
    // With volumetric clouds the cloud pass draws them over everything after this.
    bool volumetric = ubo.volumetricCloudParams.x > 0.5;
    vec3 skyAmbient = HdriActive() ? HdriRadiance(vec3(0.0, 1.0, 0.0), max(HdriMaxLod() - 1.0, 0.0))
                                   : SkyRadiance(vec3(0.0, 1.0, 0.0), sunDir, false);
    vec4 clouds = volumetric ? vec4(0.0) : SkyClouds(dir, sunDir, 5, skyAmbient);
    float deck = CloudsActive() ? ubo.cloudParams.y * smoothstep(0.6, 1.0, ubo.cloudParams.x) : 0.0;
    float hidden = clamp(max(clouds.a * 1.4, deck), 0.0, 1.0);
    vec3 color = HdriActive() ? HdriRadiance(dir, 0.0)
                              : SkyRadiance(dir, sunDir, true) + (Stars(dir, sunDir) * ubo.skyParams.w + Moon(dir, sunDir)) * (1.0 - hidden);
    color = mix(color, clouds.rgb, clouds.a);
    // Lightning lights up the clouds and the sky, and shows its bolt below them
    float flash = ubo.lightningParams.x;
    if (flash > 0.001) color += vec3(0.55, 0.6, 0.8) * flash * (0.15 + max(clouds.a, deck) * 1.2) * smoothstep(-0.05, 0.1, dir.y);
    color += LightningBolt(dir);

    // Fog thickens toward the horizon so distant fogged geometry blends into the sky
    float fogDensity = ubo.fogColorAndDensity.a;
    if (fogDensity > 0.0001) {
        float fogAmount = clamp(fogDensity * 40.0, 0.0, 1.0) * (1.0 - smoothstep(0.0, 0.35, abs(dir.y)));
        color = mix(color, ubo.fogColorAndDensity.rgb, fogAmount);
    }

    // Volumetric fog out to a fixed distance, so light shafts continue against the sky
    if (VolumetricActive()) {
        vec4 volumetric = VolumetricFog(ubo.cameraPos.xyz, ubo.cameraPos.xyz + dir * kVolumetricSkyDistance, gl_FragCoord.xy);
        color = color * volumetric.a + volumetric.rgb;
    }

    // Under water: away from straight up, only water. Light from the sky only gets in through a
    // cone overhead (Snell's window, 48.6 degrees from vertical), where the ocean surface is
    // drawn over the sky anyway; this also hides the sky beyond the ocean's far edge.
    if (ViewUnderwater(ubo.cameraPos.xyz + dir) && dir.y < 0.6) color = UnderwaterFog(color, 1e4);

    // Linear HDR; exposure, bloom and tone mapping happen in composite.frag
    outColor = vec4(min(color, vec3(60000.0)), 1.0);
}
