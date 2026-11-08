// Per-draw material push constants. Must match PushConstantData, TextureFlagBits and
// MaterialFlagBits in src/Renderer/VulkanBuffer.h
#ifndef COMMON_MATERIAL_GLSL
#define COMMON_MATERIAL_GLSL

const int TEX_ALBEDO    = 1;
const int TEX_NORMAL    = 2;
const int TEX_METALLIC  = 4;
const int TEX_ROUGHNESS = 8;
const int TEX_PACKED_MR = 128; // glTF packed metallic-roughness: roughness in G, metallic in B
const int TEX_AO        = 16;
const int TEX_EMISSIVE  = 32;
const int TEX_HEIGHT    = 64;
const int MAT_DOUBLE_SIDED = 1;
const int MAT_TRANSPARENT  = 2; // output premultiplied alpha (sorted transparent pass)
const int MAT_MIRROR       = 4; // show this view's planar reflection
const int MAT_INSTANCED    = 8; // model matrix comes from the instance buffer (instancing_common.glsl)
const int MAT_VERTEX_COLOR = 16; // multiply the base colour by the mesh's vertex colours
const int MAT_LIGHTMAP     = 32; // indirect light from the baked lightmap atlas (lightmap UVs)
const int MAT_WIND         = 64; // sways with the wind (foliage)
// Bits 8 and up: advanced-material entry + 1 (material_ext.glsl), 0 = none

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 emissiveColorAndIntensity;
    float roughness;
    float metallic;
    float aoStrength;
    float heightScale;
    int textureFlags;
    float alphaCutoff;
    int materialFlags;
    float ior; // index of refraction for transparent materials (1 = none)
} pc;

bool hasTex(int bit) { return (pc.textureFlags & bit) != 0; }

#endif
