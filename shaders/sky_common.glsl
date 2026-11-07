// Procedural sky shared by the sky background (sky.frag) and the environment
// lighting in shader.frag, so reflections and ambient light always match the sky.
// Values are linear HDR, before exposure and tone mapping. Two models: a stylised sky (colour
// ramps), or a physically based atmosphere (ubo.atmosphereParams.x), read from the view's
// sky-view look-up table that sky_view_lut.comp ray-marches each frame.
#ifndef SKY_COMMON_GLSL
#define SKY_COMMON_GLSL

#include "atmosphere_common.glsl"

layout(binding = 29) uniform sampler2D skyViewLut;       // physical atmosphere (VulkanSkyResources)
layout(binding = 31) uniform sampler2D cloudCoverageMap; // tiling cloud shapes (VulkanSkyResources)

bool PhysicalAtmosphere() {
    return ubo.atmosphereParams.x > 0.5;
}

// Brightness the sky's light is scaled by: the sky intensity, or the HDRI's without the sky
float SkyLightScale() {
    return ubo.skyParams.w > 0.0 ? ubo.skyParams.w : max(ubo.envParams.y, 1e-4);
}

// 0 at night, 1 in full daylight
float SkyDayFactor(vec3 sunDir) {
    return smoothstep(-0.12, 0.25, sunDir.y);
}

// Peaks while the sun is near the horizon
float SkySunsetFactor(vec3 sunDir) {
    return 1.0 - smoothstep(0.0, 0.35, abs(sunDir.y - 0.04));
}

vec3 SkySunColor(vec3 sunDir) {
    if (PhysicalAtmosphere()) {
        vec3 t = ubo.sunTransmittance.rgb;
        return t / max(max(t.r, max(t.g, t.b)), 1e-4);
    }
    return mix(vec3(1.0, 0.42, 0.18), vec3(1.0, 0.95, 0.88), smoothstep(0.0, 0.45, sunDir.y));
}

// How much direct sunlight survives the atmosphere: white at noon, orange near
// the horizon, nothing once the sun has set. Applied to the sky's sun light.
vec3 SkySunTransmittance(vec3 sunDir) {
    if (PhysicalAtmosphere()) {
        // Through the real atmosphere (computed on the CPU for this view), normalised so the noon
        // sun keeps the light's set brightness
        return ubo.sunTransmittance.rgb / max(ubo.sunTransmittance.w, 1e-4);
    }
    vec3 tint = mix(vec3(1.0, 0.38, 0.12), vec3(1.0), smoothstep(0.0, 0.5, sunDir.y));
    return tint * smoothstep(-0.04, 0.06, sunDir.y);
}

// Grey the sky's light under a heavy cloud deck (sky.frag draws the clouds themselves)
vec3 OvercastSky(vec3 color) {
    float overcast = ubo.cloudParams.y * smoothstep(0.55, 1.0, ubo.cloudParams.x);
    return mix(color, vec3(dot(color, vec3(0.3, 0.5, 0.2))) * 1.2, overcast * 0.8);
}

// The physical atmosphere along dir (from the view's sky-view LUT)
vec3 PhysicalSkyRadiance(vec3 dir, vec3 sunDir, bool includeSun) {
    float elevation = asin(clamp(dir.y, -1.0, 1.0));
    float azimuth = 0.0;
    vec2 dirFlat = dir.xz;
    vec2 sunFlat = sunDir.xz;
    if (dot(dirFlat, dirFlat) > 1e-8 && dot(sunFlat, sunFlat) > 1e-8) {
        azimuth = acos(clamp(dot(normalize(dirFlat), normalize(sunFlat)), -1.0, 1.0));
    }
    vec3 sky = textureLod(skyViewLut, SkyViewUV(elevation, azimuth, ubo.atmosphereParams.y), 0.0).rgb;
    // Starlight and airglow keep the night sky from going completely black
    float day = SkyDayFactor(sunDir);
    sky += mix(vec3(0.015, 0.025, 0.05), vec3(0.004, 0.007, 0.018), pow(max(dir.y, 0.0), 0.45)) * (1.0 - day);
    // The ground below the horizon: lit by the sun through the air and by the sky
    float sunOnGround = max(sunDir.y, 0.0) * dot(ubo.sunTransmittance.rgb, vec3(0.3, 0.5, 0.2));
    vec3 ground = vec3(0.20, 0.19, 0.17) * (0.04 + 0.96 * day) * mix(0.4, 1.0, sunOnGround);
    sky = mix(sky, sky * 0.5 + ground, smoothstep(0.0, 0.12, -dir.y));
    sky = OvercastSky(sky);
    if (includeSun) {
        float disc = smoothstep(0.99990, 0.99995, dot(dir, sunDir));
        sky += ubo.sunTransmittance.rgb * disc * 40.0 * step(-0.02, dir.y);
    }
    return sky;
}

