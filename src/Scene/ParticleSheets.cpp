#include "ParticleSheets.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include <algorithm>
#include <cmath>

namespace ParticleSheets {

namespace {

float Hash(int x, int y, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / 16777215.0f;
}

float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Wraps a lattice coordinate for noise that tiles (period 0: no tiling)
int Wrap(int v, int period) { return period > 0 ? ((v % period) + period) % period : v; }

// Value noise in 3D, 0..1, tiling in y and z with the given lattice periods (0 = none)
float Noise(float x, float y, float z, int periodY, int periodZ) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const float fx = Smooth(x - x0), fy = Smooth(y - y0), fz = Smooth(z - z0);
    const int ya = Wrap(y0, periodY), yb = Wrap(y0 + 1, periodY);
    float c[2];
    for (int k = 0; k < 2; ++k) {
        const int zk = Wrap(z0 + k, periodZ);
        const float a = Hash(x0, ya, zk) + (Hash(x0 + 1, ya, zk) - Hash(x0, ya, zk)) * fx;
        const float b = Hash(x0, yb, zk) + (Hash(x0 + 1, yb, zk) - Hash(x0, yb, zk)) * fx;
        c[k] = a + (b - a) * fy;
    }
    return c[0] + (c[1] - c[0]) * fz;
}

// Fractal noise; each octave doubles the frequency and the tiling periods
float Fbm(float x, float y, float z, int octaves, int periodY = 0, int periodZ = 0) {
    float sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += Noise(x, y, z, periodY, periodZ) * amplitude;
        norm += amplitude;
        x = x * 2.0f + 17.3f; // offset so the octaves' lattices don't line up
        y *= 2.0f;
        z *= 2.0f;
        periodY *= 2;
        periodZ *= 2;
        amplitude *= 0.5f;
    }
    return sum / norm;
}

// Noise that loops over the animation (t 0..1) at full contrast: it evolves along a time axis
// that tiles, while rising (scrolling down the noise by rise lattice units, which it tiles in)
float LoopFbm(float x, float y, float t, int rise, int octaves) {
    constexpr int kTimePeriod = 3;
    return Fbm(x, y - t * static_cast<float>(rise), t * kTimePeriod, octaves, rise, kTimePeriod);
}

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float SmoothStep(float e0, float e1, float x) { return Smooth(Clamp01((x - e0) / (e1 - e0))); }

struct Pixel { float r, g, b, a; };

// Colour of fire by how hot it is (0 = dull red at the edge, 1 = white-yellow core)
Pixel FlameColor(float heat) {
    heat = Clamp01(heat);
    struct Stop { float at, r, g, b; };
    const Stop stops[] = { { 0.0f, 0.55f, 0.08f, 0.02f }, { 0.3f, 0.95f, 0.38f, 0.06f }, { 0.6f, 1.0f, 0.72f, 0.22f },
                           { 0.85f, 1.0f, 0.92f, 0.6f }, { 1.0f, 1.0f, 0.98f, 0.9f } };
    for (int i = 0; i < 4; ++i) {
        if (heat <= stops[i + 1].at) {
            const float f = (heat - stops[i].at) / (stops[i + 1].at - stops[i].at);
            return { stops[i].r + (stops[i + 1].r - stops[i].r) * f, stops[i].g + (stops[i + 1].g - stops[i].g) * f,
                     stops[i].b + (stops[i + 1].b - stops[i].b) * f, 1.0f };
        }
    }
    return { 1.0f, 0.98f, 0.9f, 1.0f };
}

// x, y: -1..1 in the frame (y up); t: 0..1 through the animation
Pixel Fire(float x, float y, float t) {
    const float h = (y + 0.9f) / 1.8f; // 0 at the base, 1 at the top of the frame
    // Rising turbulence that bends the flame and tears tongues off its top
    const float warp = LoopFbm(x * 1.6f + 11.0f, h * 1.8f, t, 2, 3) - 0.5f;
    const float n = LoopFbm(x * 3.0f + warp * 1.2f, h * 3.2f, t, 4, 5);
    const float xs = x + warp * 0.55f * h;
    const float width = 0.78f * std::pow(Clamp01(1.0f - h * 0.92f), 0.8f) * SmoothStep(-0.04f, 0.1f, h);
    const float body = std::sqrt(Clamp01(1.0f - std::abs(xs) / std::max(width, 1e-3f)));
    // The noise eats more of the flame the higher it rises, leaving tongues
    float density = body * 1.2f - (1.0f - n) * (0.3f + 1.35f * h) + 0.05f;
    density = SmoothStep(0.0f, 0.3f, density);
    const float heat = Clamp01(body * 1.15f - h * 0.75f + (n - 0.5f) * 0.6f + 0.15f) * density;
    Pixel p = FlameColor(heat);
    // Soft at the base too, so overlapping flames don't show their bottom edges
    p.a = density * SmoothStep(1.0f, 0.75f, h) * SmoothStep(0.0f, 0.3f, h + (n - 0.5f) * 0.2f);
    return p;
}

Pixel Smoke(float x, float y, float t) {
    // A billowing puff: big soft lumps (domain-warped noise), slowly turning over
    const float wx = LoopFbm(x * 0.9f + 3.0f, y * 0.9f, t, 0, 3) - 0.5f;
    const float wy = LoopFbm(x * 0.9f + 9.0f, y * 0.9f + 4.0f, t, 0, 3) - 0.5f;
    const float qx = x + wx * 0.7f, qy = y + wy * 0.7f;
    const float n = LoopFbm(qx * 1.5f, qy * 1.5f, t, 0, 5);
    const float r = std::sqrt(x * x + y * y);
    float density = SmoothStep(0.92f, 0.3f, r + (0.5f - n) * 0.65f);
    density *= Clamp01(0.35f + 1.1f * n);
    // Lit from above: the tops of the billows are lighter, their undersides darker
    const float above = LoopFbm(qx * 1.5f, (qy + 0.1f) * 1.5f, t, 0, 5);
    const float shade = std::clamp(0.66f + (above - n) * 2.6f + y * 0.18f, 0.4f, 1.0f);
    return { shade, shade, shade * 1.02f, Clamp01(density) };
}

