// Shadow-map pass (shadow.vert / shadow.frag): per-draw push constants.
// Must match ShadowPushConstants in src/Renderer/VulkanShadowMap.h
#ifndef SHADOW_COMMON_GLSL
#define SHADOW_COMMON_GLSL

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightModelViewProj; // light view-projection * model (or the light alone when instanced)
    vec4 color;              // material colour (alpha used for cutout)
    float alphaCutoff;       // > 0: texels with alpha below this cast no shadow
    int textureFlags;        // TEX_ALBEDO bit: sample the albedo map's alpha
    float instanced;         // 1: models come from the instance buffer
    float wind;              // 1: sways with the wind (foliage)
} pc;

#endif
