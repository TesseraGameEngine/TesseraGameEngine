#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "lights_common.glsl"
#include "sky_common.glsl"
#include "env_common.glsl"
#include "water_common.glsl"
#include "particle_common.glsl"

layout(binding = 7) uniform sampler2DArrayShadow shadowMap;
#include "sun_shadow_common.glsl"

layout(binding = 1) uniform sampler2D albedoMap;      // the sprite (sheet)
layout(binding = 9) uniform sampler2D sceneColorMap;  // the opaque scene (heat haze bends it)
layout(binding = 24) uniform sampler2D sceneDepthMap; // the opaque scene's depth (soft particles)

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) flat in vec2 inFrame;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in float inSegment;
layout(location = 5) in float inAlong;
layout(location = 0) out vec4 outColor;

// Distance from the camera along its view axis, from a depth-buffer value
float LinearDepth(float depth) {
    if (abs(ubo.proj[2][3]) < 1e-6) return (ubo.proj[3][2] - depth) / ubo.proj[2][2]; // orthographic
    return ubo.proj[3][2] / (depth + ubo.proj[2][2]);
}

vec4 SampleSheet(vec2 uv, float frame) {
    vec2 cells = max(particle.sheet.xy, vec2(1.0));
    float frames = max(particle.sheet.z, 1.0);
    float f0 = mod(floor(frame), frames);
    vec2 cell0 = vec2(mod(f0, cells.x), floor(f0 / cells.x));
    vec4 a = texture(albedoMap, (cell0 + uv) / cells);
    if ((particle.mode.y & PARTICLE_BLEND_FRAMES) == 0 || frames <= 1.0) return a;
    float f1 = mod(f0 + 1.0, frames);
    vec2 cell1 = vec2(mod(f1, cells.x), floor(f1 / cells.x));
    vec4 b = texture(albedoMap, (cell1 + uv) / cells);
    return mix(a, b, fract(frame));
}

// Sun, sky and lights reaching a point in a cloud of particles (no surface: light from all sides
// counts, half as much as on a surface facing it)
vec3 ParticleLight(vec3 p) {
    vec3 sunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 light = (skyOrHdri ? EnvIrradiance(vec3(0.0, 1.0, 0.0), sunDir) : ubo.giSkyColor.rgb) * giStrength;
    uvec2 cluster = lightClusters.clusters[ClusterIndex(p)];
    int lightTotal = LightCountAt(cluster);
    for (int k = 0; k < lightTotal; ++k) {
        int i = LightIndexAt(cluster, k);
        vec3 lightPos = lightList.lights[i].position.xyz;
        float lightType = lightList.lights[i].position.w;
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        if (lightType > 0.5) {
            vec3 lightVec = lightPos - p;
            float dist = length(lightVec);
            float range = max(lightList.lights[i].spotDirAndRange.w, 0.1);
            float rangeFactor = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            float attenuation = rangeFactor * rangeFactor / (1.0 + 0.09 * dist + 0.032 * dist * dist);
            if (lightType > 1.5) {
                float theta = dot(-lightVec / max(dist, 1e-4), normalize(lightList.lights[i].spotDirAndRange.xyz));
                float cosInner = lightList.lights[i].spotAnglesAndShadow.x;
                float cosOuter = lightList.lights[i].spotAnglesAndShadow.y;
                float spot = clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
                attenuation *= spot * spot;
            }
            radiance *= attenuation;
        } else {
            vec3 L = normalize(lightPos);
            if (ubo.skyParams.w > 0.0 && dot(L, sunDir) > 0.9999) radiance *= SkySunTransmittance(L);
            if (ubo.shadowParams.x > 0.5 && i == int(ubo.shadowParams.y + 0.5)) {
                radiance *= SampleShadow(p, L, L, lightList.lights[i].spotAnglesAndShadow.w);
            }
            radiance *= CloudShadow(p, L);
        }
        light += radiance * 0.5 / 3.14159265;
    }
    return light;
}