Pixel Explosion(float x, float y, float t) {
    // A fireball that bursts out, cools from white through orange to dark smoke, and thins away
    const float grow = 1.0f - std::pow(1.0f - t, 2.6f);
    const float radius = 0.42f + 0.48f * grow;
    const float wx = Fbm(x * 1.3f + 2.0f, y * 1.3f, t * 0.8f, 3) - 0.5f;
    const float wy = Fbm(x * 1.3f + 7.0f, y * 1.3f + 5.0f, t * 0.8f, 3) - 0.5f;
    const float qx = x + wx * 0.6f, qy = y + wy * 0.6f + t * 0.15f;
    const float n = Fbm(qx * 2.6f + 5.0f, qy * 2.6f, t * 1.4f, 5);
    const float r = std::sqrt(x * x + y * y) / radius;
    // Lumpy billows round the edge
    const float density = SmoothStep(1.0f, 0.55f, r + (0.5f - n) * 0.55f) * Clamp01(0.45f + 0.9f * n) * (1.0f - SmoothStep(0.72f, 1.0f, t));
    const float heat = Clamp01((1.0f - t * 1.35f) * (1.25f - r * 0.75f) + (n - 0.5f) * 0.9f);
    Pixel p;
    if (heat > 0.12f) {
        p = FlameColor((heat - 0.12f) / 0.88f);
    } else {
        // Smoke: dark grey, lighter on top
        const float smoke = 0.16f + 0.12f * Clamp01(0.5f + y * 0.6f) + (n - 0.5f) * 0.1f;
        const float f = heat / 0.12f;
        const Pixel fire = FlameColor(0.0f);
        p = { smoke + (fire.r - smoke) * f, smoke + (fire.g - smoke) * f, smoke + (fire.b - smoke) * f, 1.0f };
    }
    p.a = Clamp01(density * 1.3f);
    return p;
}

Pixel Sparks(float x, float y, float t) {
    // A twinkling four-pointed glint
    const float pulse = 0.65f + 0.35f * std::sin(t * 6.2831853f * 2.0f);
    const float angle = t * 0.785f;
    const float c = std::cos(angle), s = std::sin(angle);
    const float u = x * c - y * s, v = x * s + y * c;
    const float r = std::sqrt(x * x + y * y);
    const float core = std::exp(-r * r * 40.0f);
    const float arms = std::exp(-std::abs(u) * 30.0f) * std::exp(-std::abs(v) * 2.5f * (1.4f - pulse * 0.4f)) +
                       std::exp(-std::abs(v) * 30.0f) * std::exp(-std::abs(u) * 2.5f * (1.4f - pulse * 0.4f));
    const float halo = std::exp(-r * r * 6.0f) * 0.35f;
    const float a = Clamp01((core + arms * 0.8f + halo) * pulse) * SmoothStep(1.0f, 0.8f, std::max(std::abs(x), std::abs(y)));
    return { 1.0f, 1.0f, 1.0f, a };
}

} // namespace

std::string TextureKey(ParticleSheet sheet) {
    return "@particlesheet:" + std::to_string(static_cast<int>(sheet));
}

std::vector<uint8_t> Generate(ParticleSheet sheet) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4, 0);
    const int frames = kColumns * kRows;
    for (int frame = 0; frame < frames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(frames);
        const int ox = (frame % kColumns) * kFrameSize;
        const int oy = (frame / kColumns) * kFrameSize;
        for (int j = 0; j < kFrameSize; ++j) {
            for (int i = 0; i < kFrameSize; ++i) {
                const float x = (static_cast<float>(i) + 0.5f) / kFrameSize * 2.0f - 1.0f;
                const float y = 1.0f - (static_cast<float>(j) + 0.5f) / kFrameSize * 2.0f;
                Pixel p{ 1.0f, 1.0f, 1.0f, 0.0f };
                switch (sheet) {
                    case ParticleSheet::Fire: p = Fire(x, y, t); break;
                    case ParticleSheet::Smoke: p = Smoke(x, y, t); break;
                    case ParticleSheet::Explosion: p = Explosion(x, y, t); break;
                    case ParticleSheet::Sparks: p = Sparks(x, y, t); break;
                    default: break;
                }
                // Nothing touches the frame's border (no bleeding into the next frame)
                const float edge = SmoothStep(0.96f, 0.84f, std::max(std::abs(x), std::abs(y)));
                p.a = Clamp01(p.a) * edge;
                uint8_t* out = &pixels[(static_cast<size_t>(oy + j) * kSize + static_cast<size_t>(ox + i)) * 4];
                out[0] = static_cast<uint8_t>(Clamp01(p.r) * 255.0f + 0.5f);
                out[1] = static_cast<uint8_t>(Clamp01(p.g) * 255.0f + 0.5f);
                out[2] = static_cast<uint8_t>(Clamp01(p.b) * 255.0f + 0.5f);
                out[3] = static_cast<uint8_t>(p.a * 255.0f + 0.5f);
            }
        }
    }
    return pixels;
}

} // namespace ParticleSheets
