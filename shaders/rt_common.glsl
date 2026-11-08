// Hardware ray tracing helpers for the forward shader (compiled into frag_rt.spv only, with
// -DTESSERA_RT=1). The scene's acceleration structure and per-instance surfaces come from
// VulkanRayTracing (descriptor set 1).
#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

// The #extension directives live at the top of shader.frag (they must precede other code)

layout(set = 1, binding = 0) uniform accelerationStructureEXT rtScene;

// One per TLAS instance (instanceCustomIndex), VulkanRayTracing::GpuInstance
struct RTInstance {
    uvec2 vertexAddr;
    uvec2 indexAddr;
    vec4 albedoRough;    // rgb albedo, a roughness
    vec4 emissiveMetal;  // rgb emissive (times intensity), a metallic
    vec4 alpha;          // x = cutoff (0 = opaque), y = albedo texture index (-1 = none)
};
layout(std430, set = 1, binding = 1) readonly buffer RTInstanceBuffer {
    RTInstance rtInstances[];
};
layout(set = 1, binding = 2) uniform sampler2D rtTextures[];

// VulkanRayTracing::GpuVertex
struct RTVertex {
    vec3 pos;
    float u;
    vec3 normal;
    float v;
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer RTVertexBuffer {
    RTVertex v[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RTIndexBuffer {
    uint i[];
};

const uint RT_MASK_OPAQUE = 0x01u;
const uint RT_MASK_FOLIAGE = 0x02u;
const uint RT_MASK_NO_REFLECT = 0x04u;

bool RTShadowsOn()     { return ubo.rtParams.x > 0.5; }
bool RTReflectionsOn() { return ubo.rtParams.y > 0.5; }
bool RTAOOn()          { return ubo.rtParams.z > 0.5; }

// Per-pixel, per-frame noise in [0, 1) (TAA averages the samples over frames)
float RTNoise(vec2 pixel, float salt) {
    vec3 p = vec3(pixel, ubo.timeParams.w * 0.618034 + salt);
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

// Interpolated vertex data of a triangle hit
void RTHitVertexData(RTInstance inst, uint primitive, vec2 bary, out vec3 normal, out vec2 uv) {
    RTIndexBuffer ib = RTIndexBuffer(inst.indexAddr);
    RTVertexBuffer vb = RTVertexBuffer(inst.vertexAddr);
    uint i0 = ib.i[primitive * 3u + 0u];
    uint i1 = ib.i[primitive * 3u + 1u];
    uint i2 = ib.i[primitive * 3u + 2u];
    RTVertex v0 = vb.v[i0];
    RTVertex v1 = vb.v[i1];
    RTVertex v2 = vb.v[i2];
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    normal = v0.normal * w.x + v1.normal * w.y + v2.normal * w.z;
    uv = vec2(v0.u, v0.v) * w.x + vec2(v1.u, v1.v) * w.y + vec2(v2.u, v2.v) * w.z;
}

// Alpha test of a candidate hit on a non-opaque (cutout) instance
bool RTCandidateOpaque(rayQueryEXT rq) {
    uint idx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
    RTInstance inst = rtInstances[idx];
    int tex = int(inst.alpha.y);
    if (inst.alpha.x <= 0.0 || tex < 0) return true;
    vec3 n;
    vec2 uv;
    RTHitVertexData(inst, uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, false)),
                    rayQueryGetIntersectionBarycentricsEXT(rq, false), n, uv);
    float a = textureLod(rtTextures[nonuniformEXT(tex)], uv, 1.0).a;
    return a >= inst.alpha.x;
}

// 1 = nothing in the way within tMax, 0 = blocked (alpha-tested leaves let light through their gaps)
float RTVisibility(vec3 origin, vec3 dir, float tMax) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rtScene, gl_RayFlagsTerminateOnFirstHitEXT,
                          RT_MASK_OPAQUE | RT_MASK_FOLIAGE | RT_MASK_NO_REFLECT, origin, 0.0, dir, tMax);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            RTCandidateOpaque(rq)) {
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
}

// Offset a ray start off the surface to avoid hitting it again (scaled with distance from the camera)
vec3 RTOffsetOrigin(vec3 worldPos, vec3 geomN, vec3 dir) {
    float camDist = length(ubo.cameraPos.xyz - worldPos);
    float bias = 0.01 + camDist * 0.0008;
    vec3 n = dot(geomN, dir) < 0.0 ? -geomN : geomN; // the side the ray leaves from
    return worldPos + n * bias;
}

// A direction toward a light's disc: L jittered within a cone of `coneRadius` radians
vec3 RTConeSample(vec3 L, float coneRadius, vec2 pixel) {
    if (coneRadius <= 0.0) return L;
    float r1 = RTNoise(pixel, 1.7);
    float r2 = RTNoise(pixel, 9.3);
    vec3 up = abs(L.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, L));
    vec3 b = cross(L, t);
    float r = coneRadius * sqrt(r1);
    float phi = 6.2831853 * r2;
    return normalize(L + (t * cos(phi) + b * sin(phi)) * r);
}

