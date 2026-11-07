#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Motion blur: averages the image along each pixel's screen-space motion. Motion comes from the
// prepass velocity buffer (camera and moving objects) where available, otherwise from depth and
// the camera's movement alone.
layout(binding = 0) uniform sampler2D colorTexture;    // HDR (linear sampler)
layout(binding = 1) uniform sampler2D depthTexture;    // scene depth (nearest sampler)
layout(binding = 2) uniform sampler2D velocityTexture; // xy = NDC motion this frame, w = 1 where written

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const int kSamples = 10;

void main() {
    vec3 center = SanitizeHDR(texture(colorTexture, inUV).rgb);
    float shutter = pc.params0.x;
    if (shutter <= 0.0) {
        outColor = vec4(center, 1.0);
        return;
    }

    vec2 ndc = inUV * 2.0 - 1.0;
    vec2 motionNdc;
    vec4 velocity = texture(velocityTexture, inUV);
    if (pc.params1.x > 0.5 && velocity.w > 0.5) {
        motionNdc = velocity.xy;
    } else {
        vec4 previous = pc.matrix * vec4(ndc, texture(depthTexture, inUV).r, 1.0);
        motionNdc = (previous.w > 0.0) ? ndc - previous.xy / previous.w : vec2(0.0);
    }

    // Screen-space length, scaled by how long the shutter is open and capped
    vec2 texel = pc.params0.zw;
    vec2 motionUV = motionNdc * 0.5 * shutter;
    float lengthPx = length(motionUV / texel);
    float maxPx = pc.params0.y;
    if (lengthPx > maxPx) motionUV *= maxPx / lengthPx;
    if (lengthPx < 0.5) {
        outColor = vec4(center, 1.0);
        return;
    }

    vec3 sum = vec3(0.0);
    for (int i = 0; i < kSamples; ++i) {
        float t = (float(i) + 0.5) / float(kSamples) - 0.5;
        sum += SanitizeHDR(texture(colorTexture, inUV + motionUV * t).rgb);
    }
    outColor = vec4(sum / float(kSamples), 1.0);
}
