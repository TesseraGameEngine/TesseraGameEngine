#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Auto exposure: measures the scene's average (log) brightness from the smallest bloom level and
// eases the exposure toward the value that maps it to mid-grey, like an eye adapting.
// Renders to a 1x1 target; the composite reads it.
layout(binding = 0) uniform sampler2D sceneSmall;       // smallest bloom downsample (linear sampler)
layout(binding = 1) uniform sampler2D previousExposure; // last frame's adapted exposure (1x1)

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outExposure;

void main() {
    // 8x8 samples, with the centre of the screen counting more
    float logSum = 0.0;
    float weightSum = 0.0;
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            vec2 uv = (vec2(x, y) + 0.5) / 8.0;
            vec3 c = SanitizeHDR(textureLod(sceneSmall, uv, 0.0).rgb);
            float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
            float weight = 1.0 - 0.6 * smoothstep(0.2, 0.7, length(uv - 0.5));
            logSum += log(max(luma, 1e-4)) * weight;
            weightSum += weight;
        }
    }
    float averageLuma = exp(logSum / weightSum);
    // Key value: brightness the average is mapped to. 0.3 keeps a normally lit scene close to
    // exposure 1 with this tone mapper, while dark and very bright scenes are pulled toward it.
    float target = clamp(0.3 / max(averageLuma, 1e-4), pc.params0.y, pc.params0.z);

    float exposure = target;
    if (pc.params0.w > 0.5) {
        float previous = texture(previousExposure, vec2(0.5)).r;
        if (!(isnan(previous) || isinf(previous)) && previous > 0.0) {
            exposure = mix(previous, target, pc.params0.x);
        }
    }
    outExposure = vec4(exposure, 0.0, 0.0, 1.0);
}
