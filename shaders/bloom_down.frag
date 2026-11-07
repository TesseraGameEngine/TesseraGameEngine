#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare").
// The first pass uses a Karis average so single very bright pixels don't flicker as bloom.
layout(binding = 0) uniform sampler2D srcTexture;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

float KarisWeight(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

vec3 Tap(vec2 uv) {
    return SanitizeHDR(texture(srcTexture, uv).rgb);
}

void main() {
    vec2 t = pc.params0.xy;

    vec3 a = Tap(inUV + t * vec2(-2.0,  2.0));
    vec3 b = Tap(inUV + t * vec2( 0.0,  2.0));
    vec3 c = Tap(inUV + t * vec2( 2.0,  2.0));
    vec3 d = Tap(inUV + t * vec2(-2.0,  0.0));
    vec3 e = Tap(inUV);
    vec3 f = Tap(inUV + t * vec2( 2.0,  0.0));
    vec3 g = Tap(inUV + t * vec2(-2.0, -2.0));
    vec3 h = Tap(inUV + t * vec2( 0.0, -2.0));
    vec3 i = Tap(inUV + t * vec2( 2.0, -2.0));
    vec3 j = Tap(inUV + t * vec2(-1.0,  1.0));
    vec3 k = Tap(inUV + t * vec2( 1.0,  1.0));
    vec3 l = Tap(inUV + t * vec2(-1.0, -1.0));
    vec3 m = Tap(inUV + t * vec2( 1.0, -1.0));

    vec3 result;
    if (pc.params0.z > 0.5) {
        // Five 2x2 boxes, each weighted by its brightness
        vec3 g0 = (a + b + d + e) * 0.25;
        vec3 g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25;
        vec3 g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;
        float w0 = KarisWeight(g0) * 0.125;
        float w1 = KarisWeight(g1) * 0.125;
        float w2 = KarisWeight(g2) * 0.125;
        float w3 = KarisWeight(g3) * 0.125;
        float w4 = KarisWeight(g4) * 0.5;
        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    } else {
        result  = e * 0.125;
        result += (a + c + g + i) * 0.03125;
        result += (b + d + f + h) * 0.0625;
        result += (j + k + l + m) * 0.125;
    }

    // Guard against NaN/Inf from the scene spreading through the whole bloom chain
    result = clamp(result, vec3(0.0), vec3(65000.0));
    outColor = vec4(result, 1.0);
}