// Sky radiance seen along `dir`. includeSun adds the bright sun disc, which surface
// lighting leaves out because the directional light already accounts for it.
vec3 SkyRadiance(vec3 dir, vec3 sunDir, bool includeSun) {
    if (PhysicalAtmosphere()) return PhysicalSkyRadiance(dir, sunDir, includeSun) * ubo.skyParams.w;

    float day = SkyDayFactor(sunDir);
    float sunset = SkySunsetFactor(sunDir);

    vec3 zenith  = mix(vec3(0.004, 0.007, 0.018), vec3(0.045, 0.15, 0.48), day);
    vec3 horizon = mix(vec3(0.015, 0.025, 0.05), vec3(0.36, 0.50, 0.70), day);

    // Warm sunset band, strongest on the side of the sky facing the sun
    vec2 dirFlat = dir.xz;
    vec2 sunFlat = sunDir.xz;
    float towardSun = 0.5;
    if (dot(dirFlat, dirFlat) > 1e-6 && dot(sunFlat, sunFlat) > 1e-6) {
        towardSun = dot(normalize(dirFlat), normalize(sunFlat)) * 0.5 + 0.5;
    }
    float sunsetWeight = sunset * mix(0.35, 1.0, towardSun * towardSun);
    horizon = mix(horizon, vec3(1.0, 0.48, 0.22), sunsetWeight * 0.85);
    zenith  = mix(zenith,  vec3(0.16, 0.18, 0.38), sunset * 0.5);

    // A heavy cloud deck (sky.frag draws the clouds themselves) turns the sky's light grey
    float overcast = ubo.cloudParams.y * smoothstep(0.55, 1.0, ubo.cloudParams.x);
    zenith = mix(zenith, vec3(dot(zenith, vec3(0.3, 0.5, 0.2))) * 1.4, overcast * 0.8);
    horizon = mix(horizon, vec3(dot(horizon, vec3(0.3, 0.5, 0.2))), overcast * 0.8);

    vec3 sky;
    if (dir.y >= 0.0) {
        sky = mix(horizon, zenith, pow(dir.y, 0.45));
    } else {
        // Below the horizon: dim ground, blending up into the horizon haze
        // Roughly a sunlit mid-grey ground, so metals don't reflect black below the horizon
        vec3 ground = vec3(0.20, 0.19, 0.17) * (0.05 + 0.95 * day);
        sky = mix(horizon, ground, smoothstep(0.0, 0.12, -dir.y));
    }

    // Forward-scattering glow around the sun
    float cosTheta = dot(dir, sunDir);
    float glowVisibility = smoothstep(-0.08, 0.05, sunDir.y);
    float glow = pow(max(cosTheta, 0.0), 8.0) * 0.25 + pow(max(cosTheta, 0.0), 64.0) * 0.8;
    sky += SkySunColor(sunDir) * glow * glowVisibility;

    if (includeSun) {
        // About 2x the real sun's size, as games usually exaggerate it
        float disc = smoothstep(0.99990, 0.99995, cosTheta);
        sky += SkySunColor(sunDir) * disc * 40.0 * glowVisibility * step(-0.02, dir.y);
    }

    return sky * ubo.skyParams.w;
}

// Physical atmosphere: the air between the camera and a surface dims it and adds the light it
// scatters toward the camera (aerial perspective), so distant hills fade into the sky's colour
vec3 AerialPerspective(vec3 color, vec3 worldPos) {
    if (!PhysicalAtmosphere()) return color;
    vec3 toPoint = worldPos - ubo.cameraPos.xyz;
    float distance = length(toPoint);
    if (distance < 10.0) return color;
    vec3 dir = toPoint / distance;
    float height = max((ubo.cameraPos.y + worldPos.y) * 0.5, 0.0) / 1000.0;
    vec3 transmittance = exp(-AtmosphereExtinction(height) * (distance / 1000.0));
    vec3 sunDir = normalize(ubo.skyParams.xyz);
    vec3 inScattered = PhysicalSkyRadiance(vec3(dir.x, max(dir.y, 0.0), dir.z), sunDir, false) * ubo.skyParams.w;
    return color * transmittance + inScattered * (1.0 - transmittance);
}

// ---- Clouds: one procedural layer at ubo.cloudParams.z metres, moved by the wind ----

float CloudHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float CloudNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(CloudHash(i), CloudHash(i + vec2(1.0, 0.0)), u.x),
               mix(CloudHash(i + vec2(0.0, 1.0)), CloudHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float CloudFbm(vec2 p, int octaves) {
    const mat2 turn = mat2(0.8, 0.6, -0.6, 0.8);
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * CloudNoise(p);
        total += amplitude;
        p = turn * p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return sum / total;
}

// Clouds show over the procedural sky or an HDRI (the CPU leaves cloudParams.y at 0 otherwise)
bool CloudsActive() {
    return ubo.cloudParams.y > 0.0;
}

// The coverage noise at a point of the cloud layer (0..1, before the coverage threshold). The
// shapes come from a tiling map (8 noise cells across, one per cloudParams.w metres).
float CloudCoverageNoise(vec2 xz) {
    vec2 p = (xz - ubo.cloudWind.xy * ubo.timeParams.x) / ubo.cloudParams.w;
    return textureLod(cloudCoverageMap, p / 8.0, 0.0).r;
}

// Noise above this is cloud: lower with more coverage
float CloudCoverageThreshold() {
    return mix(0.74, 0.16, ubo.cloudParams.x);
}

// How much cloud covers a point of the cloud layer (0..1); `octaves` is kept for the callers
float CloudDensityAt(vec2 xz, int octaves) {
    float threshold = CloudCoverageThreshold();
    return smoothstep(threshold, threshold + 0.2, CloudCoverageNoise(xz));
}

// Light reaching a point through the cloud layer from direction L (1 = clear)
float CloudShadow(vec3 worldPos, vec3 L) {
    if (!CloudsActive() || L.y <= 0.02 || worldPos.y >= ubo.cloudParams.z) return 1.0;
    vec2 xz = worldPos.xz + L.xz / L.y * (ubo.cloudParams.z - worldPos.y);
    // A heavy deck blocks most sunlight whatever the shadow setting
    float strength = max(ubo.cloudWind.z, 0.9 * smoothstep(0.6, 1.0, ubo.cloudParams.x));
    return 1.0 - CloudDensityAt(xz, 3) * ubo.cloudParams.y * strength;
}

// Clouds seen from the camera along dir: rgb = their light, a = how much of the sky they hide.
// skyAmbient: the light from the sky overhead (procedural sky or HDRI), at its full brightness.
vec4 SkyClouds(vec3 dir, vec3 sunDir, int octaves, vec3 skyAmbient) {
    float height = ubo.cloudParams.z - ubo.cameraPos.y;
    if (!CloudsActive() || dir.y <= 0.005 || height <= 0.0) return vec4(0.0);
    float distance = height / dir.y;
    vec2 xz = ubo.cameraPos.xz + dir.xz * distance;
    float density = CloudDensityAt(xz, octaves);
    if (density <= 0.001) return vec4(0.0);

    // Sunlit by day, faintly moonlit by night; thick cloud, and cloud toward the light, shades it
    vec3 moonDir = ubo.moonParams.w > 0.0 ? normalize(ubo.moonParams.xyz) : vec3(0.0, -1.0, 0.0);
    bool sunUp = sunDir.y > -0.04;
    vec3 L = sunUp ? sunDir : moonDir;
    vec2 towardLight = L.xz / max(L.y, 0.15) * ubo.cloudParams.w * 0.08;
    float shade = CloudDensityAt(xz + towardLight, max(octaves - 1, 1));
    float lit = mix(1.0, 0.35, density * 0.5 + shade * 0.5);
    // Heavy cover lets little light through: the deck is grey underneath
    float deck = smoothstep(0.5, 1.0, ubo.cloudParams.x);
    lit *= mix(1.0, 0.4, deck * density);
    // Thin edges glow when looking toward the light (forward scattering, the silver lining)
    float forward = pow(max(dot(dir, L), 0.0), 6.0) * (1.0 - density) * 2.0;
    vec3 light = sunUp ? SkySunColor(sunDir) * SkySunTransmittance(sunDir) * 1.1
                       : vec3(0.5, 0.6, 0.85) * 0.02 * ubo.moonParams.w * max(L.y, 0.0);
    vec3 color = light * (lit + forward) * SkyLightScale() + skyAmbient * 0.9 * mix(1.0, 0.6, density) * mix(1.0, 0.7, deck);

    // Far away the clouds thin out and fade into the horizon haze
    float fade = smoothstep(0.005, 0.12, dir.y) * exp(-distance / 60000.0);
    return vec4(color, density * ubo.cloudParams.y * fade);
}

// Approximate cosine-weighted sky light arriving at a surface facing `n`
vec3 SkyIrradiance(vec3 n, vec3 sunDir) {
    vec3 t = normalize(abs(n.y) < 0.99 ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    // Centre sample plus four samples tilted 60 degrees around the normal
    vec3 sum = SkyRadiance(n, sunDir, false) * 2.0;
    sum += SkyRadiance(normalize(n * 0.5 + t * 0.866), sunDir, false);
    sum += SkyRadiance(normalize(n * 0.5 - t * 0.866), sunDir, false);
    sum += SkyRadiance(normalize(n * 0.5 + b * 0.866), sunDir, false);
    sum += SkyRadiance(normalize(n * 0.5 - b * 0.866), sunDir, false);
    return sum / 6.0;
}

// Glossy sky reflection: sharp for smooth surfaces, fading to the irradiance as roughness rises
vec3 SkySpecular(vec3 R, float roughness, vec3 sunDir) {
    vec3 sharp = SkyRadiance(R, sunDir, false);
    if (roughness < 0.5) {
        vec4 clouds = SkyClouds(R, sunDir, 3, SkyRadiance(vec3(0.0, 1.0, 0.0), sunDir, false));
        sharp = mix(sharp, clouds.rgb, clouds.a);
    }
    vec3 blurred = SkyIrradiance(R, sunDir);
    return mix(sharp, blurred, smoothstep(0.0, 0.75, roughness));
}

// Karis' analytic split-sum environment BRDF (no lookup texture needed)
vec2 EnvBRDFScaleBias(float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 EnvBRDFApprox(vec3 F0, float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

#endif
