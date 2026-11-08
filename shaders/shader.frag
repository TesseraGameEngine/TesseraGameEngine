#version 460

#extension GL_GOOGLE_include_directive : require
#ifdef TESSERA_RT
// Hardware ray tracing variant (frag_rt.spv): see rt_common.glsl
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_nonuniform_qualifier : require
#endif

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap; // one layer per cascade
layout(binding = 8) uniform sampler2D ssaoMap; // this viewport's blurred SSAO (VulkanOffscreen)
layout(binding = 9) uniform sampler2D sceneColorMap; // opaque scene copy for refraction (VulkanOffscreen::NextScenePass)
layout(binding = 10) uniform sampler2D mirrorMap;    // this view's planar reflection (HDR)
layout(binding = 11) uniform sampler2DArrayShadow localShadowMap; // point / spot light shadow layers
#include "sun_shadow_common.glsl"

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;
layout(binding = 5) uniform sampler2D aoMap;
layout(binding = 6) uniform sampler2D emissiveMap;
layout(binding = 41) uniform sampler2D heightMap;

#include "common_material.glsl"
#include "material_ext.glsl"

layout(binding = 18) uniform sampler2D detailAlbedoMap; // tiled close-up detail (multiplied in, 0.5 grey = no change)
layout(binding = 19) uniform sampler2D detailNormalMap;
// Terrain paint: the splat map (layer weights in rgba) and the four layer textures
layout(binding = 34) uniform sampler2D terrainSplat;
layout(binding = 35) uniform sampler2D terrainLayer0;
layout(binding = 36) uniform sampler2D terrainLayer1;
layout(binding = 37) uniform sampler2D terrainLayer2;
layout(binding = 38) uniform sampler2D terrainLayer3;
#include "volumetric_common.glsl"
#include "water_common.glsl"
#include "weather_common.glsl"
#ifdef TESSERA_RT
#include "rt_common.glsl"
#endif

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPosition;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in flat int fragUseTexture;
layout(location = 5) in vec2 fragLightmapUV;

// Baked lightmap atlas: indirect light (sky and bounces) of lightmapped objects (MAT_LIGHTMAP)
layout(binding = 23) uniform sampler2D lightmapAtlas;
layout(binding = 24) uniform sampler2D sceneDepthMap; // opaque depth from this view's prepass (contact shadows)

// Screen-space contact shadow march along light direction L for fine micro-contact shadows
float ScreenSpaceContactShadow(vec3 worldPos, vec3 L) {
    ivec2 depthSize = textureSize(sceneDepthMap, 0);
    if (depthSize.x <= 4 || depthSize.y <= 4) return 1.0;
    float contactShadow = 1.0;
    const int kSteps = 8;
    const float kMaxDist = 0.22;
    for (int s = 1; s <= kSteps; ++s) {
        float t = (float(s) / float(kSteps)) * kMaxDist;
        vec4 clip = ubo.unjitteredViewProj * vec4(worldPos + L * t, 1.0);
        if (clip.w <= 1e-4) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 suv = ndc.xy * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
        float bufDepth = texture(sceneDepthMap, suv).r;
        float diff = ndc.z - bufDepth;
        if (diff > 0.00018 && diff < 0.0032) {
            contactShadow = min(contactShadow, mix(0.25, 1.0, float(s - 1) / float(kSteps)));
        }
    }
    return contactShadow;
}

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// Tangent frame from screen-space derivatives (no stored tangents): T along +u, B along the
// strand / v direction, N the surface normal
mat3 TangentFrame(vec3 N, vec3 worldPos, vec2 uv) {
    vec3 Q1  = dFdx(worldPos);
    vec3 Q2  = dFdy(worldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 N_norm = normalize(N);
    vec3 T  = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B  = -normalize(cross(N_norm, T));
    return mat3(T, B, N_norm);
}

vec3 getNormalFromMap(vec3 N, vec3 worldPos, vec2 uv) {
    // Only X and Y are used: Z is rebuilt (unit length, facing out), so two-channel BC5 maps work
    vec3 tangentNormal = DecodeNormalXY(texture(normalMap, uv).xy);
    return normalize(TangentFrame(N, worldPos, uv) * tangentNormal);
}

// Parallax Occlusion Mapping (POM) with adaptive 8..24 ray-march layers and Secant linear refinement
vec2 ParallaxOcclusionMapping(vec2 uv, vec3 viewDirTangent, float scale, out float outLayerDepth) {
    float numLayers = mix(24.0, 8.0, clamp(abs(viewDirTangent.z), 0.0, 1.0));
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    vec2 P = (viewDirTangent.xy / max(abs(viewDirTangent.z), 0.15)) * scale;
    vec2 deltaUV = P / numLayers;

    vec2 currentUV = uv;
    float currentDepthMapValue = 1.0 - texture(heightMap, currentUV).r;

    for (int step = 0; step < 24; ++step) {
        if (float(step) >= numLayers || currentLayerDepth >= currentDepthMapValue) break;
        currentUV -= deltaUV;
        currentDepthMapValue = 1.0 - texture(heightMap, currentUV).r;
        currentLayerDepth += layerDepth;
    }

    vec2 prevUV = currentUV + deltaUV;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - texture(heightMap, prevUV).r) - currentLayerDepth + layerDepth;
    float weight = clamp(afterDepth / max(afterDepth - beforeDepth, 1e-5), 0.0, 1.0);
    outLayerDepth = mix(currentLayerDepth, currentLayerDepth - layerDepth, weight);
    return mix(currentUV, prevUV, weight);
}

