// Weather on surfaces (ubo.weatherParams): rain soaks them (darker where porous, glossier
// everywhere, puddles in the flat ground with raindrop rings) and snow settles on what faces up,
// banking up against walls and capping edges as deep as the snow lies. The shelter map (the
// topmost surface around the camera, seen from above) keeps all of it out from under roofs.
// Requires common_ubo.glsl.
#ifndef WEATHER_COMMON_GLSL
#define WEATHER_COMMON_GLSL

layout(binding = 28) uniform sampler2DArray shelterMap; // depth from straight above (VulkanPipeline::m_shelterMaps)

float WeatherHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float WeatherNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(WeatherHash(i), WeatherHash(i + vec2(1.0, 0.0)), u.x),
               mix(WeatherHash(i + vec2(0.0, 1.0)), WeatherHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

bool ShelterActive() {
    return ubo.shelterParams.x > 0.5;
}

// Height of the topmost surface at xz (m), or a very low value where the map has nothing (open
// sky) or doesn't reach
float ShelterHeight(vec2 xz) {
    if (!ShelterActive()) return -1e9;
    vec4 clip = ubo.shelterMatrix * vec4(xz.x, 0.0, xz.y, 1.0);
    vec2 uv = clip.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return -1e9;
    float depth = textureLod(shelterMap, vec3(uv, 0.0), 0.0).r;
    if (depth >= 0.9999) return -1e9;
    return ubo.shelterParams.y - depth * ubo.shelterParams.z;
}

// How open to the sky a point is (0 under a roof .. 1 in the open), softened over a few texels.
// n: the surface normal; walls look just outside themselves, so outer walls count as open.
float SkyExposure(vec3 p, vec3 n) {
    if (!ShelterActive()) return 1.0;
    float texel = ubo.shelterParams.w;
    vec2 base = p.xz + n.xz * texel * 2.0;
    float open = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float top = ShelterHeight(base + vec2(x, y) * texel);
            open += p.y >= top - 0.05 - texel ? 1.0 : 0.0;
        }
    }
    return open / 9.0;
}

// Rings spreading from raindrops: a slope (xz) to tilt a flat surface's normal by
vec2 RainRipples(vec2 xz, float rain) {
    vec2 slope = vec2(0.0);
    for (int layer = 0; layer < 3; ++layer) {
        vec2 p = xz * 4.0 + float(layer) * vec2(17.3, 9.1); // one drop per 25 cm cell
        vec2 cell = floor(p);
        if (WeatherHash(cell + 3.7 + float(layer)) > rain) continue; // light rain: fewer drops
        float t = fract(ubo.timeParams.x * 1.4 + WeatherHash(cell));
        vec2 center = cell + 0.3 + 0.4 * vec2(WeatherHash(cell + 1.7), WeatherHash(cell + 5.3));
        vec2 d = p - center;
        float r = length(d);
        // One thin ring: a bump that rises then falls across its width
        float ring = t * 0.4;
        float x = (r - ring) / 0.03;
        float wave = -x * exp(-x * x) * (1.0 - t) * (1.0 - t);
        slope += d / max(r, 1e-4) * wave;
    }
    return slope * 0.5;
}

const vec3 kSnowColor = vec3(0.88, 0.9, 0.94);

