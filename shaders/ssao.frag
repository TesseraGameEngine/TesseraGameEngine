#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Screen-space ambient occlusion from the prepass depth + view-space normals.
// Output: 1 = fully open, 0 = fully occluded (blurred afterwards by ssao_blur.frag).
layout(binding = 0) uniform sampler2D depthTexture;  // nearest sampler
layout(binding = 1) uniform sampler2D normalTexture; // view-space normal, length 0 where nothing was drawn

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outAO;

const int kSampleCount = 16;

// The camera uses an OpenGL-style projection (depth -1..1) with Vulkan's flipped Y.
// Only these four terms are needed to go between view space and the screen.
float ViewZFromDepth(float d) {
    return -pc.params0.w / (d + pc.params0.z);
}

vec3 ViewPosFromDepth(vec2 uv, float d) {
    float z = ViewZFromDepth(d);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * -z / pc.params0.x, ndc.y * -z / pc.params0.y, z);
}

vec2 UVFromViewPos(vec3 p) {
    vec2 ndc = vec2(pc.params0.x * p.x, pc.params0.y * p.y) / -p.z;
    return ndc * 0.5 + 0.5;
}

float Hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    float depth = texture(depthTexture, inUV).r;
    vec3 normal = texture(normalTexture, inUV).xyz;
    if (depth >= 1.0 || dot(normal, normal) < 0.25) {
        outAO = vec4(1.0); // sky / nothing drawn
        return;
    }
    normal = normalize(normal);
    vec3 position = ViewPosFromDepth(inUV, depth);

    float radius = pc.params1.x;
    float bias = pc.params1.y;

    // Per-pixel rotation of the sample pattern, turned into noise and removed by the blur
    float angle = Hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = randomVec - normal * dot(randomVec, normal);
    if (dot(tangent, tangent) < 1e-4) {
        tangent = abs(normal.x) < 0.9 ? cross(normal, vec3(1.0, 0.0, 0.0)) : cross(normal, vec3(0.0, 1.0, 0.0));
    }
    tangent = normalize(tangent);
    mat3 TBN = mat3(tangent, cross(normal, tangent), normal);

    float occlusion = 0.0;
    for (int i = 0; i < kSampleCount; ++i) {
        // Golden-angle spiral over the hemisphere, packed closer to the surface
        float t = (float(i) + 0.5) / float(kSampleCount);
        float phi = float(i) * 2.3999632;
        float cosTheta = 1.0 - t;
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        vec3 dir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        float scale = mix(0.1, 1.0, t * t);

        vec3 samplePos = position + TBN * dir * (radius * scale);
        vec2 sampleUV = UVFromViewPos(samplePos);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sceneZ = ViewZFromDepth(texture(depthTexture, sampleUV).r);
        // Ignore occluders far in front of the sample (e.g. a foreground object over a wall)
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(position.z - sceneZ), 1e-4));
        // GTAO cosine-weighted horizon occlusion contribution
        vec3 horizonVec = ViewPosFromDepth(sampleUV, texture(depthTexture, sampleUV).r) - position;
        float horizonLen = length(horizonVec);
        float horizonCos = horizonLen > 1e-4 ? clamp(dot(normal, horizonVec / horizonLen), 0.0, 1.0) : 0.0;
        float weight = mix(1.0, horizonCos * 1.35, 0.5);
        occlusion += (sceneZ >= samplePos.z + bias ? weight : 0.0) * rangeCheck;
    }

    float ao = 1.0 - occlusion / float(kSampleCount);
    outAO = vec4(pow(clamp(ao, 0.0, 1.0), pc.params1.w));
}