// 4-step horizon self-shadow march through the heightfield along the tangent-space light vector
float ParallaxSelfShadow(vec2 uv, vec3 lightDirTangent, float initialDepth, float scale) {
    if (lightDirTangent.z <= 0.02 || initialDepth <= 0.001) return 1.0;
    const int kShadowSteps = 4;
    float stepDepth = initialDepth / float(kShadowSteps);
    vec2 stepUV = (lightDirTangent.xy / max(lightDirTangent.z, 0.15)) * scale * (initialDepth / float(kShadowSteps));
    float curDepth = initialDepth - stepDepth;
    vec2 curUV = uv + stepUV;
    float occlusion = 0.0;
    for (int i = 0; i < kShadowSteps; ++i) {
        float mapDepth = 1.0 - texture(heightMap, curUV).r;
        if (mapDepth < curDepth) {
            occlusion = max(occlusion, (curDepth - mapDepth) * (1.0 - float(i) / float(kShadowSteps)));
        }
        curDepth -= stepDepth;
        curUV += stepUV;
    }
    return clamp(1.0 - occlusion * 2.5, 0.2, 1.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Linearly Transformed Cosines (LTC) polygonal contour integral for rectangular area lights (Heitz et al. 2016)
vec3 IntegrateLTC_Edge(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float theta_sintheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * theta_sintheta;
}

float LTC_EvaluatePolygon(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4], bool twoSided) {
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    Minv = Minv * transpose(mat3(T1, T2, N));
    vec3 L0 = normalize(Minv * (points[0] - P));
    vec3 L1 = normalize(Minv * (points[1] - P));
    vec3 L2 = normalize(Minv * (points[2] - P));
    vec3 L3 = normalize(Minv * (points[3] - P));
    vec3 vsum = IntegrateLTC_Edge(L0, L1) +
                IntegrateLTC_Edge(L1, L2) +
                IntegrateLTC_Edge(L2, L3) +
                IntegrateLTC_Edge(L3, L0);
    float len = length(vsum);
    float z = vsum.z / max(len, 1e-5);
    if (twoSided) z = abs(z);
    return max(len * max(z, 0.0), 0.0);
}

mat3 LTC_SpecMatrixInverse(float roughness, float NdotV) {
    float a = clamp(roughness * roughness, 0.02, 1.0);
    float t = 1.0 - clamp(NdotV, 0.0, 1.0);
    float m00 = mix(1.0 / a, 1.0 + 0.5 / a, t * t);
    float m20 = -0.65 * t * (1.0 - a);
    float m02 =  0.35 * t * (1.0 - a);
    return mat3(
        vec3(m00, 0.0, m02),
        vec3(0.0, 1.0 / mix(a, 1.0, 0.35 * t), 0.0),
        vec3(m20, 0.0, 1.0)
    );
}

// Shadow from a point or spot light in the local shadow array. Returns 1.0 (lit) when the
// light has no shadow layers this frame. Point lights use 6 layers in the order
// +X, -X, +Y, -Y, +Z, -Z (see VulkanPipeline::RenderShadowPass); the face is picked from
// the direction away from the light.
float SampleLocalShadow(int lightIndex, vec3 worldPos, vec3 N, vec3 L, vec3 lightPos, float lightBias) {
    int entry = -1;
    int used = int(ubo.localShadowParams.x + 0.5);
    for (int e = 0; e < used; ++e) {
        if (int(ubo.localShadowLights[e].x + 0.5) == lightIndex) { entry = e; break; }
    }
    if (entry < 0) return 1.0;

    vec4 info = ubo.localShadowLights[entry];
    int layer = int(info.y + 0.5);
    vec3 fromLight = worldPos - lightPos;
    if (info.z > 0.5) {
        vec3 a = abs(fromLight);
        if (a.x >= a.y && a.x >= a.z)      layer += (fromLight.x > 0.0) ? 0 : 1;
        else if (a.y >= a.z)               layer += (fromLight.y > 0.0) ? 2 : 3;
        else                               layer += (fromLight.z > 0.0) ? 4 : 5;
    }

    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    // Normal offset sized to the shadow texel at this distance from the light
    float texelWorld = length(fromLight) * info.w;
    vec3 offsetPos = worldPos + N * texelWorld * 1.5 * (1.0 - NdotL + 0.25);
    vec4 shadowClip = ubo.localShadowMatrices[layer] * vec4(offsetPos, 1.0);
    if (shadowClip.w <= 0.0) return 1.0;
    vec3 proj = shadowClip.xyz / shadowClip.w;
    if (proj.z <= 0.0 || proj.z >= 1.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;

    float bias = max(lightBias * (1.0 - NdotL), lightBias * 0.1) * 0.1;
    float compareDepth = proj.z - bias;
    float texel = ubo.localShadowParams.y;
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            lit += texture(localShadowMap, vec4(uv + vec2(x, y) * texel * 1.5, float(layer), compareDepth));
        }
    }
    return lit / 9.0;
}

// Shader graph material (after the inputs above, which its code reads)
#ifdef SHADER_GRAPH
#include "shader_graph_material.glsl" // EvaluateShaderGraph(), generated from the node's graph
#endif

// Liquids: distance a view ray travels through the liquid from p (inside or on it) along d,
// until it leaves through the surface, the bottom or the container's side. The container is its
// world box, or the upright cylinder inside it for round containers.
float LiquidExitDistance(vec3 p, vec3 d, MaterialExt ext) {
    vec3 lo = ext.liquidBoundsMin.xyz;
    vec3 hi = ext.liquidBoundsMax.xyz;
    float exitT = 1e9;
    float towardSurface = dot(d, ext.liquidPlane.xyz);
    if (towardSurface > 1e-5) exitT = min(exitT, (ext.liquidPlane.w - dot(p, ext.liquidPlane.xyz)) / towardSurface);
    if (d.y < -1e-5) exitT = min(exitT, (lo.y - p.y) / d.y);
    if (d.y > 1e-5) exitT = min(exitT, (hi.y - p.y) / d.y);
    if (ext.liquidBoundsMin.w > 0.5) {
        vec2 center = (lo.xz + hi.xz) * 0.5;
        vec2 radius = max((hi.xz - lo.xz) * 0.5, vec2(1e-4));
        vec2 o = (p.xz - center) / radius;
        vec2 v = d.xz / radius;
        float a = dot(v, v);
        float b = dot(o, v);
        float disc = b * b - a * (dot(o, o) - 1.0);
        if (a > 1e-8 && disc > 0.0) exitT = min(exitT, (-b + sqrt(disc)) / a);
    } else {
        for (int axis = 0; axis < 3; axis += 2) {
            if (d[axis] > 1e-5) exitT = min(exitT, (hi[axis] - p[axis]) / d[axis]);
            if (d[axis] < -1e-5) exitT = min(exitT, (lo[axis] - p[axis]) / d[axis]);
        }
    }
    return clamp(exitT, 0.0, 100.0);
}

// Liquids: whether p (on the surface plane) is inside the container's outline from above
bool InsideLiquidOutline(vec3 p, MaterialExt ext) {
    vec3 lo = ext.liquidBoundsMin.xyz;
    vec3 hi = ext.liquidBoundsMax.xyz;
    if (ext.liquidBoundsMin.w > 0.5) {
        vec2 o = (p.xz - (lo.xz + hi.xz) * 0.5) / max((hi.xz - lo.xz) * 0.5, vec2(1e-4));
        return dot(o, o) <= 1.02;
    }
    return all(greaterThanEqual(p.xz, lo.xz - 1e-3)) && all(lessThanEqual(p.xz, hi.xz + 1e-3));
}