// geomN: the surface's own (unbumped) world normal
void ApplyWeather(vec3 worldPos, vec3 geomN, inout vec3 albedo, inout vec3 N, inout float roughness, inout float metallic) {
    float wet = ubo.weatherParams.z;
    float snow = ubo.weatherParams.w;
    if (wet <= 0.001 && snow <= 0.001) return;
    float up = clamp(geomN.y, 0.0, 1.0);
    float exposure = SkyExposure(worldPos, geomN);
    wet *= exposure;

    if (snow > 0.001) {
        float drift = WeatherNoise(worldPos.xz * 1.3) * 0.6 + WeatherNoise(worldPos.xz * 5.7) * 0.4;
        // Settles first on what faces up, its edge broken up by drifts
        float cover = clamp(snow * 1.8 - (1.0 - up) * 2.2 - drift * 0.5, 0.0, 1.0);
        cover = smoothstep(0.0, 1.0, cover) * exposure;
        // Depth: snow banks up against walls and objects from the open ground beside them,
        // and lies as a layer over their top edges
        float depth = ubo.weatherParams2.x * snow;
        if (depth > 0.005 && ShelterActive() && up < 0.7) {
            vec2 outward = normalize(geomN.xz + vec2(1e-5));
            float texel = ubo.shelterParams.w;
            float bank = depth * (0.7 + 0.6 * WeatherNoise(worldPos.xz * 2.1 + worldPos.y * 0.7));
            float outside = ShelterHeight(worldPos.xz + outward * texel * 2.5);
            float aboveGround = worldPos.y - outside;
            float buried = smoothstep(bank + 0.015, bank - 0.015, aboveGround) * step(-0.1, aboveGround);
            float inside = ShelterHeight(worldPos.xz - outward * texel * 2.5);
            float belowTop = inside - worldPos.y;
            float layer = min(depth * 0.6, 0.15) * (0.7 + 0.6 * drift);
            float capped = step(-0.02, belowTop) * smoothstep(layer + 0.01, layer - 0.01, belowTop) * (inside > worldPos.y - 0.02 ? 1.0 : 0.0);
            cover = max(cover, max(buried, capped));
        }
        // Soft drifts: the snow surface rolls a little
        vec2 e = vec2(0.15, 0.0);
        vec2 slope = vec2(WeatherNoise(worldPos.xz * 1.3 + e.xy) - WeatherNoise(worldPos.xz * 1.3 - e.xy),
                          WeatherNoise(worldPos.xz * 1.3 + e.yx) - WeatherNoise(worldPos.xz * 1.3 - e.yx)) * min(depth * 4.0, 1.0);
        vec3 snowN = normalize(geomN - vec3(slope.x, 0.0, slope.y) * up);
        albedo = mix(albedo, kSnowColor, cover);
        roughness = mix(roughness, 0.7, cover);
        metallic = mix(metallic, 0.0, cover);
        N = normalize(mix(N, snowN, cover * 0.85));
        wet *= 1.0 - cover;
    }

    if (wet > 0.001) {
        // Rough, porous surfaces darken as they soak; all of them turn glossier. Walls get less.
        float w = wet * mix(0.55, 1.0, up);
        float porous = smoothstep(0.25, 0.8, roughness) * (1.0 - metallic);
        albedo *= mix(1.0, 0.5, w * porous);
        roughness = mix(roughness, roughness * 0.25 + 0.05, w * 0.85);

        // Puddles gather in low spots of flat ground, growing with the wetness
        float flatness = smoothstep(0.9, 0.98, up);
        float ground = WeatherNoise(worldPos.xz * 0.35) * 0.65 + WeatherNoise(worldPos.xz * 1.4) * 0.35;
        float level = wet * 0.6;
        float puddle = flatness * smoothstep(level, level - 0.06, ground);
        albedo *= mix(1.0, 0.85, puddle);
        roughness = mix(roughness, 0.02, puddle);
        N = normalize(mix(N, geomN, puddle));

        // Raindrop rings: on puddles, and faintly on wet flat ground (only where rain reaches)
        float rain = ubo.weatherParams.x * exposure;
        if (rain > 0.01) {
            vec2 s = RainRipples(worldPos.xz, rain) * flatness * mix(0.1, 1.0, puddle);
            N = normalize(N + vec3(s.x, 0.0, s.y));
        }
    }
}

// Light from a lightning flash reaching a surface facing n
vec3 LightningLight(vec3 n) {
    return vec3(0.7, 0.75, 1.0) * ubo.lightningParams.x * 1.5 * (0.35 + 0.65 * clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

#endif
