// Advanced material settings that don't fit the 128-byte push constants. VulkanPipeline writes one
// entry per distinct material each frame (identical materials share an entry, so they still
// instance) and puts entry + 1 in materialFlags bits 8 and up (0 = standard material, no entry).
// Must match MaterialExtData in src/Renderer/VulkanPipeline.h. Requires common_material.glsl.
#ifndef MATERIAL_EXT_GLSL
#define MATERIAL_EXT_GLSL

const int MODEL_STANDARD = 0;
const int MODEL_SKIN = 1;  // subsurface scattering approximation
const int MODEL_HAIR = 2;  // anisotropic strand highlights (Kajiya-Kay) for hair cards
const int MODEL_EYE = 3;   // refracting cornea over a parallax iris

const int EXT_TRIPLANAR = 1;
const int EXT_DETAIL_ALBEDO = 2;
const int EXT_DETAIL_NORMAL = 4;
const int EXT_LIQUID = 8; // cut off at a fill level (Liquids::Simulation)
const int EXT_TERRAIN = 16; // paint layers blended by the terrain's splat map

struct MaterialExt {
    vec4 params0;    // x = model, y = clear coat (0..1), z = clear coat roughness, w = feature bits (EXT_*)
    vec4 subsurface; // skin: rgb = scatter colour, a = strength; hair: rgb = secondary highlight tint, a = highlight shift
    vec4 params1;    // x = skin translucency, y = triplanar tile size (m), z = triplanar blend sharpness, w = detail tiling
    vec4 params2;    // x = detail strength; eye: y = iris radius (UV), z = iris depth (UV), w = cornea IOR
    vec4 liquidPlane; // liquid: xyz = surface normal, w = its distance (points with dot(p, n) > w are cut away)
    vec4 liquidTop;   // liquid: colour of the surface seen from above
    vec4 liquidBoundsMin; // liquid: xyz = world box min, w = 1 for a round (cylindrical) container
    vec4 liquidBoundsMax; // liquid: xyz = world box max, w = colour depth (m)
    vec4 liquidRipple;    // liquid: xyz = where a stream lands, w = ripple strength
    vec4 terrainParams;   // terrain: x = UV tiling (splat UV = mesh UV / x), yz = size (m), w = layers with textures (bits)
    vec4 terrainTiling;   // terrain: metres per repeat of each layer
    vec4 terrainTint[4];  // terrain: rgb = each layer's tint, a = its roughness
};

layout(std430, binding = 17) readonly buffer MaterialExtBuffer {
    MaterialExt materialExts[];
};

MaterialExt LoadMaterialExt() {
    int index = (pc.materialFlags >> 8) - 1;
    if (index < 0) {
        MaterialExt none;
        none.params0 = vec4(0.0); none.subsurface = vec4(0.0); none.params1 = vec4(0.0); none.params2 = vec4(0.0);
        none.liquidPlane = vec4(0.0); none.liquidTop = vec4(0.0); none.liquidBoundsMin = vec4(0.0); none.liquidBoundsMax = vec4(0.0);
        none.liquidRipple = vec4(0.0); none.terrainParams = vec4(0.0); none.terrainTiling = vec4(0.0);
        none.terrainTint[0] = vec4(0.0); none.terrainTint[1] = vec4(0.0); none.terrainTint[2] = vec4(0.0); none.terrainTint[3] = vec4(0.0);
        return none;
    }
    return materialExts[index];
}

// ---- Triplanar mapping: three world-axis projections blended by the surface normal ----
vec3 TriplanarWeights(vec3 n, float sharpness) {
    vec3 w = pow(abs(n), vec3(max(sharpness, 1.0)));
    return w / max(w.x + w.y + w.z, 1e-4);
}

vec4 SampleTriplanar(sampler2D tex, vec3 p, vec3 w, float tileSize) {
    vec3 q = p / max(tileSize, 1e-3);
    return texture(tex, q.zy) * w.x + texture(tex, q.xz) * w.y + texture(tex, q.xy) * w.z;
}

// Tangent-space normal from a normal map's X and Y (Z rebuilt, as for BC5)
vec3 DecodeNormalXY(vec2 encoded) {
    vec2 xy = encoded * 2.0 - 1.0;
    return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

// Triplanar normal mapping with "whiteout" blending (Ben Golus): each projection's tangent
// normal is reoriented onto its world axis, then the three are blended
vec3 TriplanarNormal(sampler2D normalTex, vec3 p, vec3 n, vec3 w, float tileSize) {
    vec3 q = p / max(tileSize, 1e-3);
    vec3 tx = DecodeNormalXY(texture(normalTex, q.zy).xy);
    vec3 ty = DecodeNormalXY(texture(normalTex, q.xz).xy);
    vec3 tz = DecodeNormalXY(texture(normalTex, q.xy).xy);
    tx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
    ty = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
    tz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);
    return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

// ---- Hair: Kajiya-Kay strand highlight along a tangent ----
float StrandSpecular(vec3 T, vec3 H, float exponent) {
    float TdotH = dot(T, H);
    float sinTH = sqrt(max(1.0 - TdotH * TdotH, 0.0));
    return smoothstep(-1.0, 0.0, TdotH) * pow(sinTH, exponent);
}

#endif
