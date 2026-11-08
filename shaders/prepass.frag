#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"

// Depth + view-space normal + motion prepass (SSAO, TAA, motion blur). Vertex stage: prepass.vert.
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inCurrentClip;
layout(location = 3) in vec4 inPreviousClip;
layout(location = 4) flat in vec3 inMaterial; // alpha, cutoff, flags (1 = has albedo map, 2 = double-sided)
layout(location = 5) flat in vec2 inSurface;  // roughness, metallic

layout(location = 0) out vec4 outNormal;      // xyz = view-space normal, w = packed surface (see ssr.frag)
layout(location = 1) out vec4 outVelocity; // xy = NDC motion since last frame, w = 1 (written)

void main() {
    int flags = int(inMaterial.z + 0.5);
    bool doubleSided = (flags & 2) != 0;
    if (!gl_FrontFacing && !doubleSided) {
        discard;
    }

    // Cut-out holes (leaves, fences) must not occlude anything
    float cutoff = inMaterial.y;
    if (cutoff > 0.0) {
        float alpha = inMaterial.x;
        if ((flags & 1) != 0) {
            alpha *= texture(albedoMap, inTexCoord).a;
        }
        if (alpha < cutoff) {
            discard;
        }
    }

    vec3 worldNormal = normalize(gl_FrontFacing ? inWorldNormal : -inWorldNormal);
    // w packs metallic and roughness as 4-bit values (exact in a 16-bit float): 1 + metal * 16 + rough
    // 64: glTF packed metallic-roughness (roughness in G, metallic in B)
    const bool packedMR = (flags & 64) != 0;
    float roughness = ((flags & 16) != 0) ? inSurface.x * (packedMR ? texture(roughnessMap, inTexCoord).g : texture(roughnessMap, inTexCoord).r) : inSurface.x;
    float metallic  = ((flags & 32) != 0) ? inSurface.y * (packedMR ? texture(metallicMap, inTexCoord).b : texture(metallicMap, inTexCoord).r) : inSurface.y;
    float packedSurface = 1.0 + floor(clamp(metallic, 0.0, 1.0) * 15.0 + 0.5) * 16.0 +
                          floor(clamp(roughness, 0.0, 1.0) * 15.0 + 0.5);

    outNormal = vec4(normalize(mat3(ubo.view) * worldNormal), packedSurface);

    vec2 current = inCurrentClip.xy / inCurrentClip.w;
    vec2 previous = inPreviousClip.xy / inPreviousClip.w;
    outVelocity = vec4(current - previous, 0.0, 1.0);
}