// Soft ray-traced shadow toward a directional light (sun / moon)
float RTDirectionalShadow(vec3 worldPos, vec3 geomN, vec3 L) {
    vec3 dir = RTConeSample(L, ubo.rtParams2.x, gl_FragCoord.xy);
    return RTVisibility(RTOffsetOrigin(worldPos, geomN, dir), dir, 10000.0);
}

// Ray-traced shadow toward a point / spot light with a small spherical bulb (radius in metres)
float RTLocalShadow(vec3 worldPos, vec3 geomN, vec3 lightPos, float bulbRadius) {
    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-4);
    vec3 dir = RTConeSample(L, atan(bulbRadius / max(dist, 0.01)), gl_FragCoord.xy);
    return RTVisibility(RTOffsetOrigin(worldPos, geomN, dir), dir, max(dist - bulbRadius, 0.01));
}

// Ambient occlusion from one short cosine-distributed ray (TAA accumulates)
float RTAmbientOcclusion(vec3 worldPos, vec3 N, vec3 geomN) {
    float r1 = RTNoise(gl_FragCoord.xy, 3.1);
    float r2 = RTNoise(gl_FragCoord.xy, 5.9);
    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, N));
    vec3 b = cross(N, t);
    float phi = 6.2831853 * r1;
    float s = sqrt(r2);
    vec3 dir = normalize(t * cos(phi) * s + b * sin(phi) * s + N * sqrt(max(1.0 - r2, 0.0)));
    float radius = max(ubo.rtParams2.y, 0.05);

    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rtScene, gl_RayFlagsNoneEXT, RT_MASK_OPAQUE | RT_MASK_FOLIAGE | RT_MASK_NO_REFLECT,
                          RTOffsetOrigin(worldPos, geomN, dir), 0.0, dir, radius);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            RTCandidateOpaque(rq)) {
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) return 1.0;
    // Nearer occluders darken more
    float t0 = rayQueryGetIntersectionTEXT(rq, true);
    return clamp(t0 / radius, 0.0, 1.0) * 0.85;
}

// Reflection ray. On a hit: true, with the hit's radiance (its material lit by the sun with its own
// shadow ray, plus sky ambient and emission). On a miss: false (use the environment instead).
// `sunRadiance` is the sun's light arriving at the scene (0 at night / without a sun).
bool RTTraceReflection(vec3 worldPos, vec3 geomN, vec3 R, vec3 sunDir, vec3 sunRadiance, float giStrength,
                       out vec3 radiance, out float hitDist) {
    radiance = vec3(0.0);
    hitDist = 0.0;
    float tMax = max(ubo.rtParams2.z, 1.0);
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, rtScene, gl_RayFlagsNoneEXT, RT_MASK_OPAQUE | RT_MASK_FOLIAGE,
                          RTOffsetOrigin(worldPos, geomN, R), 0.0, R, tMax);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT &&
            RTCandidateOpaque(rq)) {
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) return false;

    hitDist = rayQueryGetIntersectionTEXT(rq, true);
    RTInstance inst = rtInstances[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
    vec3 objN;
    vec2 uv;
    RTHitVertexData(inst, uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true)),
                    rayQueryGetIntersectionBarycentricsEXT(rq, true), objN, uv);
    mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
    vec3 n = normalize(transpose(inverse(mat3(o2w))) * objN);
    if (dot(n, R) > 0.0) n = -n; // seen from behind (double-sided surfaces)
    vec3 hitPos = worldPos + R * hitDist;

    vec3 albedo = inst.albedoRough.rgb;
    int tex = int(inst.alpha.y);
    if (tex >= 0) {
        // A coarse mip: reflections are usually blurred and far away
        albedo *= textureLod(rtTextures[nonuniformEXT(tex)], uv, 3.0).rgb;
    }
    float metallic = inst.emissiveMetal.a;
    vec3 diffuseAlbedo = albedo * (1.0 - metallic);

    vec3 lit = vec3(0.0);
    float NdotL = dot(n, sunDir);
    if (NdotL > 0.0 && dot(sunRadiance, vec3(1.0)) > 0.0) {
        float vis = RTVisibility(hitPos + n * 0.02, sunDir, 10000.0);
        lit += (diffuseAlbedo / 3.14159265 + albedo * metallic * 0.15) * sunRadiance * NdotL * vis;
    }
    lit += EnvIrradiance(n, sunDir) * diffuseAlbedo * giStrength;
    lit += inst.emissiveMetal.rgb;
    radiance = lit;
    return true;
}

#endif
