#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Depth of field: each pixel's blur size (circle of confusion) grows with its distance from the
// focus plane. Blur is gathered from a disc of samples; a sample only contributes if its own blur
// reaches this pixel, and background samples can't blur over an in-focus foreground.
layout(binding = 0) uniform sampler2D colorTexture; // HDR (linear sampler)
layout(binding = 1) uniform sampler2D depthTexture; // scene depth (nearest sampler)

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const int kTaps = 32;

// Distance from the camera (the cameras use an OpenGL-style depth range)
float LinearDepth(float d) {
    return pc.params0.y / (d + pc.params0.x);
}

// Blur radius in pixels
float CircleOfConfusion(float dist) {
    float focus = pc.params0.z;
    return clamp(pc.params0.w * abs(1.0 - focus / max(dist, 1e-3)), 0.0, 1.0) * pc.params1.x;
}

void main() {
    vec3 center = SanitizeHDR(texture(colorTexture, inUV).rgb);
    if (pc.params0.w <= 0.0) {
        outColor = vec4(center, 1.0);
        return;
    }

    vec2 texel = pc.params1.yz;
    float maxRadius = pc.params1.x;
    float centerDist = LinearDepth(texture(depthTexture, inUV).r);
    float centerCoC = CircleOfConfusion(centerDist);

    vec3 sum = center;
    float weightSum = 1.0;
    for (int i = 0; i < kTaps; ++i) {
        // Golden-angle spiral covering the disc evenly
        float t = (float(i) + 0.5) / float(kTaps);
        float r = sqrt(t) * maxRadius;
        float a = float(i) * 2.3999632;
        vec2 offset = vec2(cos(a), sin(a)) * r;
        vec2 uv = inUV + offset * texel;

        float sampleDist = LinearDepth(texture(depthTexture, uv).r);
        float sampleCoC = CircleOfConfusion(sampleDist);
        // Something behind this pixel can't blur further onto it than this pixel's own blur
        if (sampleDist > centerDist) sampleCoC = min(sampleCoC, centerCoC);

        float weight = smoothstep(r - 1.0, r + 1.0, sampleCoC);
        sum += SanitizeHDR(texture(colorTexture, uv).rgb) * weight;
        weightSum += weight;
    }
    outColor = vec4(sum / weightSum, 1.0);
}
