#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "fluid_common.glsl"

layout(binding = 9) uniform sampler2D sceneColorMap;   // opaque scene, seen through the fluid
layout(binding = 26) uniform sampler2D fluidSurface;   // smoothed surface distance (0 = none)
layout(binding = 27) uniform sampler2D fluidThickness; // fluid along each view ray (m)
layout(binding = 32) uniform sampler2D fluidBodyColor; // nearest particle's body: rgb = colour, a = colour depth (m)

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// View-space position of the fluid surface at a pixel
vec3 SurfacePoint(ivec2 p, ivec2 size, float distance) {
    vec2 ndc = (vec2(p) + 0.5) / vec2(size) * 2.0 - 1.0;
    return vec3(distance * (ndc.x + ubo.proj[2][0]) / ubo.proj[0][0],
                distance * (ndc.y + ubo.proj[2][1]) / ubo.proj[1][1],
                -distance);
}

// The fluid as one continuous surface: normals from the smoothed distances, refraction of what's
// behind tinted by the thickness, Fresnel reflection of the sky / probes and the sun's highlight
void main() {
    ivec2 size = textureSize(fluidSurface, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = texelFetch(fluidSurface, p, 0).r;
    if (d <= 0.0) discard;
    vec3 center = SurfacePoint(p, size, d);

    // Normal: of the two neighbours on each axis, use the one on the same surface (nearer depth).
    // Neighbours a couple of pixels away average out what's left of the particle bumps.
    vec3 dx = vec3(0.0), dy = vec3(0.0);
    float bestX = 1e9, bestY = 1e9;
    for (int s = -2; s <= 2; s += 4) {
        ivec2 qx = clamp(p + ivec2(s, 0), ivec2(0), size - 1);
        float sx = texelFetch(fluidSurface, qx, 0).r;
        if (sx > 0.0 && abs(sx - d) < bestX) {
            bestX = abs(sx - d);
            dx = (SurfacePoint(qx, size, sx) - center) * float(s);
        }
        ivec2 qy = clamp(p + ivec2(0, s), ivec2(0), size - 1);
        float sy = texelFetch(fluidSurface, qy, 0).r;
        if (sy > 0.0 && abs(sy - d) < bestY) {
            bestY = abs(sy - d);
            dy = (SurfacePoint(qy, size, sy) - center) * float(s);
        }
    }
    vec3 viewNormal = normalize(cross(dx, dy) + vec3(0.0, 0.0, 1e-6));
    if (viewNormal.z < 0.0) viewNormal = -viewNormal; // facing the camera
    mat3 viewToWorld = transpose(mat3(ubo.view));
    vec3 N = normalize(viewToWorld * viewNormal);
    vec3 worldPos = viewToWorld * (center - vec3(ubo.view[3]));
    vec3 V = normalize(ubo.cameraPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 1e-3);

    // What's behind, bent by the surface and coloured by how much fluid it is seen through
    float thickness = texelFetch(fluidThickness, p, 0).r;
    // Each volume's own colour (the nearest particle's body here)
    vec4 body = texelFetch(fluidBodyColor, p, 0);
    vec3 fluidColor = body.a > 0.0 ? body.rgb : fluid.color.rgb;
    float colorDepth = max(body.a > 0.0 ? body.a : fluid.params.y, 1e-3);
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec2 bend = vec2(viewNormal.x, -viewNormal.y) * 0.05 * clamp(thickness / colorDepth, 0.0, 1.0);
    vec3 behind = texture(sceneColorMap, clamp(uv - bend, vec2(0.001), vec2(0.999))).rgb;
    vec3 tint = pow(max(fluidColor, vec3(1e-3)), vec3(thickness / colorDepth));

    // Reflections and highlights
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    vec3 skySunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 R = reflect(-V, N);
    vec3 reflection = skyOrHdri ? EnvSpecular(R, 0.04, skySunDir)
                                : mix(vec3(0.05, 0.04, 0.03), ubo.giSkyColor.rgb, R.y * 0.5 + 0.5);
    int probe;
    float probeWeight = ProbeInfluence(worldPos, probe);
    if (probeWeight > 0.0) reflection = mix(reflection, SampleProbe(probe, ProbeDirection(probe, worldPos, R), 0.0), probeWeight);
    reflection *= giStrength;
    vec3 ambient = (skyOrHdri ? EnvIrradiance(N, skySunDir) : ubo.giSkyColor.rgb) * giStrength;
    vec3 highlight = vec3(0.0);
    vec3 sunLight = vec3(0.0);
    uint directional = lightClusters.lightCounts.y;
    for (uint k = 0u; k < directional; ++k) {
        uint i = lightClusters.directionalLights[k];
        vec3 L = normalize(lightList.lights[i].position.xyz);
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        if (ubo.skyParams.w > 0.0 && dot(L, skySunDir) > 0.9999) radiance *= SkySunTransmittance(L);
        vec3 H = normalize(V + L);
        highlight += radiance * pow(max(dot(N, H), 0.0), 400.0) * 4.0 * max(dot(N, L), 0.0);
        sunLight += radiance * max(dot(N, L), 0.0);
    }
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    // Light scattered inside thick, coloured fluid (milk, juice) shows its colour even in front of dark things
    vec3 scattered = fluidColor * (ambient + sunLight / 3.14159) * (1.0 - exp(-thickness / colorDepth)) * 0.35;
    vec3 color = behind * tint * (1.0 - fresnel) + reflection * fresnel + highlight + scattered;

    vec4 clip = ubo.proj * vec4(center, 1.0);
    gl_FragDepth = clip.z / clip.w;
    outColor = vec4(min(color, vec3(60000.0)), 1.0);
}
