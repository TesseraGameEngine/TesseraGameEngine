#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Final pass: HDR scene (+ sharpening) + bloom + lens flare -> exposure -> tone mapping ->
// colour grading -> gamma -> LUT -> vignette and film grain, into the display image.
layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D bloomTexture;
layout(binding = 2) uniform sampler2D exposureTexture; // 1x1 adapted exposure (auto exposure)
layout(binding = 3) uniform sampler2D lutTexture;      // colour LUT as a strip: N slices of N x N (blue = slice)

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 ToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float Hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Ghosts and a halo reflected through the screen centre, from the bright parts of the bloom image
vec3 LensFlare(vec2 uv, float bloomScale) {
    vec2 toCenter = vec2(0.5) - uv;
    vec3 flare = vec3(0.0);
    const vec3 tints[4] = vec3[](vec3(1.0, 0.6, 0.4), vec3(0.5, 0.8, 1.0), vec3(0.8, 1.0, 0.6), vec3(1.0, 0.7, 1.0));
    for (int i = 0; i < 4; ++i) {
        vec2 ghostUV = uv + toCenter * (0.55 + 0.45 * float(i)) * 2.0;
        vec3 c = texture(bloomTexture, ghostUV).rgb * bloomScale;
        float falloff = 1.0 - smoothstep(0.0, 0.75, length(ghostUV - vec2(0.5)));
        flare += max(c - vec3(1.0), vec3(0.0)) * tints[i] * falloff;
    }
    // Ring-shaped halo at a fixed distance from the centre
    vec2 haloUV = uv + normalize(toCenter + vec2(1e-5)) * 0.35;
    float haloFalloff = smoothstep(0.2, 0.5, length(haloUV - vec2(0.5)));
    flare += max(texture(bloomTexture, haloUV).rgb * bloomScale - vec3(1.0), vec3(0.0)) * vec3(0.9, 0.8, 1.0) * haloFalloff * 0.5;
    return flare;
}

// Display-space colour lookup. The strip is N*N wide and N tall: x = red within slice + blue slice,
// y = green. Two slices are blended for blue; textureLod keeps the full-resolution level.
vec3 ApplyLUT(vec3 c, float size) {
    c = clamp(c, 0.0, 1.0);
    float slice = c.b * (size - 1.0);
    float slice0 = floor(slice);
    float slice1 = min(slice0 + 1.0, size - 1.0);
    vec2 uv = vec2((c.r * (size - 1.0) + 0.5) / (size * size), (c.g * (size - 1.0) + 0.5) / size);
    vec3 a = textureLod(lutTexture, uv + vec2(slice0 / size, 0.0), 0.0).rgb;
    vec3 b = textureLod(lutTexture, uv + vec2(slice1 / size, 0.0), 0.0).rgb;
    return mix(a, b, slice - slice0);
}

