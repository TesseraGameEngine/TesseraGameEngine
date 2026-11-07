// Physically based atmosphere (Earth): Rayleigh scattering by air, Mie scattering by haze and
// ozone absorption, in kilometres. Shared by sky_view_lut.comp; VulkanSkyResources.cpp has the
// same numbers for the sun's transmittance on the CPU.
#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

const float kGroundRadius = 6360.0;
const float kTopRadius = 6460.0;
const vec3 kRayleighScattering = vec3(5.802, 13.558, 33.1) * 1e-3;
const float kRayleighHeight = 8.0;
const float kMieScattering = 3.996e-3;
const float kMieExtinction = 4.40e-3;
const float kMieHeight = 1.2;
const float kMieG = 0.8;
const vec3 kOzoneAbsorption = vec3(0.650, 1.881, 0.085) * 1e-3;

float OzoneDensity(float h) {
    return max(0.0, 1.0 - abs(h - 25.0) / 15.0);
}

vec3 AtmosphereExtinction(float h) {
    return kRayleighScattering * exp(-h / kRayleighHeight) + vec3(kMieExtinction * exp(-h / kMieHeight)) +
           kOzoneAbsorption * OzoneDensity(h);
}

// Distance along a ray from radius r (direction cosine mu to the zenith) to the sphere of radius R,
// or -1 when it misses
float RaySphere(float r, float mu, float R) {
    float d = r * r * (mu * mu - 1.0) + R * R;
    if (d < 0.0) return -1.0;
    float s = sqrt(d);
    float t = -r * mu + s;
    return t >= 0.0 ? t : -1.0;
}

bool RayHitsGround(float r, float mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + kGroundRadius * kGroundRadius >= 0.0;
}

// Light surviving from the top of the atmosphere to radius r along a ray with zenith cosine mu
vec3 AtmosphereTransmittance(float r, float mu) {
    if (RayHitsGround(r, mu)) return vec3(0.0);
    float length = RaySphere(r, mu, kTopRadius);
    if (length <= 0.0) return vec3(1.0);
    const int steps = 12;
    float dt = length / float(steps);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + 0.5) * dt;
        float ri = sqrt(r * r + t * t + 2.0 * r * mu * t);
        depth += AtmosphereExtinction(ri - kGroundRadius) * dt;
    }
    return exp(-depth);
}

float RayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * 3.14159265) * (1.0 + cosTheta * cosTheta);
}

float MiePhase(float cosTheta) {
    // Cornette-Shanks
    float g = kMieG;
    float k = 3.0 / (8.0 * 3.14159265) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cosTheta * cosTheta) / pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5);
}

// Sky-view LUT mapping: u = angle from the sun's azimuth (0..pi), v = elevation, packed tighter
// near the horizon
vec2 SkyViewUV(float elevation, float azimuthFromSun, float horizon) {
    float lat = elevation - horizon;
    float coord = sign(lat) * sqrt(abs(lat) / 1.5707963);
    return vec2(clamp(azimuthFromSun / 3.14159265, 0.0, 1.0), clamp(0.5 + 0.5 * coord, 0.0, 1.0));
}

void SkyViewAngles(vec2 uv, float horizon, out float elevation, out float azimuthFromSun) {
    float coord = uv.y * 2.0 - 1.0;
    elevation = sign(coord) * coord * coord * 1.5707963 + horizon;
    azimuthFromSun = uv.x * 3.14159265;
}

// The horizon's elevation seen from height h (km): a little below level
float HorizonElevation(float h) {
    float r = kGroundRadius + max(h, 0.001);
    return -acos(clamp(kGroundRadius / r, -1.0, 1.0));
}

#endif
