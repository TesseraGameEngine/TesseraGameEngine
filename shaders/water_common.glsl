// The scene's ocean as seen from other shaders: caustics on surfaces under the water, and the
// water fog when the camera is under it. main.cpp fills ubo.waterParams / waterColor /
// waterBounds from the ocean around the camera. Requires common_ubo.glsl, lights_common.glsl,
// sky_common.glsl and env_common.glsl.
#ifndef WATER_COMMON_GLSL
#define WATER_COMMON_GLSL

// The waterline on screen at NDC x (a camera half in the water)
float WaterlineAt(float ndcX) {
    float x = clamp(ndcX * 0.5 + 0.5, 0.0, 1.0) * 15.0;
    int i = min(int(floor(x)), 14);
    float f = x - float(i);
    float a = ubo.waterline[i / 4][i % 4];
    float b = ubo.waterline[(i + 1) / 4][(i + 1) % 4];
    return mix(a, b, f);
}

// Whether the view looks out from under the water toward this point. A camera half in the water
// sees both: the part of the screen below the waterline is under it.
bool ViewUnderwater(vec3 worldPos) {
    if (ubo.waterParams.x < 1.5) return ubo.waterParams.x > 0.5;
    vec4 clip = ubo.unjitteredViewProj * vec4(worldPos, 1.0);
    if (clip.w <= 1e-5) return ubo.waterParams.w > 0.5;
    vec2 ndc = clip.xy / clip.w;
    bool positiveSide = ndc.y > WaterlineAt(ndc.x);
    return ubo.waterParams.w > 0.5 ? positiveSide : !positiveSide;
}

float WaterLevel() {
    return ubo.waterParams.y;
}

// Inside the ocean's area and below its calm surface
bool UnderWater(vec3 p) {
    return ubo.waterColor.a > 0.5 && p.y < ubo.waterParams.y &&
           all(greaterThanEqual(p.xz, ubo.waterBounds.xy)) && all(lessThanEqual(p.xz, ubo.waterBounds.zw));
}

// Light scattered inside the water: the water's colour lit by the sky and the directional
// lights from above
vec3 WaterScatterColor() {
    vec3 skySunDir = normalize(ubo.skyParams.xyz + vec3(0.0, 1e-5, 0.0));
    bool skyOrHdri = ubo.skyParams.w > 0.0 || HdriActive();
    float giStrength = (ubo.enableGI == 1) ? ubo.giSkyColor.a : 0.08;
    vec3 light = (skyOrHdri ? EnvIrradiance(vec3(0.0, 1.0, 0.0), skySunDir) : ubo.giSkyColor.rgb) * giStrength;
    uint directional = lightClusters.lightCounts.y;
    for (uint k = 0u; k < directional; ++k) {
        uint i = lightClusters.directionalLights[k];
        vec3 L = normalize(lightList.lights[i].position.xyz);
        vec3 radiance = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
        if (ubo.skyParams.w > 0.0 && dot(L, skySunDir) > 0.9999) radiance *= SkySunTransmittance(L);
        light += radiance * max(L.y, 0.0) / 3.14159265;
    }
    return ubo.waterColor.rgb * light;
}

// With the camera under water, light from a point at `distance` fades into the water's
// colour; red goes first. Deeper cameras see darker water.
vec3 UnderwaterFog(vec3 color, float distance) {
    float clarity = max(ubo.waterParams.z, 0.05);
    float cameraDepth = max(ubo.waterParams.y - ubo.cameraPos.y, 0.0);
    vec3 scatter = WaterScatterColor() * exp(-cameraDepth / (clarity * 4.0));
    vec3 tint = mix(vec3(0.35, 0.75, 0.85), vec3(1.0), exp(-distance * 0.15));
    return mix(scatter, color * tint, exp(-distance / clarity));
}

// Bright moving lines of sunlight focused by the waves (after joltz0r's well-known iterated
// pattern), 0..~1
float CausticPattern(vec2 p, float time) {
    vec2 q = mod(p * 6.2831853, 6.2831853) - 250.0;
    vec2 i = q;
    float c = 1.0;
    const float intensity = 0.005;
    for (int n = 0; n < 4; ++n) {
        float t = time * (1.0 - 3.5 / float(n + 1));
        i = q + vec2(cos(t - i.x) + sin(t + i.y), sin(t - i.y) + cos(t + i.x));
        c += 1.0 / length(vec2(q.x / (sin(i.x + t) / intensity), q.y / (cos(i.y + t) / intensity)));
    }
    c /= 4.0;
    c = 1.17 - pow(c, 1.4);
    return clamp(pow(abs(c), 8.0), 0.0, 2.0);
}

// How much of the directional light reaches a point under the water: dimmed with depth and
// broken into caustics. 1 above the water or without an ocean.
float UnderwaterSunlight(vec3 p) {
    if (!UnderWater(p)) return 1.0;
    float depth = ubo.waterParams.y - p.y;
    float clarity = max(ubo.waterParams.z, 0.05);
    float pattern = CausticPattern(p.xz * 0.15, ubo.timeParams.x * 0.5);
    float strength = smoothstep(0.0, 0.25, depth) * exp(-depth / (clarity * 2.0));
    return exp(-depth / (clarity * 3.0)) * mix(1.0, 0.45 + 2.2 * pattern, strength);
}

#endif