void main() {
    bool segment = inSegment > 0.5;
    bool textured = (particle.mode.y & PARTICLE_TEXTURED) != 0;
    int blend = particle.mode.x;

    // The sprite's shape and colour
    vec4 sprite;
    if (segment) {
        if (textured) {
            sprite = texture(albedoMap, vec2(inUV.x, inAlong));
        } else {
            float across = 1.0 - abs(inUV.x * 2.0 - 1.0);
            sprite = vec4(1.0, 1.0, 1.0, smoothstep(0.0, 0.7, across));
        }
    } else if (textured) {
        sprite = SampleSheet(inUV, inFrame.x);
    } else {
        float dist = length(inUV - 0.5);
        if (dist > 0.5) discard;
        float soft = smoothstep(0.0, 1.0, 1.0 - dist / 0.5);
        sprite = vec4(1.0, 1.0, 1.0, soft);
        // A hot core for glowing particles
        if (blend == 0) sprite.rgb += pow(soft, 2.2) * 0.75;
    }
    float alpha = sprite.a * inColor.a;

    // Soft particles: fade where the particle meets what's behind it, and near the camera
    float fragDepth = LinearDepth(gl_FragCoord.z);
    if ((particle.mode.y & PARTICLE_SOFT) != 0 && particle.look.x > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthMap, 0));
        float sceneDepth = LinearDepth(texture(sceneDepthMap, screenUV).r);
        alpha *= clamp((sceneDepth - fragDepth) / particle.look.x, 0.0, 1.0);
    }
    if (particle.look.z > 0.0) alpha *= clamp((fragDepth - 0.05) / particle.look.z, 0.0, 1.0);
    if (alpha <= 0.002) discard;

    if (blend == 2) {
        // Heat haze: the scene behind, pushed around by rippling noise where the particle is
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneColorMap, 0));
        float t = ubo.timeParams.x;
        float seed = inFrame.y * 37.0;
        vec2 p = inUV * 6.0;
        vec2 wobble = vec2(sin(p.y * 2.3 + t * 6.0 + seed) + 0.5 * sin(p.x * 4.1 - t * 9.0 + seed * 1.3),
                           cos(p.x * 2.1 - t * 5.0 + seed * 0.7) + 0.5 * cos(p.y * 3.7 + t * 8.0 + seed));
        // Farther haze bends less of the screen
        vec2 offset = wobble * 0.5 * particle.look.w * alpha / max(fragDepth * 0.15, 1.0);
        // The finished scene (copied just before the haze is drawn), bent, laid over itself
        vec3 bent = texture(sceneColorMap, clamp(screenUV + offset, vec2(0.0), vec2(1.0))).rgb;
        outColor = vec4(bent * alpha, alpha);
        return;
    }

    vec3 color = sprite.rgb * inColor.rgb;
    if (blend == 0) {
        color *= particle.look.y; // glow
    } else {
        vec3 base = color;
        if ((particle.mode.y & PARTICLE_LIT) != 0) color *= ParticleLight(inWorldPos);
        // Self glow: the bright parts of the sprite (fire in an explosion's smoke) give light
        if (particle.extra.x > 0.0) {
            float heat = textured ? smoothstep(0.45, 0.85, max(sprite.r, max(sprite.g, sprite.b))) : 1.0;
            color += base * particle.extra.x * heat;
        }
    }

    // Fog between the camera and the particle
    float camDist = length(ubo.cameraPos.xyz - inWorldPos);
    float fogAmount = 0.0;
    float fogDensity = ubo.fogColorAndDensity.a;
    if (fogDensity > 0.0001) {
        float heightFactor = exp(-max(inWorldPos.y, 0.0) * 0.15);
        fogAmount = clamp(1.0 - exp(-camDist * fogDensity * heightFactor), 0.0, 1.0);
    }
    if (blend == 0) {
        // Added light: fog only hides it
        color *= 1.0 - fogAmount;
        if (ViewUnderwater(inWorldPos) && inWorldPos.y < WaterLevel()) color *= exp(-camDist * 0.15);
        outColor = vec4(color * alpha, 0.0);
    } else {
        color = AerialPerspective(color, inWorldPos);
        color = mix(color, ubo.fogColorAndDensity.rgb, fogAmount);
        if (ViewUnderwater(inWorldPos) && inWorldPos.y < WaterLevel()) color = UnderwaterFog(color, camDist);
        outColor = vec4(min(color, vec3(60000.0)) * alpha, alpha);
    }
}
