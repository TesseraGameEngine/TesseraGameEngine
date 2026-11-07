// Ocean surface (VulkanPipeline::DrawOcean): a radial grid that follows the camera, moved by a
// sum of Gerstner waves. Shared by ocean.vert and ocean.frag. Must match OceanPushConstants in
// VulkanPipeline.cpp (the push block is the same 128 bytes as PushConstantData).
#ifndef OCEAN_COMMON_GLSL
#define OCEAN_COMMON_GLSL

layout(push_constant) uniform OceanParams {
    vec4 surface;      // x = water height (m), y = wave height (m), z = choppiness (0..1), w = time (s)
    vec4 wind;         // xy = wind direction, z = longest wavelength (m), w = speed multiplier
    vec4 bounds;       // xy = minimum world x / z, zw = maximum world x / z
    vec4 shallowColor; // rgb = tint of shallow water, a = clarity (metres you can see through)
    vec4 deepColor;    // rgb = colour of deep water, a = whitecap amount (0..1)
    vec4 foamParams;   // x = shore foam width (m), y = 1 when this view has scene depth, z = surface roughness
    vec4 reserved0;
    vec4 reserved1;
} ocean;

const int kOceanSwellWaves = 8;  // the waves that move the surface
const int kOceanRippleWaves = 4; // small wind ripples, shading only

// Wave i: direction, wavenumber (2 pi / wavelength), amplitude, steepness, angular speed, phase.
// Swell waves get shorter by a fixed ratio and spread around the wind; together their amplitudes
// add up to half the wave height. Ripples have fixed short wavelengths.
void OceanWave(int i, out vec2 dir, out float k, out float amplitude, out float steepness,
               out float omega, out float phase) {
    const float spread[12] = float[](0.0, 0.45, -0.38, 0.9, -0.75, 0.2, -1.15, 1.35, 0.6, -0.5, 1.1, -0.95);
    float c = cos(spread[i]);
    float s = sin(spread[i]);
    vec2 wind = normalize(ocean.wind.xy + vec2(1e-5, 0.0));
    dir = vec2(c * wind.x - s * wind.y, s * wind.x + c * wind.y);
    if (i < kOceanSwellWaves) {
        const float ratio = 0.62;
        const float ratioSum = 2.5679; // 1 + ratio + ... + ratio^7
        float wavelength = max(ocean.wind.z, 0.5) * pow(ratio, float(i));
        k = 6.2831853 / wavelength;
        amplitude = max(ocean.surface.y, 0.0) * 0.5 * pow(ratio, float(i)) / ratioSum;
        steepness = clamp(ocean.surface.z, 0.0, 1.0) / max(k * amplitude * float(kOceanSwellWaves), 1e-4);
    } else {
        const float rippleLengths[4] = float[](1.7, 1.1, 0.73, 0.47);
        float wavelength = rippleLengths[i - kOceanSwellWaves];
        k = 6.2831853 / wavelength;
        amplitude = wavelength * 0.02;
        steepness = 0.5 / max(k * amplitude * float(kOceanRippleWaves), 1e-4);
    }
    omega = sqrt(9.81 * k) * ocean.wind.w;
    phase = float(i) * 1.7;
}

// Waves that span too few grid cells or pixels (footprint, metres) can't be shown without
// aliasing into patterns: they fade out between 8 and 4 footprints per wavelength
float OceanWaveFade(float k, float footprint) {
    return smoothstep(4.0, 8.0, (6.2831853 / k) / max(footprint, 1e-4));
}

// How far the waves move the point that rests at p (horizontal position on the calm surface)
vec3 OceanDisplacement(vec2 p, float footprint) {
    vec3 offset = vec3(0.0);
    for (int i = 0; i < kOceanSwellWaves; ++i) {
        vec2 dir;
        float k, amplitude, steepness, omega, phase;
        OceanWave(i, dir, k, amplitude, steepness, omega, phase);
        float a = amplitude * OceanWaveFade(k, footprint);
        float theta = k * dot(dir, p) - omega * ocean.surface.w + phase;
        offset.xz += steepness * a * dir * cos(theta);
        offset.y += a * sin(theta);
    }
    return offset;
}

// Surface normal at p (with ripples), and how compressed the longer waves are there: 1 = flat,
// near 0 = bunched up into a sharp crest (where whitecaps form)
vec3 OceanNormal(vec2 p, float footprint, out float compression) {
    vec3 n = vec3(0.0, 1.0, 0.0);
    compression = 1.0;
    for (int i = 0; i < kOceanSwellWaves + kOceanRippleWaves; ++i) {
        vec2 dir;
        float k, amplitude, steepness, omega, phase;
        OceanWave(i, dir, k, amplitude, steepness, omega, phase);
        float wa = k * amplitude * OceanWaveFade(k, footprint);
        float theta = k * dot(dir, p) - omega * ocean.surface.w + phase;
        float s = sin(theta);
        float c = cos(theta);
        n.x -= dir.x * wa * c;
        n.z -= dir.y * wa * c;
        n.y -= steepness * wa * s;
        // Only the longer waves make whitecaps; short ones would only speckle the surface
        if (i < 5) compression -= steepness * wa * s * 1.6;
    }
    return normalize(n);
}

#endif
