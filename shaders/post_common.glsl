// Push constants shared by all post-processing passes. Must match PostPushConstants in VulkanOffscreen.h
#ifndef POST_COMMON_GLSL
#define POST_COMMON_GLSL

layout(push_constant) uniform PostPushConstants {
    vec4 params0; // bloom: xy = source texel size, z = 1 on the first downsample (Karis average), w = upsample radius
                  // composite: x = exposure, y = bloom strength, z = 1 for ACES / 0 for Reinhard, w = 1 / bloom levels
                  // ssao: projection terms (P[0][0], P[1][1], P[2][2], P[3][2])
                  // dof: x = P[2][2], y = P[3][2] (linearise depth), z = focus distance (m), w = aperture (0 = off)
                  // motion blur: x = shutter amount (0 = off), y = max blur length (px), zw = texel size
                  // exposure: x = adaptation blend this frame, y = min exposure, z = max exposure, w = 1 if history valid
    vec4 params1; // ssao: x = radius (m), y = depth bias (m), z = unused, w = contrast power
                  // ssao blur, fxaa: xy = texel size
                  // taa: xy = this frame's jitter in NDC, z = 1 when the history is usable, w = history weight
                  // dof: x = max blur radius (px), yz = texel size
                  // motion blur: x = 1 when the velocity buffer is valid this frame
                  // composite: x = contrast, y = saturation, z = temperature, w = tint
    vec4 params2; // ssao: xy = texel size
                  // taa: xy = texel size, z = 1 when the velocity buffer is valid this frame
                  // composite: x = vignette, y = film grain, z = time (s), w = sharpening
    mat4 matrix;  // taa, motion blur: current (unjittered) NDC + depth -> previous frame's NDC
                  // composite: [0] = (lens flare, 1 = auto exposure, texel size xy),
                  //            [1] = (LUT strength (0 = off), LUT size, chromatic aberration, unused)
                  // ssr: see ssr.frag
} pc;

// Replaces NaN/Inf with black so one bad pixel can't spread through the bloom chain
vec3 SanitizeHDR(vec3 c) {
    bvec3 bad = bvec3(isnan(c.x) || isinf(c.x), isnan(c.y) || isinf(c.y), isnan(c.z) || isinf(c.z));
    return min(mix(c, vec3(0.0), bad), vec3(60000.0));
}

#endif
