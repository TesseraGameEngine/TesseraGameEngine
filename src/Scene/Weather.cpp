#include "Weather.h"
#include "Scene.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Weather {

namespace {

constexpr float kPi = 3.14159265f;

float Random(SceneEnvironment& env) {
    env.weatherRandom = env.weatherRandom * 1664525u + 1013904223u;
    return static_cast<float>(env.weatherRandom >> 8) / 16777216.0f;
}

void Approach(float& value, float target, float k) {
    value += (target - value) * k;
}

// Brightness of a strike `age` seconds in: a bright flash, a second flicker, then darkness
float FlashAt(float age) {
    if (age < 0.0f) return 0.0f;
    float flash = std::exp(-age * 14.0f);
    if (age > 0.16f) flash = std::max(flash, 0.75f * std::exp(-(age - 0.16f) * 9.0f));
    if (age > 0.42f) flash = std::max(flash, 0.35f * std::exp(-(age - 0.42f) * 12.0f));
    return flash;
}

} // namespace

const char* PresetName(int preset) {
    static const char* kNames[PresetCount] = { "Clear", "Cloudy", "Overcast", "Rain", "Storm", "Snow" };
    return (preset >= 0 && preset < PresetCount) ? kNames[preset] : "Clear";
}

int PresetFromName(const std::string& name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (int p = 0; p < PresetCount; ++p) {
        std::string preset(PresetName(p));
        std::transform(preset.begin(), preset.end(), preset.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (preset == lower) return p;
    }
    return -1;
}

Targets PresetTargets(int preset) {
    switch (preset) {
    case Cloudy:   return { 0.55f, 0.85f, 0.0f, 0.0f, 0.0f,   8.0f,  0.0f };
    case Overcast: return { 0.92f, 0.95f, 0.0f, 0.0f, 0.004f, 10.0f, 0.0f };
    case Rain:     return { 0.97f, 1.0f,  0.6f, 0.0f, 0.008f, 12.0f, 0.0f };
    case Storm:    return { 1.0f,  1.0f,  1.0f, 0.0f, 0.012f, 25.0f, 6.0f };
    case Snow:     return { 0.95f, 0.95f, 0.0f, 0.7f, 0.01f,  5.0f,  0.0f };
    default:       return { 0.15f, 0.6f,  0.0f, 0.0f, 0.0f,   4.0f,  0.0f };
    }
}

void Strike(SceneEnvironment& env) {
    env.lightningAge = 0.0f;
    env.boltAzimuth = Random(env) * 2.0f * kPi;
    env.boltSeed = Random(env) * 100.0f;
    ++env.strikeCount;
    // Between about 200 m and 2 km away; the thunder comes once the sound has travelled. It
    // waits in a free slot (or replaces the one due last)
    const float distance = 200.0f + Random(env) * 1800.0f;
    int slot = 0;
    for (int i = 0; i < 4; ++i) {
        if (env.thunderTimers[i] < 0.0f) { slot = i; break; }
        if (env.thunderTimers[i] > env.thunderTimers[slot]) slot = i;
    }
    env.thunderTimers[slot] = distance / 343.0f;
    env.thunderDistances[slot] = distance;
}

void Update(SceneEnvironment& env, float dt) {
    dt = std::clamp(dt, 0.0f, 0.25f);
    if (env.weatherEnabled) {
        // Blend toward the preset: most of the way there after weatherTransition seconds
        const Targets target = PresetTargets(env.weatherPreset);
        const float k = 1.0f - std::exp(-3.0f * dt / std::max(env.weatherTransition, 0.1f));
        env.cloudsEnabled = true;
        Approach(env.cloudCoverage, target.cloudCoverage, k);
        Approach(env.cloudOpacity, target.cloudOpacity, k);
        Approach(env.rainAmount, target.rain, k);
        Approach(env.snowAmount, target.snow, k);
        Approach(env.fogDensity, target.fogDensity, k);
        Approach(env.windSpeed, target.windSpeed, k);
        Approach(env.lightningRate, target.lightningRate, k);
        // Rain soaks surfaces within about 10 s (heavy rain) and they dry over a few minutes;
        // snow settles over about half a minute and melts slowly once it stops
        if (env.rainAmount > 0.02f) env.wetness += dt * env.rainAmount * 0.12f;
        else env.wetness -= dt * 0.006f;
        if (env.snowAmount > 0.02f) env.snowCover += dt * env.snowAmount * 0.04f;
        else env.snowCover -= dt * 0.004f;
        env.wetness = std::clamp(env.wetness, 0.0f, 1.0f);
        env.snowCover = std::clamp(env.snowCover, 0.0f, 1.0f);
    }

    // Lightning: strikes at random, on average lightningRate a minute
    if (env.lightningRate > 0.05f) {
        env.lightningTimer -= dt;
        if (env.lightningTimer <= 0.0f) {
            Strike(env);
            const float mean = 60.0f / env.lightningRate;
            env.lightningTimer = std::max(1.5f, -std::log(std::max(Random(env), 1e-4f)) * mean);
        }
    }
    if (env.lightningAge >= 0.0f) {
        env.lightningAge += dt;
        if (env.lightningAge > 1.2f) env.lightningAge = -1.0f;
    }
    env.lightningFlash = FlashAt(env.lightningAge);
    for (int i = 0; i < 4; ++i) {
        if (env.thunderTimers[i] < 0.0f) continue;
        env.thunderTimers[i] -= dt;
        if (env.thunderTimers[i] < 0.0f) {
            env.thunderTimers[i] = -1.0f;
            env.thunderDistance = env.thunderDistances[i];
            env.thunderDue = true;
        }
    }
}

} // namespace Weather
