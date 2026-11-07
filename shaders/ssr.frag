#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Screen-space reflections, on the HDR scene before TAA. Each reflective pixel traces its
// reflection ray through the depth buffer; where the ray hits visible geometry, that geometry's
// colour replaces the environment reflection (sky / HDRI / probe) by the Fresnel amount.
// Rays that leave the screen or go behind objects fade back to the environment reflection.
layout(binding = 0) uniform sampler2D sceneTexture;   // HDR scene
layout(binding = 1) uniform sampler2D depthTexture;   // prepass depth (point sampled)
layout(binding = 2) uniform sampler2D normalTexture;  // prepass: xyz = view-space normal, w = packed surface

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// params0 = projection terms (P[0][0], P[1][1], P[2][2], P[3][2])
// params1 = (strength, max distance (m), thickness (m), 1 = prepass valid)
// params2 = (texel size xy, frame noise offset, unused)
const int kSteps = 40;
const int kRefineSteps = 5;

float LinearDepth(float d) {
    return pc.params0.w / (d + pc.params0.z);
}

vec3 ViewPosition(vec2 uv, float d) {
    float dist = LinearDepth(d);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * dist / pc.params0.x, ndc.y * dist / pc.params0.y, -dist);
}

vec3 ProjectToScreen(vec3 viewPos) {
    float w = -viewPos.z;
    vec2 ndc = vec2(pc.params0.x * viewPos.x, pc.params0.y * viewPos.y) / w;
    return vec3(ndc * 0.5 + 0.5, w);
}

float Noise(vec2 fragCoord) {
    fragCoord += 5.588238 * pc.params2.z;
    return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 color = SanitizeHDR(texture(sceneTexture, inUV).rgb);
    outColor = vec4(color, 1.0);

    float strength = pc.params1.x;
    if (strength <= 0.0 || pc.params1.w < 0.5) return;

    vec4 normalSample = texture(normalTexture, inUV);
    if (normalSample.w < 0.5) return; // nothing drawn in the prepass (sky, transparent-only)
    float packedSurface = normalSample.w - 1.0;
    float metallic = floor(packedSurface / 16.0 + 0.01) / 15.0;
    float roughness = mod(packedSurface + 0.01, 16.0) / 15.0;
    float roughFade = 1.0 - smoothstep(0.25, 0.6, roughness);
    if (roughFade <= 0.0) return;

    float depth = texture(depthTexture, inUV).r;
    if (depth >= 1.0) return;
    vec3 viewPos = ViewPosition(inUV, depth);
    vec3 N = normalize(normalSample.xyz);
    vec3 V = normalize(-viewPos);
    vec3 R = reflect(-V, N);

    // Fresnel (Schlick) with a single F0: 0.04 for dielectrics, toward 1 for metals
    float NdotV = max(dot(N, V), 0.0);
    float F0 = mix(0.04, 0.9, metallic);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    // Rays toward the camera reflect things behind it, which the screen doesn't contain
    float facingFade = 1.0 - smoothstep(0.0, 0.5, R.z);
    if (facingFade <= 0.0) return;

    float maxDistance = pc.params1.y;
    float thickness = pc.params1.z;
    vec3 origin = viewPos + N * (0.01 + 0.002 * -viewPos.z);
    float jitter = Noise(gl_FragCoord.xy);

    // March with steps that grow with distance (detail near the surface, reach far away)
    vec3 hitUV = vec3(-1.0);
    float prevT = 0.0;
    for (int i = 1; i <= kSteps; ++i) {
        float f = (float(i) - 1.0 + jitter) / float(kSteps);
        float t = maxDistance * f * f;
        vec3 p = origin + R * t;
        if (p.z > -0.05) break; // behind the camera
        vec3 screen = ProjectToScreen(p);
        if (any(lessThan(screen.xy, vec2(0.0))) || any(greaterThan(screen.xy, vec2(1.0)))) break;

        float sceneDepth = texture(depthTexture, screen.xy).r;
        if (sceneDepth >= 1.0) { prevT = t; continue; } // sky: keep going, never a hit
        float sceneDist = LinearDepth(sceneDepth);
        float rayDist = screen.z;
        float diff = rayDist - sceneDist;
        if (diff > 0.0 && diff < thickness + 0.02 * rayDist) {
            // Refine between the last miss and this step
            float lo = prevT, hi = t;
            for (int r = 0; r < kRefineSteps; ++r) {
                float mid = 0.5 * (lo + hi);
                vec3 s = ProjectToScreen(origin + R * mid);
                float d = texture(depthTexture, s.xy).r;
                if (d < 1.0 && s.z > LinearDepth(d)) hi = mid; else lo = mid;
            }
            hitUV = ProjectToScreen(origin + R * hi);
            break;
        }
        prevT = t;
    }
    if (hitUV.x < 0.0) return;

    // Fade near the screen border and toward the end of the ray
    vec2 edge = smoothstep(vec2(0.0), vec2(0.08), hitUV.xy) * (1.0 - smoothstep(vec2(0.92), vec2(1.0), hitUV.xy));
    float confidence = edge.x * edge.y * facingFade * roughFade;
    // Back faces of the hit geometry (prepass normal facing away from the ray) aren't real hits
    vec3 hitNormal = texture(normalTexture, hitUV.xy).xyz;
    confidence *= smoothstep(-0.2, 0.2, -dot(normalize(hitNormal + vec3(1e-5)), R));

    vec3 reflected = SanitizeHDR(texture(sceneTexture, hitUV.xy).rgb);
    float weight = clamp(fresnel * confidence * strength, 0.0, 1.0);
    outColor = vec4(mix(color, reflected, weight), 1.0);
}
