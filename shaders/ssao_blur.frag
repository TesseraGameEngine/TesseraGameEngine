#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// 5x5 depth-aware blur of the raw SSAO, so the per-pixel noise disappears without
// smearing occlusion across object edges.
layout(binding = 0) uniform sampler2D aoTexture;
layout(binding = 1) uniform sampler2D depthTexture; // nearest sampler

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outAO;

void main() {
    vec2 texel = pc.params1.xy;
    float centerDepth = texture(depthTexture, inUV).r;
    if (centerDepth >= 1.0) {
        outAO = vec4(1.0);
        return;
    }

    float sum = 0.0;
    float weightSum = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 uv = inUV + vec2(x, y) * texel;
            float d = texture(depthTexture, uv).r;
            // Depth is non-linear, so compare relative differences
            float w = exp(-abs(d - centerDepth) / max(centerDepth * 0.002, 1e-5));
            sum += texture(aoTexture, uv).r * w;
            weightSum += w;
        }
    }
    outAO = vec4(sum / max(weightSum, 1e-5));
}
