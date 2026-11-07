#include "SkyClock.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>

namespace SkyClock {

namespace {

constexpr float kPi = 3.14159265f;

float Smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

glm::vec3 CelestialDirection(float hours, float latitudeDegrees, float northDegrees, float declinationDegrees) {
    // Hour angle: 0 at the highest point (noon for the sun), negative before it (rising in the east)
    const float hourAngle = (hours - 12.0f) / 24.0f * 2.0f * kPi;
    const float latitude = glm::radians(std::clamp(latitudeDegrees, -89.0f, 89.0f));
    const float declination = glm::radians(std::clamp(declinationDegrees, -89.0f, 89.0f));
    const float cosD = std::cos(declination), sinD = std::sin(declination);
    const float cosL = std::cos(latitude), sinL = std::sin(latitude);
    const float cosH = std::cos(hourAngle);
    // East, up, and toward the equator (south in the north)
    glm::vec3 dir(-cosD * std::sin(hourAngle), sinL * sinD + cosL * cosD * cosH, sinL * cosD * cosH - cosL * sinD);
    const float north = glm::radians(northDegrees);
    const float c = std::cos(north), s = std::sin(north);
    dir = glm::vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);
    return glm::normalize(dir);
}

float SunDeclination(float dayOfYear) {
    return 23.44f * std::sin(2.0f * kPi * (dayOfYear - 81.0f) / 365.0f);
}

float DayLengthHours(float latitudeDegrees, float dayOfYear) {
    const float latitude = glm::radians(std::clamp(latitudeDegrees, -89.0f, 89.0f));
    const float declination = glm::radians(SunDeclination(dayOfYear));
    const float cosH = -std::tan(latitude) * std::tan(declination);
    if (cosH <= -1.0f) return 24.0f;
    if (cosH >= 1.0f) return 0.0f;
    return 2.0f * std::acos(cosH) / (2.0f * kPi) * 24.0f;
}

State Evaluate(const SceneEnvironment& env) {
    State state;
    const float hours = std::fmod(std::fmod(env.timeOfDay, 24.0f) + 24.0f, 24.0f);
    const float sunDeclination = SunDeclination(env.dayOfYear);
    state.sunDir = CelestialDirection(hours, env.latitude, env.northAngle, sunDeclination);
    const float phase = std::fmod(std::fmod(env.moonPhase, 1.0f) + 1.0f, 1.0f);
    // The moon trails the sun by its phase (a full moon rises as the sun sets) and, as it does,
    // swings to the opposite declination: a full moon rides low in summer and high in winter
    state.moonDir = CelestialDirection(hours - phase * 24.0f, env.latitude, env.northAngle,
                                       sunDeclination * std::cos(phase * 2.0f * kPi));
    state.moonIllumination = env.moonEnabled ? 0.5f * (1.0f - std::cos(phase * 2.0f * kPi)) : 0.0f;
    // The stars turn once a sidereal day: a little ahead of the sun each day of the year
    state.starRotation = (hours - 12.0f) / 24.0f * 2.0f * kPi + (env.dayOfYear - 81.0f) / 365.0f * 2.0f * kPi;

    if (state.sunDir.y > -0.04f) {
        // Sunlight: the sky shaders dim and redden it as the sun sets (SkySunTransmittance);
        // without the procedural sky it fades here instead
        state.lightDir = state.sunDir;
        if (!env.enableSky) state.lightScale = Smoothstep(-0.04f, 0.06f, state.sunDir.y);
    } else {
        // Moonlight: faint and blue, growing as the sky darkens and with the lit share of the moon
        state.lightDir = state.moonDir;
        state.lightTint = glm::vec3(0.62f, 0.72f, 1.0f);
        state.lightScale = 0.06f * std::max(env.moonBrightness, 0.0f) * state.moonIllumination *
                           Smoothstep(-0.04f, -0.14f, state.sunDir.y) * Smoothstep(-0.02f, 0.1f, state.moonDir.y);
    }
    return state;
}

void Advance(SceneEnvironment& env, float dt) {
    if (!env.timeOfDayEnabled || env.dayLengthMinutes <= 0.0f || dt <= 0.0f) return;
    env.timeOfDay += dt * 24.0f / (env.dayLengthMinutes * 60.0f);
    if (env.timeOfDay >= 24.0f && env.advanceCalendar) {
        const float days = std::floor(env.timeOfDay / 24.0f);
        env.dayOfYear = std::fmod(env.dayOfYear - 1.0f + days, 365.0f) + 1.0f;
        env.moonPhase = std::fmod(env.moonPhase + days / 29.53f, 1.0f);
    }
    env.timeOfDay = std::fmod(std::fmod(env.timeOfDay, 24.0f) + 24.0f, 24.0f);
}

} // namespace SkyClock