void main() {
    // Under water the view wobbles gently (matrix[1].w = 1 when the camera is under water). Half
    // in the water (+-2: which side), only below the waterline (matrix[2..3]: its height at 8
    // points across), and the line itself shows as a thin dark, blurred meniscus.
    vec2 uv = inUV;
    float mode = pc.matrix[1].w;
    bool wobble = mode > 0.5 && mode < 1.5;
    float meniscus = 0.0;
    if (abs(mode) > 1.5) {
        float x = clamp(inUV.x, 0.0, 1.0) * 7.0;
        int i = min(int(floor(x)), 6);
        float a = pc.matrix[2 + i / 4][i % 4];
        float b = pc.matrix[2 + (i + 1) / 4][(i + 1) % 4];
        float lineUV = mix(a, b, x - float(i)) * 0.5 + 0.5;
        float offset = inUV.y - lineUV;
        wobble = mode > 0.0 ? offset > 0.0 : offset < 0.0;
        meniscus = smoothstep(0.006, 0.0, abs(offset));
    }
    if (wobble) {
        uv += vec2(sin(inUV.y * 22.0 + pc.params2.z * 2.1), cos(inUV.x * 18.0 + pc.params2.z * 1.7)) * 0.003;
    }
    uv.y += meniscus * 0.004 * sin(inUV.x * 90.0 + pc.params2.z * 3.0);
    vec2 texel = pc.matrix[0].zw;
    vec3 scene = SanitizeHDR(texture(sceneTexture, uv).rgb);

    // Chromatic aberration: red and blue land slightly apart toward the screen edges, like a cheap lens
    float aberration = pc.matrix[1].z;
    if (aberration > 0.0) {
        vec2 offset = (uv - 0.5) * aberration * 0.012;
        scene.r = SanitizeHDR(texture(sceneTexture, uv + offset).rgb).r;
        scene.b = SanitizeHDR(texture(sceneTexture, uv - offset).rgb).b;
    }

    // Sharpening (offsets TAA's slight softness): push the centre away from its neighbours
    float sharpen = pc.params2.w;
    if (sharpen > 0.0) {
        vec3 neighbours = SanitizeHDR(texture(sceneTexture, uv + vec2(texel.x, 0.0)).rgb) +
                          SanitizeHDR(texture(sceneTexture, uv - vec2(texel.x, 0.0)).rgb) +
                          SanitizeHDR(texture(sceneTexture, uv + vec2(0.0, texel.y)).rgb) +
                          SanitizeHDR(texture(sceneTexture, uv - vec2(0.0, texel.y)).rgb);
        scene = max(scene + (scene * 4.0 - neighbours) * sharpen * 0.25, vec3(0.0));
    }

    // Mip 0 holds the sum of every bloom level; average it back to scene brightness
    float bloomScale = pc.params0.w;
    vec3 bloom = SanitizeHDR(texture(bloomTexture, uv).rgb) * bloomScale;

    // Energy-conserving blend: bloom spreads a small share of all light, so bright
    // areas (sun, emissives) glow far more than ordinary surfaces.
    float bloomMix = clamp(pc.params0.y * 0.12, 0.0, 1.0);
    vec3 color = mix(scene, bloom, bloomMix);

    float flareAmount = pc.matrix[0].x;
    if (flareAmount > 0.0) {
        color += LensFlare(uv, bloomScale) * flareAmount;
    }

    // Exposure: manual compensation, times the adapted value when auto exposure is on
    float exposure = pc.params0.x;
    if (pc.matrix[0].y > 0.5) {
        exposure *= texture(exposureTexture, vec2(0.5)).r;
    }
    color *= exposure;

    if (pc.params0.z > 0.5) {
        color = ToneMapACES(color);
    } else {
        color = color / (color + vec3(1.0));
    }

    // Colour grading (white balance, saturation) in linear display space
    float temperature = pc.params1.z;
    float tint = pc.params1.w;
    color *= vec3(1.0 + 0.3 * temperature + 0.1 * tint, 1.0 - 0.2 * tint, 1.0 - 0.3 * temperature + 0.1 * tint);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(mix(vec3(luma), color, pc.params1.y), vec3(0.0));

    color = pow(color, vec3(1.0 / 2.2));

    // Contrast around mid-grey, in perceptual (gamma) space
    color = clamp((color - 0.5) * pc.params1.x + 0.5, 0.0, 1.0);

    // Colour LUT ("look" made in Photoshop, DaVinci Resolve etc.), blended by strength
    float lutStrength = pc.matrix[1].x;
    if (lutStrength > 0.0) {
        color = mix(color, ApplyLUT(color, pc.matrix[1].y), lutStrength);
    }

    // Vignette: darken toward the corners
    float vignette = pc.params2.x;
    if (vignette > 0.0) {
        float d = length((uv - 0.5) * vec2(1.0, 0.8)) * 1.4;
        color *= 1.0 - vignette * smoothstep(0.35, 1.0, d);
    }

    // Film grain: animated per-pixel noise
    float grain = pc.params2.y;
    if (grain > 0.0) {
        float n = Hash12(uv / texel + fract(pc.params2.z) * 1000.0) - 0.5;
        color = clamp(color + n * grain * 0.1, 0.0, 1.0);
    }

    outColor = vec4(color, 1.0);
}
