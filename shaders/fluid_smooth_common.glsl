// Screen-space fluid smoothing, shared by fluid_smooth.frag (first pass) and
// fluid_smooth_wide.frag (second pass, FLUID_SMOOTH_SECOND)

layout(binding = 25) uniform sampler2D fluidDistance;    // particle spheres (r: distance, 0 = no fluid; g: radius)
#ifdef FLUID_SMOOTH_SECOND
layout(binding = 33) uniform sampler2D fluidFirstSmooth; // the first pass's result (read by the second)
#endif

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outDistance;

// Each pass is its own shader (FLUID_SMOOTH_SECOND), so the first never refers to the image it writes
float Source(ivec2 q, bool second) {
#ifdef FLUID_SMOOTH_SECOND
    return texelFetch(fluidFirstSmooth, q, 0).r;
#else
    return texelFetch(fluidDistance, q, 0).r;
#endif
}

// Edge-preserving blur of the sphere surfaces into one smooth liquid surface. The filter covers
// a few particle radii on screen; neighbours much nearer or further (another sheet of fluid,
// or the edge) count less, so separate parts don't melt together. The second pass blurs the
// first's result again, more widely and more gently, which irons out the ridges rows of
// particles pressed against a wall leave behind.
void main() {
#ifdef FLUID_SMOOTH_SECOND
    const bool second = true;
#else
    const bool second = false;
#endif
    ivec2 size = textureSize(fluidDistance, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = Source(p, second);
    if (d <= 0.0) {
        outDistance = 0.0;
        return;
    }
    // Each pixel's own particle size (volumes can differ)
    float radius = max(texelFetch(fluidDistance, p, 0).g, 1e-4);
    // About three particle diameters (the second pass: five) melt the spheres into one surface
    float span = second ? 10.0 : 6.0;
    float radiusPixels = clamp(radius * span * abs(ubo.proj[1][1]) * 0.5 * float(size.y) / d, 1.0, second ? 64.0 : 48.0);
    float stepPixels = radiusPixels / 6.0;
    float falloff = radius * (second ? 4.0 : 6.0);
    // The surface's slope across the screen (a wall seen at an angle gets further away down the
    // screen): neighbours are compared with the tilted plane, not the flat depth, so slanted
    // surfaces smooth in every direction instead of only along their rows
    vec2 slope = vec2(0.0);
    {
        float span2 = max(stepPixels * 2.0, 1.0);
        ivec2 ox = ivec2(round(span2), 0), oy = ivec2(0, round(span2));
        float xl = Source(clamp(p - ox, ivec2(0), size - 1), second), xr = Source(clamp(p + ox, ivec2(0), size - 1), second);
        float yd = Source(clamp(p - oy, ivec2(0), size - 1), second), yu = Source(clamp(p + oy, ivec2(0), size - 1), second);
        // One-sided at the fluid's edges; none across a jump to another sheet of fluid
        float gx = (xl > 0.0 && xr > 0.0) ? (xr - xl) * 0.5 : (xr > 0.0 ? xr - d : (xl > 0.0 ? d - xl : 0.0));
        float gy = (yd > 0.0 && yu > 0.0) ? (yu - yd) * 0.5 : (yu > 0.0 ? yu - d : (yd > 0.0 ? d - yd : 0.0));
        slope = vec2(abs(gx) < falloff * 2.0 ? gx : 0.0, abs(gy) < falloff * 2.0 ? gy : 0.0) / span2;
    }
    float sum = 0.0;
    float weights = 0.0;
    for (int y = -6; y <= 6; ++y) {
        for (int x = -6; x <= 6; ++x) {
            vec2 offset = round(vec2(x, y) * stepPixels);
            ivec2 q = clamp(p + ivec2(offset), ivec2(0), size - 1);
            float s = Source(q, second);
            if (s <= 0.0) continue;
            float spatial = exp(-float(x * x + y * y) / 40.0);
            float planeDepth = d + dot(slope, offset);
            float dz = (s - planeDepth) / falloff;
            float w = spatial * exp(-dz * dz);
            s -= dot(slope, offset); // averaged on the plane, so the slope itself stays
            sum += s * w;
            weights += w;
        }
    }
    outDistance = sum / max(weights, 1e-6);
}
