#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "sky_common.glsl"
#include "weather_common.glsl"

layout(push_constant) uniform Precip {
    vec4 params;     // x = kind (0 snow, 1 rain, 2 splashes), y = box size (m), z = amount (0..1)
    vec4 unused[7];
} precip;

layout(location = 0) in vec2 inCorner;
layout(location = 1) in float inFade;
layout(location = 2) in float inLife;
layout(location = 3) in float inSeed;
layout(location = 0) out vec4 outColor;

// Lit by the sky (and a lightning flash); premultiplied alpha for the transparent pass
void main() {
    float kind = precip.params.x;
    vec3 sunDir = normalize(ubo.skyParams.xyz);
    vec3 light = ubo.skyParams.w > 0.0 ? SkyIrradiance(vec3(0.0, 1.0, 0.0), sunDir) : ubo.giSkyColor.rgb * ubo.giSkyColor.a;
    light += LightningLight(vec3(0.0, 1.0, 0.0));
    float alpha;
    vec3 color;
    if (kind > 1.5) {
        // Splash: a few droplets thrown up and out, each on its own arc, rising then falling back
        vec2 p = vec2(inCorner.x, inCorner.y);
        alpha = 0.0;
        for (int k = 0; k < 4; ++k) {
            float h1 = WeatherHash(vec2(inSeed, float(k) * 3.1));
            float h2 = WeatherHash(vec2(float(k) * 1.7, inSeed + 5.0));
            float out_ = (h1 * 2.0 - 1.0) * inLife;               // sideways as it flies
            float up = (0.4 + 0.6 * h2) * 4.0 * inLife * (1.0 - inLife); // up and back down
            vec2 drop = vec2(out_, 0.08 + up * 0.9);
            alpha += smoothstep(0.16, 0.05, length((p - drop) * vec2(1.0, 1.6)));
        }
        alpha = min(alpha, 1.0) * (1.0 - inLife * inLife) * 0.5;
        color = light * 1.5;
    } else if (kind > 0.5) {
        // Rain: brightest down the middle, fading toward both ends
        alpha = (1.0 - abs(inCorner.x)) * smoothstep(0.0, 0.25, inCorner.y) * smoothstep(1.0, 0.5, inCorner.y) * 0.3;
        color = light * 1.6;
    } else {
        vec2 c = vec2(inCorner.x, inCorner.y * 2.0 - 1.0);
        alpha = smoothstep(1.0, 0.3, length(c)) * 0.9;
        vec3 sun = ubo.skyParams.w > 0.0 ? SkySunColor(sunDir) * SkySunTransmittance(sunDir) * 0.4 : vec3(0.0);
        color = light * 1.3 + sun;
    }
    alpha *= inFade;
    if (alpha <= 0.002) discard;
    outColor = vec4(color * alpha, alpha);
}