// Output is linear HDR light. Exposure, bloom, tone mapping and gamma are applied
// afterwards by the post-processing passes (composite.frag).
void main() {
    // Animated splash (ocean spray where a wave breaks on a rock): a 16-frame flipbook drawn
    // procedurally. A crown of droplets flies out and falls while a mist puff spreads and fades.
    // pc.metallic = how far through its life (0..1), pc.roughness = its shape
    if (pc.heightScale < -1.5) {
        float frame = floor(clamp(pc.metallic, 0.0, 0.999) * 16.0) / 16.0;
        float seed = pc.roughness;
        vec2 p = (fragTexCoord - 0.5) * 2.0; // -1..1, y down
        p.y = -p.y;
        float alpha = 0.0;
        // Mist: a soft puff growing from the base
        float mistRadius = 0.25 + 0.55 * frame;
        float mist = smoothstep(mistRadius, mistRadius * 0.2, length(p - vec2(0.0, -0.5 + 0.3 * frame)));
        alpha += mist * 0.55 * (1.0 - frame);
        // Droplets on ballistic paths
        for (int k = 0; k < 14; ++k) {
            float h1 = fract(sin(seed * 12.9898 + float(k) * 78.233) * 43758.5453);
            float h2 = fract(sin(seed * 39.3468 + float(k) * 11.135) * 24634.6345);
            float angle = (h1 - 0.5) * 2.4;
            float speed = 0.8 + 0.7 * h2;
            vec2 drop = vec2(sin(angle) * speed * frame, -0.85 + cos(angle) * speed * 1.6 * frame - 1.9 * frame * frame);
            float size = mix(0.09, 0.03, frame) * (0.6 + 0.8 * h2);
            alpha += smoothstep(size, size * 0.3, length(p - drop)) * (1.0 - frame * 0.7);
        }
        alpha = clamp(alpha, 0.0, 1.0) * fragColor.a * smoothstep(1.0, 0.85, max(abs(p.x), abs(p.y)));
        if (alpha <= 0.003) discard;
        // White water lit by the sky and the sun (dim at night)
        vec3 sprayLight = vec3(1.0);
        if (ubo.skyParams.w > 0.0 || HdriActive()) {
            vec3 skySun = normalize(ubo.skyParams.xyz);
            sprayLight = EnvIrradiance(vec3(0.0, 1.0, 0.0), skySun) * 2.0 +
                         SkySunColor(skySun) * SkySunTransmittance(skySun) * max(skySun.y, 0.0) * 1.2;
        }
        outColor = vec4(fragColor.rgb * sprayLight, alpha);
        return;
    }

    // Procedural soft-particle circular falloff & self-illumination for billboard particles
    if (pc.heightScale < -0.5) {
        vec2 center = vec2(0.5, 0.5);
        float dist = length(fragTexCoord - center);
        if (dist > 0.5) {
            discard;
        }
        float radial = clamp(1.0 - (dist / 0.5), 0.0, 1.0);
        float soft = smoothstep(0.0, 1.0, radial);

        vec4 albedo = fragColor;
        if (hasTex(TEX_ALBEDO)) {
            vec4 texColor = texture(albedoMap, fragTexCoord);
            albedo = albedo * texColor;
            albedo.a *= soft;
        } else {
            albedo.a *= soft;
            // Hot core luminance boost for fire & glowing particles
            albedo.rgb += albedo.rgb * (pow(soft, 2.2) * 0.75);
        }

        vec3 emit = pc.emissiveColorAndIntensity.rgb * (pc.emissiveColorAndIntensity.a * soft);
        outColor = vec4(albedo.rgb + emit, albedo.a);
        return;
    }

    // Single-sided materials hide their back faces
    bool doubleSided = (pc.materialFlags & MAT_DOUBLE_SIDED) != 0;
    // A mirrored (reflection) view reverses triangle winding
    bool frontFacing = gl_FrontFacing != (ubo.timeParams.z > 0.5);
    if (!frontFacing && !doubleSided) {
        discard;
    }

    vec3 V = normalize(ubo.cameraPos.xyz - fragPosition);

    // Parallax Occlusion Mapping (POM) when a heightMap is bound, or lightweight offset fallback
    vec2 uv = fragTexCoord;
    float pomLayerDepth = 0.0;
    bool pomActive = hasTex(TEX_HEIGHT) && pc.heightScale > 0.0001;
    mat3 pomTBN = mat3(1.0);
    if (pomActive) {
        pomTBN = TangentFrame(frontFacing ? fragNormal : -fragNormal, fragPosition, fragTexCoord);
        vec3 viewTangent = normalize(transpose(pomTBN) * V);
        uv = ParallaxOcclusionMapping(fragTexCoord, viewTangent, pc.heightScale, pomLayerDepth);
    } else if (pc.heightScale > 0.0001) {
        uv += V.xy * (pc.heightScale * 0.15);
    }

    // Advanced material settings (skin, hair, eye, clear coat, triplanar, detail maps)
    MaterialExt ext = LoadMaterialExt();
    int model = int(ext.params0.x + 0.5);
    int features = int(ext.params0.w + 0.5);
    bool triplanar = (features & EXT_TRIPLANAR) != 0;
    // Liquid: everything above its (sloshing) surface is cut away, and the inside of the mesh
    // seen through the cut is shaded as the flat top surface
    bool liquidTop = false;
    float liquidThickness = 0.0;   // metres of liquid the view passes through here
    vec3 liquidSurfacePoint = fragPosition;
    if ((features & EXT_LIQUID) != 0) {
        if (dot(fragPosition, ext.liquidPlane.xyz) > ext.liquidPlane.w) discard;
        vec3 viewRay = normalize(fragPosition - ubo.cameraPos.xyz);
        if (!frontFacing) {
            // Only from above; from below the surface the inside shows the liquid itself
            if (dot(ubo.cameraPos.xyz, ext.liquidPlane.xyz) < ext.liquidPlane.w) discard;
            liquidTop = true;
            // What's seen is the surface where the view ray crosses it; the ray then runs
            // through the liquid to this back face
            float along = (ext.liquidPlane.w - dot(ubo.cameraPos.xyz, ext.liquidPlane.xyz)) /
                          min(dot(viewRay, ext.liquidPlane.xyz), -1e-4);
            liquidSurfacePoint = ubo.cameraPos.xyz + viewRay * along;
            // Elsewhere the back face is just the far side of the liquid, which the front
            // face in front of it already accounts for (its thickness runs to here)
            if (!InsideLiquidOutline(liquidSurfacePoint, ext)) discard;
            liquidThickness = distance(liquidSurfacePoint, fragPosition);
        } else {
            liquidThickness = LiquidExitDistance(fragPosition, viewRay, ext);
        }
    }
    vec3 triWeights = TriplanarWeights(normalize(fragNormal), ext.params1.z);
    float triTile = ext.params1.y;

    // Eye: the iris sits below a refracting cornea, so look it up where the refracted view ray
    // lands (UVs: iris centred on (0.5, 0.5))
    float irisMask = 0.0;
    float irisRadialPos = 0.0;
    if (model == MODEL_EYE) {
        mat3 eyeFrame = TangentFrame(normalize(fragNormal), fragPosition, fragTexCoord);
        vec3 viewTangent = transpose(eyeFrame) * V;
        float irisRadius = max(ext.params2.y, 0.01);
        irisRadialPos = length(fragTexCoord - 0.5) / irisRadius;
        irisMask = 1.0 - smoothstep(0.95, 1.05, irisRadialPos);
        vec3 refracted = refract(-viewTangent, vec3(0.0, 0.0, 1.0), 1.0 / max(ext.params2.w, 1.0));
        uv += refracted.xy / max(-refracted.z, 0.25) * ext.params2.z * irisMask;
    }

    vec4 albedo = fragColor;
    if (hasTex(TEX_ALBEDO)) {
        vec4 texColor = triplanar ? SampleTriplanar(albedoMap, fragPosition, triWeights, triTile) : texture(albedoMap, uv);
        albedo = albedo * texColor;
    }
    vec2 detailUV = triplanar ? vec2(0.0) : uv * max(ext.params1.w, 0.01);
    if ((features & EXT_DETAIL_ALBEDO) != 0) {
        vec3 detail = triplanar ? SampleTriplanar(detailAlbedoMap, fragPosition, triWeights, triTile / max(ext.params1.w, 0.01)).rgb
                                : texture(detailAlbedoMap, detailUV).rgb;
        albedo.rgb *= mix(vec3(1.0), detail * 2.0, clamp(ext.params2.x, 0.0, 1.0));
    }
    if (model == MODEL_EYE) {
        // Darker ring where the iris meets the white (limbus)
        albedo.rgb *= 1.0 - 0.45 * smoothstep(0.75, 1.0, irisRadialPos) * (1.0 - smoothstep(1.0, 1.15, irisRadialPos));
    }

    // Alpha cutout (foliage, fences, hair cards): hard edges, no blending needed
    if (pc.alphaCutoff > 0.0) {
        if (albedo.a < pc.alphaCutoff) {
            discard;
        }
        albedo.a = 1.0;
    }

    if (liquidTop) albedo = ext.liquidTop;

    // Terrain: the paint layers blended by the splat map. A layer shows its texture (tinted), or
    // without one its tint with a little natural variation. Where layers meet, the brighter
    // (higher) parts of each texture win, so edges break up instead of fading flatly.
    float terrainRoughness = -1.0;
    if ((features & EXT_TERRAIN) != 0) {
        vec2 splatUV = fragTexCoord / max(ext.terrainParams.x, 1e-4);
        vec4 weights = texture(terrainSplat, splatUV);
        weights /= max(weights.r + weights.g + weights.b + weights.a, 1e-4);
        int textured = int(ext.terrainParams.w + 0.5);
        vec2 metres = splatUV * ext.terrainParams.yz;
        vec3 layerColor[4];
        float layerHeight[4];
        // Layer textures follow the world (not the tile), so neighbouring terrain tiles line up;
        // a second, rotated and larger sample blended in by a slow noise breaks up the tiling
        vec2 worldXZ = fragPosition.xz;
        float macro = smoothstep(0.32, 0.68, WeatherNoise(worldXZ * 0.021) * 0.65 + WeatherNoise(worldXZ * 0.067) * 0.35);
        const mat2 kLayerRot = mat2(0.8, -0.6, 0.6, 0.8);
        for (int i = 0; i < 4; ++i) {
            vec2 layerUV = worldXZ / max(ext.terrainTiling[i], 1e-3);
            vec2 layerUV2 = kLayerRot * layerUV * 0.43 + vec2(0.37, 0.71);
            vec3 c = ext.terrainTint[i].rgb;
            if ((textured & (1 << i)) != 0) {
                vec3 t1 = i == 0 ? texture(terrainLayer0, layerUV).rgb : i == 1 ? texture(terrainLayer1, layerUV).rgb
                        : i == 2 ? texture(terrainLayer2, layerUV).rgb : texture(terrainLayer3, layerUV).rgb;
                vec3 t2 = i == 0 ? texture(terrainLayer0, layerUV2).rgb : i == 1 ? texture(terrainLayer1, layerUV2).rgb
                        : i == 2 ? texture(terrainLayer2, layerUV2).rgb : texture(terrainLayer3, layerUV2).rgb;
                vec3 t = mix(t1, t2, macro * 0.85) * mix(0.9, 1.1, WeatherNoise(worldXZ * 0.013 + float(i) * 7.1));
                c *= t;
                layerHeight[i] = dot(t, vec3(0.3, 0.5, 0.2));
            } else {
                float n = WeatherNoise(layerUV * 1.7) * 0.6 + WeatherNoise(layerUV * 6.3) * 0.4;
                c *= 0.82 + 0.36 * n;
                layerHeight[i] = n;
            }
            layerColor[i] = c;
        }
        float blendW[4];
        float top = 0.0;
        for (int i = 0; i < 4; ++i) top = max(top, weights[i] + layerHeight[i] * 0.35);
        float total = 0.0;
        for (int i = 0; i < 4; ++i) {
            blendW[i] = max(weights[i] + layerHeight[i] * 0.35 - top + 0.12, 0.0) * step(0.001, weights[i]);
            total += blendW[i];
        }
        vec3 blended = vec3(0.0);
        terrainRoughness = 0.0;
        for (int i = 0; i < 4; ++i) {
            float w = blendW[i] / max(total, 1e-4);
            blended += layerColor[i] * w;
            terrainRoughness += ext.terrainTint[i].a * w;
        }
        albedo.rgb = blended;
    }
    // Liquids look deeper and more opaque the more of them the view passes through: Colour
    // Depth of liquid gives the set colour and opacity, thinner is paler, thicker deeper
    vec3 transmissionTint = albedo.rgb;
    if ((features & EXT_LIQUID) != 0) {
        float depths = liquidThickness / max(ext.liquidBoundsMax.w, 1e-4);
        transmissionTint = pow(max(albedo.rgb, vec3(1e-3)), vec3(depths));
        albedo.a = 1.0 - pow(1.0 - clamp(albedo.a, 0.0, 0.999), depths);
    }

    // Back faces of double-sided surfaces are lit from their own side
    vec3 geomNormal = frontFacing ? fragNormal : -fragNormal;
    if (liquidTop) geomNormal = ext.liquidPlane.xyz;
    vec3 N = normalize(geomNormal);
    if (liquidTop && ext.liquidRipple.w > 0.001) {
        // Rings spreading from where a stream lands, sized to the container
        vec2 width = ext.liquidBoundsMax.xz - ext.liquidBoundsMin.xz;
        float wavelength = clamp(min(width.x, width.y) * 0.12, 0.004, 0.5);
        vec2 offset = liquidSurfacePoint.xz - ext.liquidRipple.xz;
        float r = length(offset);
        float k = 6.2831853 / wavelength;
        float slope = cos(r * k - ubo.timeParams.x * k * wavelength * 5.0) *
                      exp(-r / (wavelength * 3.0)) * 0.6 * ext.liquidRipple.w;
        N = normalize(N + vec3(offset.x, 0.0, offset.y) / max(r, 1e-5) * slope);
    }
    if (hasTex(TEX_NORMAL)) {
        N = triplanar ? TriplanarNormal(normalMap, fragPosition, N, triWeights, triTile)
                      : getNormalFromMap(geomNormal, fragPosition, uv);
    }
    if ((features & EXT_DETAIL_NORMAL) != 0) {
        // Detail bumps added on top of the base normal (in the surface's own frame)
        vec3 detailN = triplanar ? TriplanarNormal(detailNormalMap, fragPosition, N, triWeights, triTile / max(ext.params1.w, 0.01))
                                 : TangentFrame(N, fragPosition, uv) * DecodeNormalXY(texture(detailNormalMap, detailUV).xy);
        N = normalize(mix(N, normalize(N + detailN - normalize(geomNormal)), clamp(ext.params2.x, 0.0, 1.0)));
    }

    float metallic = pc.metallic;
    if (hasTex(TEX_METALLIC)) {
        vec4 metalSample = triplanar ? SampleTriplanar(metallicMap, fragPosition, triWeights, triTile) : texture(metallicMap, uv);
        metallic = metallic * (hasTex(TEX_PACKED_MR) ? metalSample.b : metalSample.r);
    }
    metallic = clamp(metallic, 0.0, 1.0);

    float roughness = pc.roughness;
    if (hasTex(TEX_ROUGHNESS)) {
        vec4 roughSample = triplanar ? SampleTriplanar(roughnessMap, fragPosition, triWeights, triTile) : texture(roughnessMap, uv);
        roughness = roughness * (hasTex(TEX_PACKED_MR) ? roughSample.g : roughSample.r);
    }
    if (terrainRoughness >= 0.0) roughness = terrainRoughness;
    roughness = clamp(roughness, 0.04, 1.0);

    // A shader graph material replaces the base material's values (the lighting stays the same)
    float graphAO = 1.0;
    vec3 graphEmissive = vec3(0.0);
#ifdef SHADER_GRAPH
    {
        vec4 sgAlbedo;
        vec3 sgNormal;
        float sgMetallic, sgRoughness;
        EvaluateShaderGraph(V, sgAlbedo, sgNormal, sgMetallic, sgRoughness, graphAO, graphEmissive);
        albedo = sgAlbedo * fragColor;
        N = sgNormal;
        metallic = sgMetallic;
        roughness = sgRoughness; // F0 is derived from albedo and metallic just below
    }
#endif

    // Weather: wet or snowy surfaces (not see-through ones or liquids)
    if ((pc.materialFlags & MAT_TRANSPARENT) == 0 && (features & EXT_LIQUID) == 0) {
        vec3 weatherAlbedo = albedo.rgb;
        ApplyWeather(fragPosition, normalize(geomNormal), weatherAlbedo, N, roughness, metallic);
        albedo.rgb = weatherAlbedo;
    }

    // Clear coat: a thin glossy layer over the base, with its own (smooth, unbumped) normal.
    // Eyes use it for the wet cornea.
    float clearCoat = clamp(ext.params0.y, 0.0, 1.0);
    float clearCoatRoughness = clamp(ext.params0.z, 0.02, 1.0);
    vec3 coatNormal = normalize(geomNormal);

    // Hair: strands run along the texture's V direction
    vec3 hairTangent = vec3(0.0);
    if (model == MODEL_HAIR) {
        hairTangent = normalize(TangentFrame(N, fragPosition, uv)[1]);
    }

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);
    float NdotV = max(dot(N, V), 0.0001);
    vec2 dfg = EnvBRDFScaleBias(roughness, NdotV);
    // Kulla-Conty / Turquin multi-scattering GGX energy compensation for rough conductors/dielectrics
    vec3 energyComp = 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 0.01) - 1.0);

    vec3 Lo = vec3(0.0);
    // Specular part of all lighting, kept separately so transparent surfaces can leave
    // their reflections at full strength (see the premultiplied output at the end)
    vec3 specularSum = vec3(0.0);

    if (ubo.numLights == 0) {
        vec3 L = normalize(vec3(1.0, 2.0, 1.5));
        vec3 H = normalize(V + L);
        vec3 radiance = vec3(1.5);

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular     = (numerator / denominator) * energyComp;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
        specularSum += specular * radiance * NdotL;
    } else {
        // Directional lights, then the point / spot lights listed for this pixel's cluster
        uvec2 cluster = lightClusters.clusters[ClusterIndex(fragPosition)];
        int lightTotal = LightCountAt(cluster);
        for (int k = 0; k < lightTotal; ++k) {
            int i = LightIndexAt(cluster, k);
            vec3 lightPos = lightList.lights[i].position.xyz;
            float lightType = lightList.lights[i].position.w; // 0 = Directional, 1 = Point, 2 = Spot
            vec3 lightColor = lightList.lights[i].color.rgb;
            float intensity = lightList.lights[i].color.a;
            float maxRange = max(lightList.lights[i].spotDirAndRange.w, 0.1);

            vec3 L;
            vec3 radiance;

            if (lightType > 2.5) {
                // Rectangular Area Light (Linearly Transformed Cosines — LTC)
                vec3 lightVec = lightPos - fragPosition;
                float dist = length(lightVec);
                float rangeFactor = clamp(1.0 - pow(dist / maxRange, 4.0), 0.0, 1.0);
                if (rangeFactor <= 0.0001) continue;
                vec3 areaNormal = normalize(lightList.lights[i].spotDirAndRange.xyz);
                float halfW = max(lightList.lights[i].spotAnglesAndShadow.x, 0.05);
                float halfH = max(lightList.lights[i].spotAnglesAndShadow.y, 0.05);
                bool twoSided = lightList.lights[i].spotAnglesAndShadow.z > 0.5;
                float roll = lightList.lights[i].spotAnglesAndShadow.w;
                if (!twoSided && dot(fragPosition - lightPos, areaNormal) <= 0.0) continue;

                vec3 upRef = abs(areaNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                vec3 basisRight = normalize(cross(upRef, areaNormal));
                vec3 basisUp = cross(areaNormal, basisRight);
                vec3 areaRight = cos(roll) * basisRight + sin(roll) * basisUp;
                vec3 areaUp    = -sin(roll) * basisRight + cos(roll) * basisUp;

                vec3 rectPts[4];
                rectPts[0] = lightPos - areaRight * halfW - areaUp * halfH;
                rectPts[1] = lightPos + areaRight * halfW - areaUp * halfH;
                rectPts[2] = lightPos + areaRight * halfW + areaUp * halfH;
                rectPts[3] = lightPos - areaRight * halfW + areaUp * halfH;

                float ltcDiff = LTC_EvaluatePolygon(N, V, fragPosition, mat3(1.0), rectPts, twoSided);
                float ltcSpec = LTC_EvaluatePolygon(N, V, fragPosition, LTC_SpecMatrixInverse(roughness, NdotV), rectPts, twoSided);

                vec3 F_ltc = F0 * dfg.x + dfg.y;
                vec3 kD_ltc = (vec3(1.0) - F_ltc) * (1.0 - metallic);
                vec3 areaRad = lightColor * intensity * (rangeFactor * rangeFactor);
                vec3 specContrib = F_ltc * energyComp * ltcSpec * areaRad;
                Lo += kD_ltc * albedo.rgb * ltcDiff * areaRad + specContrib;
                specularSum += specContrib;
                continue;
            } else if (lightType > 0.5) {
                vec3 lightVec = lightPos - fragPosition;
                float dist = length(lightVec);
                L = normalize(lightVec);
                float rangeFactor = clamp(1.0 - pow(dist / maxRange, 4.0), 0.0, 1.0);
                float attenuation = (rangeFactor * rangeFactor) / (1.0 + 0.09 * dist + 0.032 * dist * dist);

                if (lightType > 1.5) {
                    // Spot light cone attenuation
                    vec3 spotDir = normalize(lightList.lights[i].spotDirAndRange.xyz);
                    float theta = dot(-L, spotDir);
                    float cosInner = lightList.lights[i].spotAnglesAndShadow.x;
                    float cosOuter = lightList.lights[i].spotAnglesAndShadow.y;
                    float epsilon = max(cosInner - cosOuter, 0.0001);
                    float spotFactor = clamp((theta - cosOuter) / epsilon, 0.0, 1.0);
                    attenuation *= spotFactor * spotFactor;
                }
                radiance = lightColor * intensity * attenuation;
            } else {
                L = normalize(lightPos);
                radiance = lightColor * intensity;
                // The directional light that is the sky's sun dims and warms with the time of day
                if (ubo.skyParams.w > 0.0 && dot(L, normalize(ubo.skyParams.xyz)) > 0.9999) {
                    radiance *= SkySunTransmittance(L);
                }
                // Under an ocean: dimmed with depth and broken into caustics
                radiance *= UnderwaterSunlight(fragPosition);
                // Shade from the cloud layer
                radiance *= CloudShadow(fragPosition, L);
            }

            // Shadow-mapped lights: the sun via cascades, point/spot lights via the local array
            // (see VulkanPipeline::RenderShadowPass)
            float shadow = 1.0;
#ifdef TESSERA_RT
            // Ray-traced shadows: soft (sun disc / bulb size), alpha-tested through foliage, and
            // not limited to the shadow map's distance or resolution
            bool rtShadowed = false;
            if (RTShadowsOn() && lightList.lights[i].spotAnglesAndShadow.z > 0.5) {
                if (lightType < 0.5) {
                    shadow = RTDirectionalShadow(fragPosition, normalize(geomNormal), L);
                    rtShadowed = true;
                } else if (length(lightPos - ubo.cameraPos.xyz) < ubo.rtParams2.w) {
                    shadow = RTLocalShadow(fragPosition, normalize(geomNormal), lightPos, 0.05);
                    rtShadowed = true;
                }
            }
            if (rtShadowed) {
                // done
            } else
#endif
            if (ubo.shadowParams.x > 0.5 && i == int(ubo.shadowParams.y + 0.5)) {
                shadow = SampleShadow(fragPosition, normalize(geomNormal), L, lightList.lights[i].spotAnglesAndShadow.w);
                if (shadow > 0.01 && (ubo.postProcessParams.y > 0.001 || ubo.postProcessParams.z > 0.001)) {
                    shadow *= ScreenSpaceContactShadow(fragPosition, L);
                }
            } else if (lightType > 0.5 && ubo.localShadowParams.x > 0.5) {
                shadow = SampleLocalShadow(i, fragPosition, normalize(geomNormal), L, lightPos, lightList.lights[i].spotAnglesAndShadow.w);
            }
            if (pomActive && shadow > 0.01) {
                shadow *= ParallaxSelfShadow(uv, normalize(transpose(pomTBN) * L), pomLayerDepth, pc.heightScale);
            }

            vec3 H = normalize(V + L);
            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator    = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular     = (numerator / denominator) * energyComp;

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            float NdotL = max(dot(N, L), 0.0);
            vec3 diffuseTerm = kD * albedo.rgb / PI;
            vec3 contribution = (diffuseTerm + specular) * NdotL * shadow;
            vec3 contributionSpecular = specular * NdotL * shadow;

            if (model == MODEL_SKIN) {
                // Light scattered under the skin: it wraps past the shadow terminator with the
                // scatter colour, softens shadow edges, and glows through thin parts when backlit
                float s = clamp(ext.subsurface.a, 0.0, 1.0);
                float wrapped = max((dot(N, L) + s * 0.5) / (1.0 + s * 0.5), 0.0);
                vec3 scatter = ext.subsurface.rgb * max(wrapped - NdotL, 0.0);
                float softShadow = mix(shadow, 1.0, 0.25 * s);
                float backLight = pow(clamp(dot(V, -normalize(L + N * 0.4)), 0.0, 1.0), 3.0) * ext.params1.x;
                contribution = diffuseTerm * (NdotL + scatter) * softShadow + contributionSpecular +
                               albedo.rgb * ext.subsurface.rgb * backLight * mix(0.3, 1.0, shadow) / PI;
            } else if (model == MODEL_HAIR) {
                // Two strand highlights: a white one shifted toward the tip and a dimmer coloured
                // one toward the root (Kajiya-Kay with Scheuermann's shifts)
                float shift = ext.subsurface.a;
                vec3 T1 = normalize(hairTangent + N * shift);
                vec3 T2 = normalize(hairTangent + N * (shift - 0.25));
                float exponent = mix(300.0, 20.0, roughness);
                vec3 strandSpec = vec3(StrandSpecular(T1, H, exponent)) * 0.12 +
                                  albedo.rgb * ext.subsurface.rgb * StrandSpecular(T2, H, exponent * 0.25) * 0.3;
                float hairDiffuse = max(0.75 * dot(N, L) + 0.25, 0.0);
                contribution = (albedo.rgb / PI * hairDiffuse + strandSpec) * shadow;
                contributionSpecular = strandSpec * shadow;
            }

            if (clearCoat > 0.0) {
                float coatNdotL = max(dot(coatNormal, L), 0.0);
                float coatD = DistributionGGX(coatNormal, H, clearCoatRoughness);
                float coatG = GeometrySmith(coatNormal, V, L, clearCoatRoughness);
                float coatF = (0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0)) * clearCoat;
                vec3 coatSpec = vec3(coatD * coatG * coatF / (4.0 * max(dot(coatNormal, V), 0.0) * coatNdotL + 0.0001)) * coatNdotL * shadow;
                contribution = contribution * (1.0 - coatF) + coatSpec;
                contributionSpecular = contributionSpecular * (1.0 - coatF) + coatSpec;
            }

            Lo += contribution * radiance;
            specularSum += contributionSpecular * radiance;
        }
    }

    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    bool proceduralSky = ubo.skyParams.w > 0.0;
    // Environment lighting from the procedural sky or an HDRI, refined by reflection probes.
    // Probes also work with neither (e.g. indoors with the sky off), over a flat hemisphere.
    bool skyOrHdri = proceduralSky || HdriActive();
    int probe;
    float probeWeight = ProbeInfluence(fragPosition, probe);
    // Baked GI: sampled a little off the surface so probes behind it (inside walls) count less
    float bakedWeight = BakedGIWeight(fragPosition);
    bool lightmapped = (pc.materialFlags & MAT_LIGHTMAP) != 0 && ubo.lightmapParams.x > 0.5;
    float vxgiWeight = VXGIWeight(fragPosition);
    bool envLighting = skyOrHdri || probeWeight > 0.0 || bakedWeight > 0.0 || lightmapped || vxgiWeight > 0.0;
    vec3 sunDir = normalize(ubo.skyParams.xyz);

    vec3 ambient;
    vec3 ambientSpecular = vec3(0.0);
    if (envLighting) {
        // Image-based lighting: diffuse irradiance + Fdez-Aguera multi-scattering glossy reflection
        vec3 envBRDF = F0 * dfg.x + dfg.y;
        float Ems = max(1.0 - (dfg.x + dfg.y), 0.0);
        vec3 Favg = F0 + (1.0 - F0) / 21.0;
        vec3 Fms = envBRDF * Favg / max(1.0 - Ems * Favg, vec3(1e-4));
        vec3 kD = (vec3(1.0) - envBRDF - Fms * Ems) * (1.0 - metallic);
        vec3 R = reflect(-V, N);
        vec3 irradiance;
        vec3 prefiltered;
        if (skyOrHdri) {
            irradiance = EnvIrradiance(N, sunDir) + LightningLight(N);
            prefiltered = EnvSpecular(R, roughness, sunDir);
        } else {
            // Same flat hemisphere as the no-sky ambient below
            vec3 groundGI = vec3(0.05, 0.04, 0.03);
            irradiance = mix(groundGI, ubo.giSkyColor.rgb, N.y * 0.5 + 0.5);
            prefiltered = mix(groundGI, ubo.giSkyColor.rgb, R.y * 0.5 + 0.5);
        }
        if (probeWeight > 0.0) {
            float maxLod = ProbeMaxLod();
            irradiance = mix(irradiance, SampleProbe(probe, N, maxLod), probeWeight);
            prefiltered = mix(prefiltered, SampleProbe(probe, ProbeDirection(probe, fragPosition, R), roughness * maxLod), probeWeight);
        }
        if (lightmapped) {
            // The lightmap replaces the sky / probe estimate for diffuse lighting. Where it shows
            // the surface is enclosed (less light than the open sky gives), sky reflections dim
            // too, unless a reflection probe already shows the surroundings.
            vec3 baked = texture(lightmapAtlas, fragLightmapUV).rgb * ubo.lightmapParams.y;
            const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
            float openSky = max(dot(irradiance, lumaWeights), 1e-4);
            float enclosed = clamp(dot(baked, lumaWeights) / openSky, 0.0, 1.0);
            prefiltered *= mix(enclosed, 1.0, probeWeight);
            irradiance = baked;
        } else if (bakedWeight > 0.0) {
            // Baked bounce light replaces the sky / probe estimate for diffuse lighting
            float spacing = min(min(ubo.giVolumeSize.x / max(ubo.giVolumeDims.x - 1.0, 1.0),
                                    ubo.giVolumeSize.y / max(ubo.giVolumeDims.y - 1.0, 1.0)),
                                ubo.giVolumeSize.z / max(ubo.giVolumeDims.z - 1.0, 1.0));
            vec3 baked = BakedIrradiance(fragPosition + normalize(geomNormal) * spacing * 0.35, N);
            irradiance = mix(irradiance, baked, bakedWeight);
        }
        if (vxgiWeight > 0.0) {
            vec3 safeGeomN = normalize(geomNormal);
            vec4 vxgiDiff = VXGIDiffuse(fragPosition, N, safeGeomN);
            irradiance = irradiance * mix(1.0, vxgiDiff.a, vxgiWeight * 0.85) + vxgiDiff.rgb * ubo.vxgiParams.y * vxgiWeight;
            if (roughness < 0.85) {
                vec4 vxgiSpec = VXGISpecular(fragPosition, R, safeGeomN, roughness);
                float specBlend = clamp(vxgiSpec.a * vxgiWeight * (1.0 - roughness * 0.7), 0.0, 0.85);
                prefiltered = mix(prefiltered, vxgiSpec.rgb * ubo.vxgiParams.y, specBlend);
            }
        }
#ifdef TESSERA_RT
        // Ray-traced reflections for glossy surfaces: what the reflection ray actually hits, lit
        // by the sun (with its own shadow ray) and the sky; the environment fills in on a miss
        if (RTReflectionsOn() && roughness <= ubo.rtParams.w) {
            // Glossy (not mirror) surfaces jitter the ray within a roughness cone; TAA averages it
            float a = roughness * roughness;
            vec3 rDir = R;
            if (a > 0.001) {
                float r1 = RTNoise(gl_FragCoord.xy, 13.7);
                float r2 = RTNoise(gl_FragCoord.xy, 27.1);
                vec3 up = abs(R.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                vec3 t = normalize(cross(up, R));
                vec3 b = cross(R, t);
                float spread = a * 0.6 * sqrt(r1);
                rDir = normalize(R + (t * cos(6.2831853 * r2) + b * sin(6.2831853 * r2)) * spread);
                if (dot(rDir, N) <= 0.0) rDir = R;
            }
            // The sun's light (first directional light), for lighting what the ray hits
            vec3 sunL = sunDir;
            vec3 sunRad = vec3(0.0);
            for (int k = 0; k < ubo.numLights && k < 16; ++k) {
                if (lightList.lights[k].position.w < 0.5) {
                    sunL = normalize(lightList.lights[k].position.xyz);
                    sunRad = lightList.lights[k].color.rgb * lightList.lights[k].color.a;
                    if (proceduralSky && dot(sunL, sunDir) > 0.9999) sunRad *= SkySunTransmittance(sunL);
                    break;
                }
            }
            vec3 rtRadiance;
            float rtHitDist;
            if (RTTraceReflection(fragPosition, normalize(geomNormal), rDir, sunL, sunRad, giStrength, rtRadiance, rtHitDist)) {
                float fade = 1.0 - smoothstep(ubo.rtParams.w * 0.7, ubo.rtParams.w, roughness);
                // prefiltered is scaled by giStrength below; the hit radiance is already absolute
                prefiltered = mix(prefiltered, rtRadiance / max(giStrength, 0.05), fade);
            }
        }
#endif
        vec3 diffuseIBL = irradiance * albedo.rgb * (kD + Fms * Ems * (1.0 - metallic));
        vec3 specularIBL = prefiltered * envBRDF + irradiance * Fms * Ems * metallic;
        if (model == MODEL_HAIR) specularIBL *= 0.3; // strands reflect the environment far less than a mirror-like sheet
        ambient = (diffuseIBL + specularIBL) * giStrength;
        ambientSpecular = specularIBL * giStrength;

        if (clearCoat > 0.0) {
            // The coat reflects the environment sharply, on top of the base
            vec3 coatR = reflect(-V, coatNormal);
            vec3 coatEnv = skyOrHdri ? EnvSpecular(coatR, clearCoatRoughness, sunDir)
                                     : mix(vec3(0.05, 0.04, 0.03), ubo.giSkyColor.rgb, coatR.y * 0.5 + 0.5);
            if (probeWeight > 0.0) {
                coatEnv = mix(coatEnv, SampleProbe(probe, ProbeDirection(probe, fragPosition, coatR), clearCoatRoughness * ProbeMaxLod()), probeWeight);
            }
            float coatF = (0.04 + 0.96 * pow(1.0 - max(dot(coatNormal, V), 0.0), 5.0)) * clearCoat;
            ambient = ambient * (1.0 - coatF) + coatEnv * coatF * giStrength;
            ambientSpecular = ambientSpecular * (1.0 - coatF) + coatEnv * coatF * giStrength;
        }
    } else {
        // Flat hemisphere ambient from the Sky Hemisphere Color setting
        float hemi = N.y * 0.5 + 0.5;
        vec3 skyGI = ubo.giSkyColor.rgb;
        vec3 groundGI = vec3(0.05, 0.04, 0.03);
        ambient = mix(groundGI, skyGI, hemi) * giStrength * albedo.rgb;
    }

    // Screen-Space Ambient Occlusion (SSAO) & Material AO
    // Screen-space AO computed from the depth/normal prepass (1 = open, 0 = occluded).
    // postProcessParams.y is the strength: 0 = off, 1 = as computed, >1 = darker.
    float ssao = 1.0;
    if (ubo.postProcessParams.y > 0.001) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(ssaoMap, 0));
        ssao = pow(texture(ssaoMap, screenUV).r, ubo.postProcessParams.y);
    }
    float materialAO = 1.0;
    if (hasTex(TEX_AO)) {
        float aoSample = triplanar ? SampleTriplanar(aoMap, fragPosition, triWeights, triTile).r : texture(aoMap, uv).r;
        materialAO = mix(1.0, aoSample, clamp(pc.aoStrength, 0.0, 1.0));
    }
    float ao = ssao * materialAO * graphAO;
