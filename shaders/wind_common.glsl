// Foliage swaying in the wind (MAT_WIND): a vertex moves along the wind, more the higher it is
// above its model's origin, with gusts that roll across the land. The same sway in the main,
// prepass and shadow passes keeps them lined up. Requires common_ubo.glsl.
#ifndef WIND_COMMON_GLSL
#define WIND_COMMON_GLSL

// worldPos: the vertex; origin: the model's origin in the world; localHeight: the vertex's height
// in the model (metres before scaling)
vec3 WindSway(vec3 worldPos, vec3 origin, float localHeight) {
    vec2 wind = ubo.cloudWind.xy; // the weather's wind (zero without clouds: a light breeze then)
    float speed = length(wind);
    vec2 dir = speed > 0.01 ? wind / speed : vec2(0.8, 0.6);
    float strength = 0.04 + min(speed, 30.0) * 0.012;
    float t = ubo.timeParams.x;
    float phase = dot(origin.xz, vec2(0.37, 0.53));
    float gust = 0.6 + 0.4 * (sin(t * 1.3 + phase) * 0.6 + sin(t * 2.9 + phase * 1.7) * 0.4);
    // Tall things (trees) are stiffer than grass
    float h = max(localHeight, 0.0);
    float bend = h * mix(1.0, 0.12, smoothstep(0.6, 3.0, h)) * strength * gust;
    vec2 flutter = vec2(dir.y, -dir.x) * sin(t * 4.1 + phase * 3.0 + h * 2.0) * bend * 0.25;
    worldPos.xz += dir * bend + flutter;
    worldPos.y -= bend * bend * 0.3; // bending lowers the tip a little
    return worldPos;
}

#endif
