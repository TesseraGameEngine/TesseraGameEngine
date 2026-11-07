#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "weather_common.glsl"

// Rain, snow and splashes (VulkanPipeline::DrawPrecipitation): one quad per drop, flake or
// splash, placed from its instance number and the time in a box that follows the camera, so
// nothing is stored or simulated on the CPU. Rain is a streak along its fall, snow a drifting
// flake; both stop at the first surface above the ground (the shelter map), so none falls under
// a roof. Splashes burst on the topmost surface (or the ocean) where drops land.
layout(push_constant) uniform Precip {
    vec4 params;     // x = kind (0 snow, 1 rain, 2 splashes), y = box size (m), z = amount (0..1)
    vec4 unused[7];
} precip;

layout(location = 0) out vec2 outCorner; // x across (-1..1), y along (0 head .. 1 tail) / up
layout(location = 1) out float outFade;
layout(location = 2) out float outLife;  // splashes: 0 .. 1 through the burst
layout(location = 3) out float outSeed;  // splashes: shapes each one differently

float Hash(uint n) {
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff);
}

const vec2 kCorners[6] = vec2[6](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                                 vec2(-1.0, 0.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

void Hide() {
    gl_Position = vec4(0.0, 0.0, -1.0, 1.0); // outside the depth range: dropped
    outFade = 0.0;
    outCorner = vec2(0.0);
    outLife = 0.0;
    outSeed = 0.0;
}

// Where a drop at xz lands: the topmost surface, or the ocean above it
float LandingHeight(vec2 xz, out bool known) {
    float h = ShelterHeight(xz);
    if (ubo.waterColor.a > 0.5 && all(greaterThanEqual(xz, ubo.waterBounds.xy)) && all(lessThanEqual(xz, ubo.waterBounds.zw))) {
        h = max(h, ubo.waterParams.y);
    }
    known = h > -1e8;
    return h;
}

void main() {
    uint id = uint(gl_InstanceIndex);
    float kind = precip.params.x;
    float box = precip.params.y;
    float t = ubo.timeParams.x;
    vec3 cam = ubo.cameraPos.xyz;
    vec2 corner = kCorners[gl_VertexIndex];
    outCorner = corner;
    outLife = 0.0;
    outSeed = 0.0;

    if (kind > 1.5) {
        // Splash: every slot bursts somewhere new a few times a second
        const float rate = 2.5;
        float cycleTime = t * rate + Hash(id * 5u);
        float cycle = floor(cycleTime);
        float life = fract(cycleTime) / 0.2; // the burst lasts the first 20% of the cycle
        if (life >= 1.0) { Hide(); return; }
        uint c = uint(mod(cycle, 65536.0));
        vec2 r = vec2(Hash(id * 7919u + c * 104729u), Hash(id * 6271u + c * 15485863u + 17u));
        vec2 xz = cam.xz + (r * 2.0 - 1.0) * box * 0.5;
        bool known;
        float ground = LandingHeight(xz, known);
        if (!known) { Hide(); return; }
        vec3 p = vec3(xz.x, ground, xz.y);
        vec3 toCam = cam - p;
        float dist = length(toCam);
        // A small crown facing the camera (upright)
        vec3 side = normalize(cross(vec3(0.0, 1.0, 0.0), toCam / max(dist, 1e-3)) + vec3(1e-5, 0.0, 0.0));
        float width = 0.025 + life * 0.04 + dist * 0.0015;
        float height = 0.045 + dist * 0.001;
        vec3 worldPos = p + side * corner.x * width + vec3(0.0, corner.y * height, 0.0);
        outLife = life;
        outSeed = r.x * 37.0 + r.y * 11.0;
        outFade = smoothstep(box * 0.5, box * 0.3, dist) * smoothstep(0.3, 1.0, dist);
        gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
        return;
    }

    bool rain = kind > 0.5;
    vec3 seed = vec3(Hash(id * 3u), Hash(id * 3u + 1u), Hash(id * 3u + 2u));
    vec2 wind = ubo.cloudWind.xy;
    vec3 velocity = rain ? vec3(wind.x * 0.35, -(8.0 + seed.x * 2.0), wind.y * 0.35)
                         : vec3(wind.x * 0.25, -(0.8 + seed.x * 0.5), wind.y * 0.25);
    vec3 p = seed * box + velocity * t;
    if (!rain) p.xz += vec2(sin(t * 1.3 + seed.y * 20.0), cos(t * 1.1 + seed.z * 20.0)) * 0.25; // flakes sway

    // Wrapped into the box around the camera
    p = cam + mod(p - cam + box * 0.5, vec3(box)) - box * 0.5;

    // Under a roof (or below the ground): nothing falls here
    if (p.y < ShelterHeight(p.xz) - 0.02) { Hide(); return; }

    vec3 toCam = cam - p;
    float dist = max(length(toCam), 1e-3);
    vec3 worldPos;
    if (rain) {
        // A thin streak facing the camera, as long as the drop falls in about 1/20 s
        vec3 axis = normalize(velocity);
        vec3 side = normalize(cross(axis, toCam / dist) + vec3(1e-5, 0.0, 0.0));
        float width = 0.003 + dist * 0.0012;
        float len = length(velocity) * 0.05;
        worldPos = p + side * corner.x * width - axis * corner.y * len;
    } else {
        mat3 cameraToWorld = transpose(mat3(ubo.view));
        float size = 0.012 + seed.y * 0.014 + dist * 0.0008;
        vec2 c = vec2(corner.x, corner.y * 2.0 - 1.0);
        worldPos = p + (cameraToWorld[0] * c.x + cameraToWorld[1] * c.y) * size;
    }
    // Fades out toward the edge of the box and right in front of the camera
    outFade = smoothstep(box * 0.5, box * 0.3, dist) * smoothstep(0.3, 1.0, dist);
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