#ifdef TESSERA_RT
    // Ray-traced AO replaces the screen-space estimate (sees occluders off screen and behind)
    if (RTAOOn()) ao = RTAmbientOcclusion(fragPosition, N, normalize(geomNormal)) * materialAO * graphAO;
#endif
    float specAO = clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
    ambient = max(ambient - ambientSpecular, vec3(0.0)) * ao + ambientSpecular * specAO;
    ambientSpecular *= specAO;

    // Screen-Space Reflection (SSR) environment specular lobe
    // (skipped with the procedural sky, whose reflections are already included above)
    float ssrStrength = ubo.postProcessParams.z;
    if (!envLighting && ssrStrength > 0.001) {
        vec3 R = reflect(-V, N);
        float rSky = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 envRefl = mix(vec3(0.06, 0.06, 0.08), ubo.giSkyColor.rgb * 2.2, rSky);
        vec3 F_env = fresnelSchlick(max(dot(N, V), 0.0), F0);
        ambient += envRefl * F_env * (1.0 - roughness) * ssrStrength;
        ambientSpecular += envRefl * F_env * (1.0 - roughness) * ssrStrength;
    }

    // Emissive contribution
    vec3 emissive = pc.emissiveColorAndIntensity.rgb * pc.emissiveColorAndIntensity.a;
    if (hasTex(TEX_EMISSIVE)) {
        // With a map but no colour set, the map's own colours glow at the given intensity
        vec3 emitTint = (dot(pc.emissiveColorAndIntensity.rgb, vec3(1.0)) > 0.0) ? pc.emissiveColorAndIntensity.rgb : vec3(1.0);
        emissive = texture(emissiveMap, uv).rgb * emitTint * pc.emissiveColorAndIntensity.a;
    }

    vec3 color = ambient + Lo + emissive + graphEmissive;
    specularSum += ambientSpecular;

    // Planar mirror: show the reflection rendered from the mirrored camera, tinted by the base colour
    if ((pc.materialFlags & MAT_MIRROR) != 0) {
        vec2 mirrorUV = gl_FragCoord.xy / vec2(textureSize(mirrorMap, 0));
        color = texture(mirrorMap, mirrorUV).rgb * albedo.rgb + emissive;
    }

    // Physical atmosphere: distant surfaces fade into the air's colour
    color = AerialPerspective(color, fragPosition);

    // Exponential Height Fog
    float fogDensity = ubo.fogColorAndDensity.a;
    float fogAmount = 0.0;
    if (fogDensity > 0.0001) {
        float camDist = length(ubo.cameraPos.xyz - fragPosition);
        float heightFactor = exp(-max(fragPosition.y, 0.0) * 0.15);
        fogAmount = clamp(1.0 - exp(-camDist * fogDensity * heightFactor), 0.0, 1.0);
        color = mix(color, ubo.fogColorAndDensity.rgb, fogAmount);
    }

    // Camera under water: surfaces in the water fade into it. Surfaces above the water are seen
    // through the ocean surface, which adds the fog for its own distance.
    if (ViewUnderwater(fragPosition) && fragPosition.y < WaterLevel()) {
        color = UnderwaterFog(color, length(ubo.cameraPos.xyz - fragPosition));
    }

    // Volumetric fog and light shafts between the camera and this surface. Transparent surfaces
    // are skipped: the opaque surface behind them already carries the fog for the whole ray.
    if (VolumetricActive() && (pc.materialFlags & MAT_TRANSPARENT) == 0) {
        vec4 volumetric = VolumetricFog(ubo.cameraPos.xyz, fragPosition, gl_FragCoord.xy);
        color = color * volumetric.a + volumetric.rgb;
    }

    // Stay inside the 16-bit float range of the HDR target (max 65504). Tight highlights on
    // very smooth metals can exceed it, and an Inf pixel would turn bloom into NaN everywhere.
    color = min(color, vec3(60000.0));

    if ((pc.materialFlags & MAT_TRANSPARENT) != 0) {
        // Premultiplied alpha (blend: ONE, ONE_MINUS_SRC_ALPHA). The non-reflective part is
        // scaled by opacity; reflections are added at full strength, like real glass.
        vec3 specular = min(specularSum * (1.0 - fogAmount), color);
        vec3 body = color - specular;
        // Fresnel: grazing angles reflect more and let less of the background through
        float fresnel = dot(fresnelSchlick(NdotV, F0), vec3(1.0 / 3.0));
        float transmitted = (1.0 - albedo.a) * (1.0 - fresnel);

        if (pc.ior > 1.001) {
            // Refraction: look up the opaque scene behind, shifted by the view-space normal.
            // The result is fully opaque, so the blend just replaces what was there.
            vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneColorMap, 0));
            vec3 viewNormal = normalize(mat3(ubo.view) * N);
            // Screen y runs opposite to view-space y (Vulkan's flipped projection)
            vec2 offset = vec2(viewNormal.x, -viewNormal.y) * (pc.ior - 1.0) * 0.08;
            vec3 background = texture(sceneColorMap, clamp(screenUV - offset, vec2(0.001), vec2(0.999))).rgb;
            // Coloured glass tints what's seen through it
            outColor = vec4(body * albedo.a + specular + background * transmissionTint * transmitted, 1.0);
        } else {
            outColor = vec4(body * albedo.a + specular, 1.0 - transmitted);
        }
    } else {
        outColor = vec4(color, albedo.a);
    }
}
