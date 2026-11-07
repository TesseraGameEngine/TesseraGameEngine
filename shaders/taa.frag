#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Temporal anti-aliasing resolve (HDR, before bloom). Each frame is rendered with a different
// sub-pixel jitter; this blends it with the accumulated history, reprojected using depth and the
// camera's motion. History is clipped to the current pixel's neighbourhood colour range so
// anything that moved or changed can't leave a ghost trail.
layout(binding = 0) uniform sampler2D currentTexture; // this frame's HDR scene (linear sampler)
layout(binding = 1) uniform sampler2D historyTexture; // last frame's resolved result (linear sampler)
layout(binding = 2) uniform sampler2D depthTexture;   // this frame's scene depth (nearest sampler)
layout(binding = 3) uniform sampler2D velocityTexture; // prepass motion: xy = NDC motion this frame, w = 1 where written

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 RGBToYCoCg(vec3 c) {
    return vec3( 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                 0.5  * c.r             - 0.5  * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float Luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Pulls a history colour toward the centre of the neighbourhood box until it lies inside it
vec3 ClipToBox(vec3 history, vec3 boxMin, vec3 boxMax) {
    vec3 center = 0.5 * (boxMax + boxMin);
    vec3 extents = max(0.5 * (boxMax - boxMin), vec3(1e-5));
    vec3 offset = history - center;
    vec3 ratio = abs(offset / extents);
    float maxRatio = max(ratio.x, max(ratio.y, ratio.z));
    return (maxRatio > 1.0) ? center + offset / maxRatio : history;
}

// Bicubic (Catmull-Rom) history lookup built from 9 bilinear taps. Sharper than a single
// bilinear tap, which would soften the image a little more every frame.
vec3 SampleHistory(vec2 uv, vec2 texel) {
    vec2 size = 1.0 / texel;
    vec2 pixel = uv * size;
    vec2 base = floor(pixel - 0.5) + 0.5;
    vec2 f = pixel - base;

    // Catmull-Rom weights for the 4 texels on each axis
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // The middle two texels are fetched together with one bilinear tap
    vec2 w12 = w1 + w2;
    vec2 p0 = (base - 1.0) * texel;
    vec2 p12 = (base + w2 / w12) * texel;
    vec2 p3 = (base + 2.0) * texel;

    vec3 result =
        (texture(historyTexture, vec2(p0.x,  p0.y)).rgb  * w0.x  +
         texture(historyTexture, vec2(p12.x, p0.y)).rgb  * w12.x +
         texture(historyTexture, vec2(p3.x,  p0.y)).rgb  * w3.x) * w0.y +
        (texture(historyTexture, vec2(p0.x,  p12.y)).rgb * w0.x  +
         texture(historyTexture, vec2(p12.x, p12.y)).rgb * w12.x +
         texture(historyTexture, vec2(p3.x,  p12.y)).rgb * w3.x) * w12.y +
        (texture(historyTexture, vec2(p0.x,  p3.y)).rgb  * w0.x  +
         texture(historyTexture, vec2(p12.x, p3.y)).rgb  * w12.x +
         texture(historyTexture, vec2(p3.x,  p3.y)).rgb  * w3.x) * w3.y;
    return max(result, vec3(0.0)); // the negative lobes can undershoot
}

void main() {
    vec2 texel = pc.params2.xy;
    vec3 current = SanitizeHDR(texture(currentTexture, inUV).rgb);

    if (pc.params1.z < 0.5) {
        outColor = vec4(current, 1.0); // no usable history (first frame, resize, TAA just enabled)
        return;
    }

    // Where this pixel's surface was last frame: from the prepass motion where it covers the
    // pixel (handles moving objects), otherwise from depth and the camera's motion. The pixel
    // was rendered jittered, so remove the jitter before reprojecting.
    vec2 ndc = inUV * 2.0 - 1.0 - pc.params1.xy;
    vec2 previousUV;
    bool previousValid = true;
    vec4 velocity = texture(velocityTexture, inUV);
    if (pc.params2.z > 0.5 && velocity.w > 0.5) {
        previousUV = (ndc - velocity.xy) * 0.5 + 0.5;
    } else {
        float depth = texture(depthTexture, inUV).r;
        vec4 previous = pc.matrix * vec4(ndc, depth, 1.0);
        previousValid = previous.w > 0.0;
        previousUV = (previous.xy / max(previous.w, 1e-6)) * 0.5 + 0.5;
    }
    if (!previousValid || any(lessThan(previousUV, vec2(0.0))) || any(greaterThan(previousUV, vec2(1.0)))) {
        outColor = vec4(current, 1.0); // came into view this frame
        return;
    }

    // Neighbourhood statistics in YCoCg (separates brightness from colour, so the box is tighter)
    vec3 mean = vec3(0.0);
    vec3 meanSq = vec3(0.0);
    vec3 boxMin = vec3(1e9);
    vec3 boxMax = vec3(-1e9);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec3 c = RGBToYCoCg(SanitizeHDR(texture(currentTexture, inUV + vec2(x, y) * texel).rgb));
            mean += c;
            meanSq += c * c;
            boxMin = min(boxMin, c);
            boxMax = max(boxMax, c);
        }
    }
    mean /= 9.0;
    vec3 sigma = sqrt(max(meanSq / 9.0 - mean * mean, vec3(0.0)));
    // Variance clipping: a box around the mean, no larger than the actual min/max
    vec3 clipMin = max(boxMin, mean - 1.25 * sigma);
    vec3 clipMax = min(boxMax, mean + 1.25 * sigma);

    vec3 history = SanitizeHDR(SampleHistory(previousUV, texel));
    history = YCoCgToRGB(ClipToBox(RGBToYCoCg(history), clipMin, clipMax));

    // Weight each sample by 1 / (1 + luma) so a single very bright pixel can't flicker
    float historyWeight = pc.params1.w;
    float wCurrent = (1.0 - historyWeight) / (1.0 + Luma(current));
    float wHistory = historyWeight / (1.0 + Luma(history));
    vec3 result = (current * wCurrent + history * wHistory) / max(wCurrent + wHistory, 1e-5);

    outColor = vec4(result, 1.0);
}
