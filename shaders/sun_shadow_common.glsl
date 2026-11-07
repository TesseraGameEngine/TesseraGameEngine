// Sun shadows: soft (PCF) lookups in the cascaded shadow map, shared by the scene (shader.frag)
// and the ocean. The includer declares shadowMap (binding 7, sampler2DArrayShadow).
#ifndef SUN_SHADOW_COMMON_GLSL
#define SUN_SHADOW_COMMON_GLSL

// Soft (PCF) shadow lookup in one cascade. Returns -1 when the point lies outside it;
// edge = how close the point is to the cascade's border (0 = centre, 1 = border).
float SampleCascade(int cascade, vec3 worldPos, vec3 N, float NdotL, float lightBias, out float edge) {
    // Push the lookup off the surface along its normal (more at grazing angles) to avoid acne;
    // the push is sized to this cascade's texels
    float normalOffset = ubo.shadowCascadeTexel[cascade] * 1.5;
    vec3 offsetPos = worldPos + N * normalOffset * (1.0 - NdotL + 0.25);
    vec4 shadowClip = ubo.shadowMatrices[cascade] * vec4(offsetPos, 1.0);
    vec3 proj = shadowClip.xyz / shadowClip.w;

    edge = max(abs(proj.x), abs(proj.y));
    if (edge >= 1.0 || proj.z <= 0.0 || proj.z >= 1.0) {
        return -1.0;
    }

    vec2 uv = proj.xy * 0.5 + 0.5;
    float bias = max(lightBias * (1.0 - NdotL), lightBias * 0.1) * 0.1;
    float compareDepth = proj.z - bias;

    // 3x3 grid of hardware-filtered taps = soft 4x4-texel penumbra
    float texel = ubo.shadowParams.z;
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            lit += texture(shadowMap, vec4(uv + vec2(x, y) * texel * 1.5, float(cascade), compareDepth));
        }
    }
    return lit / 9.0;
}

// 1.0 = fully lit, 0.0 = fully in shadow. Only valid for the light at ubo.shadowParams.y.
// Uses the finest cascade that covers the point (chosen by coverage rather than camera
// distance, so mirror views work too) and blends into the next one near its border.
float SampleShadow(vec3 worldPos, vec3 N, vec3 L, float lightBias) {
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    int cascadeCount = int(ubo.shadowParams.w + 0.5);

    for (int c = 0; c < cascadeCount; ++c) {
        float edge;
        float lit = SampleCascade(c, worldPos, N, NdotL, lightBias, edge);
        if (lit < 0.0) {
            continue;
        }
        float blend = smoothstep(0.85, 1.0, edge);
        if (blend > 0.0) {
            // Past the last cascade, fade to lit instead of cutting off sharply
            float nextLit = 1.0;
            if (c + 1 < cascadeCount) {
                float nextEdge;
                float next = SampleCascade(c + 1, worldPos, N, NdotL, lightBias, nextEdge);
                if (next >= 0.0) nextLit = next;
            }
            lit = mix(lit, nextLit, blend);
        }
        return lit;
    }
    return 1.0;
}

#endif
