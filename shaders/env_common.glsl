// Environment lighting shared by shader.frag and sky.frag: the procedural sky (sky_common.glsl),
// an optional HDRI image, and reflection probes. Requires common_ubo.glsl and sky_common.glsl.
#ifndef ENV_COMMON_GLSL
#define ENV_COMMON_GLSL

layout(binding = 12) uniform sampler2D hdriMap;       // equirectangular HDR environment (mipmapped)
layout(binding = 13) uniform sampler2DArray probeMap; // reflection probes: 6 layers (cube faces) per probe, mipmapped

bool HdriActive() {
    return ubo.envParams.x > 0.5;
}

// Equirectangular lookup for a world direction, rotated around the vertical axis
vec2 EquirectUV(vec3 d) {
    float r = ubo.envParams.z;
    vec3 rd = vec3(cos(r) * d.x - sin(r) * d.z, d.y, sin(r) * d.x + cos(r) * d.z);
    float u = atan(rd.z, rd.x) / 6.2831853 + 0.5;
    float v = acos(clamp(rd.y, -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
}

float HdriMaxLod() {
    return float(textureQueryLevels(hdriMap) - 1);
}

vec3 HdriRadiance(vec3 d, float lod) {
    return textureLod(hdriMap, EquirectUV(d), lod).rgb * ubo.envParams.y;
}

// Light arriving at a surface facing n, from the sky or the HDRI
vec3 EnvIrradiance(vec3 n, vec3 sunDir) {
    if (HdriActive()) {
        // A very blurry mip approximates the cosine-weighted average
        return HdriRadiance(n, max(HdriMaxLod() - 1.0, 0.0));
    }
    return SkyIrradiance(n, sunDir);
}

// Glossy reflection along R: sharp for smooth surfaces, blurrier as roughness rises
vec3 EnvSpecular(vec3 R, float roughness, vec3 sunDir) {
    if (HdriActive()) {
        vec3 reflected = HdriRadiance(R, roughness * HdriMaxLod());
        // Clouds over the HDRI show in sharp reflections too
        if (roughness < 0.5 && CloudsActive()) {
            vec4 clouds = SkyClouds(R, sunDir, 3, HdriRadiance(vec3(0.0, 1.0, 0.0), max(HdriMaxLod() - 1.0, 0.0)));
            reflected = mix(reflected, clouds.rgb, clouds.a * (1.0 - roughness * 2.0));
        }
        return reflected;
    }
    return SkySpecular(R, roughness, sunDir);
}

// ---- Baked GI irradiance volume (VulkanVolumeTexture): L1 SH per probe, one texture per colour ----
layout(binding = 20) uniform sampler3D giVolumeR;
layout(binding = 21) uniform sampler3D giVolumeG;
layout(binding = 22) uniform sampler3D giVolumeB;

// 1 inside the baked volume, fading to 0 over the outer half probe spacing
float BakedGIWeight(vec3 p) {
    if (ubo.giVolumeMin.w < 0.5) return 0.0;
    vec3 dims = max(ubo.giVolumeDims.xyz, vec3(2.0));
    vec3 q = (p - ubo.giVolumeMin.xyz) / max(ubo.giVolumeSize.xyz, vec3(1e-3));
    vec3 margin = 0.5 / (dims - 1.0);
    vec3 inside = smoothstep(-margin, margin, q) * (1.0 - smoothstep(1.0 - margin, 1.0 + margin, q));
    return inside.x * inside.y * inside.z;
}

// Baked indirect light reaching a surface facing n, in the same units as EnvIrradiance (the
// cosine-weighted average radiance, i.e. irradiance / pi). L1 SH with the cosine lobe applied.
vec3 BakedIrradiance(vec3 p, vec3 n) {
    vec3 dims = max(ubo.giVolumeDims.xyz, vec3(2.0));
    vec3 q = clamp((p - ubo.giVolumeMin.xyz) / max(ubo.giVolumeSize.xyz, vec3(1e-3)), 0.0, 1.0);
    vec3 uvw = (q * (dims - 1.0) + 0.5) / dims; // probe k sits at the centre of texel k
    vec4 r = texture(giVolumeR, uvw);
    vec4 g = texture(giVolumeG, uvw);
    vec4 b = texture(giVolumeB, uvw);
    const float c0 = 0.282095;              // Y00
    const float c1 = 0.488603 * 2.0 / 3.0;  // Y1m times the cosine lobe's band-1 factor (2/3)
    vec3 l0 = vec3(r.x, g.x, b.x);
    vec3 l1 = vec3(dot(r.yzw, n), dot(g.yzw, n), dot(b.yzw, n));
    return max(c0 * l0 + c1 * l1, vec3(0.0)) * ubo.giVolumeSize.w;
}

// ---- Reflection probes ----
// Faces are captured in the order +X, -X, +Y, -Y, +Z, -Z with a 90 degree camera looking along
// the face direction (see main.cpp, probe capture). This reproduces that camera's projection.

float ProbeMaxLod() {
    return float(textureQueryLevels(probeMap) - 1);
}

vec3 SampleProbe(int probe, vec3 dir, float lod) {
    vec3 a = abs(dir);
    int face;
    vec3 f;
    if (a.x >= a.y && a.x >= a.z) { face = dir.x > 0.0 ? 0 : 1; f = vec3(sign(dir.x), 0.0, 0.0); }
    else if (a.y >= a.z)          { face = dir.y > 0.0 ? 2 : 3; f = vec3(0.0, sign(dir.y), 0.0); }
    else                          { face = dir.z > 0.0 ? 4 : 5; f = vec3(0.0, 0.0, sign(dir.z)); }

    vec3 up = (abs(f.y) > 0.99) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    float depth = dot(dir, f);
    vec2 ndc = vec2(dot(dir, s), -dot(dir, u)) / depth; // the capture camera flips Y (Vulkan)
    vec2 uv = clamp(ndc * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    return textureLod(probeMap, vec3(uv, float(probe * 6 + face)), lod).rgb;
}

// Which probe (if any) covers a point, and how strongly: 1 inside, fading to 0 at the radius
// (or, for a box-projected probe, near the box faces)
float ProbeInfluence(vec3 worldPos, out int probe) {
    probe = -1;
    float best = 0.0;
    int count = int(ubo.probeParams.x + 0.5);
    for (int i = 0; i < count; ++i) {
        vec4 pr = ubo.probePosRadius[i];
        vec4 box = ubo.probeBoxExtents[i];
        float weight;
        if (box.w > 0.5) {
            vec3 q = abs(worldPos - pr.xyz) / max(box.xyz, vec3(0.001));
            weight = 1.0 - smoothstep(0.9, 1.0, max(q.x, max(q.y, q.z)));
        } else {
            float dist = length(worldPos - pr.xyz);
            weight = 1.0 - smoothstep(pr.w * 0.75, pr.w, dist);
        }
        if (weight > best) {
            best = weight;
            probe = i;
        }
    }
    return best;
}

// Box projection (parallax correction): a probe stores what it sees from its own centre, so a
// reflection ray from elsewhere in the room is first traced to the room's box, and the probe is
// sampled in the direction of that hit point. Reflections then line up on floors and walls.
vec3 ProbeDirection(int probe, vec3 worldPos, vec3 dir) {
    vec4 box = ubo.probeBoxExtents[probe];
    if (box.w < 0.5) return dir;
    vec3 center = ubo.probePosRadius[probe].xyz;
    vec3 safeDir = mix(dir, vec3(1e-5), equal(dir, vec3(0.0)));
    vec3 t1 = (center + box.xyz - worldPos) / safeDir;
    vec3 t2 = (center - box.xyz - worldPos) / safeDir;
    vec3 tFar = max(t1, t2);
    float t = max(min(tFar.x, min(tFar.y, tFar.z)), 0.0);
    return normalize(worldPos + dir * t - center);
}

// ---- Real-time Voxel Global Illumination (VXGI) cone tracing ----
layout(binding = 40) uniform sampler3D vxgiVolume;

// 1 inside the VXGI volume, fading smoothly to 0 over the outer 8% border
float VXGIWeight(vec3 p) {
    if (ubo.vxgiParams.x < 0.5 || ubo.vxgiParams.z <= 0.001) return 0.0;
    vec3 q = (p - ubo.vxgiCenter.xyz) / ubo.vxgiParams.z;
    vec3 inside = smoothstep(0.0, 0.08, q) * (1.0 - smoothstep(0.92, 1.0, q));
    return inside.x * inside.y * inside.z;
}

// Traces a cone through the mipmapped 3D voxel radiance + opacity volume.
// Returns premultiplied accumulated radiance in rgb and accumulated occlusion in a.
vec4 TraceVXGICone(vec3 origin, vec3 dir, float tanHalfAngle, float maxDist) {
    float voxelSize = max(ubo.vxgiCenter.w, 0.05);
    float volumeExtent = max(ubo.vxgiParams.z, 0.1);
    float maxMip = max(ubo.vxgiParams.w, 0.0);
    vec4 accum = vec4(0.0);
    float t = voxelSize * 0.75;
    for (int step = 0; step < 16 && t < maxDist && accum.a < 0.97; ++step) {
        vec3 p = origin + dir * t;
        vec3 uvw = (p - ubo.vxgiCenter.xyz) / volumeExtent;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;
        float diameter = max(2.0 * tanHalfAngle * t, voxelSize);
        float lod = clamp(log2(diameter / voxelSize), 0.0, maxMip);
        vec4 vox = textureLod(vxgiVolume, uvw, lod);
        float w = 1.0 - accum.a;
        accum.rgb += w * vox.rgb;
        accum.a   += w * vox.a;
        t += max(diameter * 0.55, voxelSize * 0.55);
    }
    return accum;
}

// Evaluates 5 hemisphere cones around N. Returns rgb = indirect diffuse bounce, a = voxel AO (1 = open).
vec4 VXGIDiffuse(vec3 worldPos, vec3 N, vec3 geomNormal) {
    float voxelSize = max(ubo.vxgiCenter.w, 0.05);
    float maxDist = ubo.vxgiParams.z * 0.65;
    vec3 origin = worldPos + geomNormal * (voxelSize * 0.75);

    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    const float aperture = 0.57735;
    vec4 c0 = TraceVXGICone(origin, N, aperture, maxDist);
    vec4 c1 = TraceVXGICone(origin, normalize(0.5 * N + 0.866025 * T), aperture, maxDist);
    vec4 c2 = TraceVXGICone(origin, normalize(0.5 * N - 0.866025 * T), aperture, maxDist);
    vec4 c3 = TraceVXGICone(origin, normalize(0.5 * N + 0.866025 * B), aperture, maxDist);
    vec4 c4 = TraceVXGICone(origin, normalize(0.5 * N - 0.866025 * B), aperture, maxDist);

    vec4 sum = 0.28 * c0 + 0.18 * (c1 + c2 + c3 + c4);
    float voxelAO = clamp(1.0 - sum.a * 0.75, 0.18, 1.0);
    return vec4(sum.rgb, voxelAO);
}

// Evaluates a single glossy reflection cone along R whose aperture widens with surface roughness.
vec4 VXGISpecular(vec3 worldPos, vec3 R, vec3 geomNormal, float roughness) {
    float voxelSize = max(ubo.vxgiCenter.w, 0.05);
    float maxDist = ubo.vxgiParams.z * 0.8;
    vec3 origin = worldPos + geomNormal * (voxelSize * 0.75);
    float tanHalfAngle = mix(0.04, 0.55, roughness * roughness);
    vec4 spec = TraceVXGICone(origin, R, tanHalfAngle, maxDist);
    vec3 rgb = spec.a > 1e-3 ? (spec.rgb / max(spec.a, 0.25)) : vec3(0.0);
    return vec4(rgb, clamp(spec.a, 0.0, 1.0));
}

#endif
