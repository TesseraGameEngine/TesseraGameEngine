#include "Bc7Encoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Bc7 {
namespace {

// Interpolation weights for 4-bit indices (from the BC7 specification), out of 64
constexpr int kWeights[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

int Interpolate(int a, int b, int weight) {
    return ((64 - weight) * a + weight * b + 32) >> 6;
}

// Rounds an RGBA endpoint to what mode 6 can store: 7 bits per channel plus one low bit shared by
// all four channels. Tries both low bits and keeps the closer result.
void QuantizeEndpoint(const float value[4], int out[4]) {
    float bestError = 1e30f;
    for (int p = 0; p < 2; ++p) {
        int candidate[4];
        float error = 0.0f;
        for (int c = 0; c < 4; ++c) {
            const int q = std::clamp(static_cast<int>(std::lround((value[c] - p) * 0.5f)), 0, 127);
            candidate[c] = q * 2 + p;
            const float d = candidate[c] - value[c];
            error += d * d;
        }
        if (error < bestError) {
            bestError = error;
            std::memcpy(out, candidate, sizeof(candidate));
        }
    }
}

// Picks the closest of the 16 palette colours for each texel; returns the total squared error
int AssignIndices(const uint8_t* rgba, const int e0[4], const int e1[4], uint8_t indices[16]) {
    int palette[16][4];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) palette[i][c] = Interpolate(e0[c], e1[c], kWeights[i]);

    int total = 0;
    for (int t = 0; t < 16; ++t) {
        int best = 0, bestError = 1 << 30;
        for (int i = 0; i < 16; ++i) {
            int error = 0;
            for (int c = 0; c < 4; ++c) {
                const int d = palette[i][c] - rgba[t * 4 + c];
                error += d * d;
            }
            if (error < bestError) { bestError = error; best = i; }
        }
        indices[t] = static_cast<uint8_t>(best);
        total += bestError;
    }
    return total;
}

// Least-squares endpoints for fixed indices: minimises sum |texel - ((1 - w) e0 + w e1)|^2
bool FitEndpoints(const uint8_t* rgba, const uint8_t indices[16], float e0[4], float e1[4]) {
    float a = 0, b = 0, c = 0, x0[4] = {}, x1[4] = {};
    for (int t = 0; t < 16; ++t) {
        const float w = kWeights[indices[t]] / 64.0f;
        a += (1 - w) * (1 - w);
        b += (1 - w) * w;
        c += w * w;
        for (int ch = 0; ch < 4; ++ch) {
            x0[ch] += (1 - w) * rgba[t * 4 + ch];
            x1[ch] += w * rgba[t * 4 + ch];
        }
    }
    const float det = a * c - b * b;
    if (std::abs(det) < 1e-6f) return false;
    for (int ch = 0; ch < 4; ++ch) {
        e0[ch] = std::clamp((c * x0[ch] - b * x1[ch]) / det, 0.0f, 255.0f);
        e1[ch] = std::clamp((a * x1[ch] - b * x0[ch]) / det, 0.0f, 255.0f);
    }
    return true;
}

struct BitWriter {
    uint8_t* out;
    int position = 0;
    void Write(uint32_t value, int bits) {
        for (int i = 0; i < bits; ++i, ++position) {
            if (value & (1u << i)) out[position >> 3] |= static_cast<uint8_t>(1u << (position & 7));
        }
    }
};

} // namespace

void EncodeBlockMode6(const uint8_t* rgba, uint8_t* out) {
    // Principal axis of the block's colours (power iteration on the covariance)
    float mean[4] = {};
    for (int t = 0; t < 16; ++t)
        for (int c = 0; c < 4; ++c) mean[c] += rgba[t * 4 + c] / 16.0f;
    float cov[4][4] = {};
    for (int t = 0; t < 16; ++t) {
        float d[4];
        for (int c = 0; c < 4; ++c) d[c] = rgba[t * 4 + c] - mean[c];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) cov[i][j] += d[i] * d[j];
    }
    float axis[4] = { 1, 1, 1, 1 };
    for (int iteration = 0; iteration < 8; ++iteration) {
        float next[4] = {};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) next[i] += cov[i][j] * axis[j];
        const float length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2] + next[3] * next[3]);
        if (length < 1e-6f) break; // flat block: any axis works
        for (int i = 0; i < 4; ++i) axis[i] = next[i] / length;
    }

    // Endpoints: the extremes of the colours along that axis
    float tMin = 1e30f, tMax = -1e30f;
    for (int t = 0; t < 16; ++t) {
        float s = 0;
        for (int c = 0; c < 4; ++c) s += (rgba[t * 4 + c] - mean[c]) * axis[c];
        tMin = std::min(tMin, s);
        tMax = std::max(tMax, s);
    }
    float f0[4], f1[4];
    for (int c = 0; c < 4; ++c) {
        f0[c] = std::clamp(mean[c] + axis[c] * tMin, 0.0f, 255.0f);
        f1[c] = std::clamp(mean[c] + axis[c] * tMax, 0.0f, 255.0f);
    }

    int e0[4], e1[4];
    QuantizeEndpoint(f0, e0);
    QuantizeEndpoint(f1, e1);
    uint8_t indices[16];
    int error = AssignIndices(rgba, e0, e1, indices);

    // Refine: refit the endpoints to the chosen indices, keep whichever is better
    for (int pass = 0; pass < 2 && error > 0; ++pass) {
        float r0[4], r1[4];
        if (!FitEndpoints(rgba, indices, r0, r1)) break;
        int q0[4], q1[4];
        QuantizeEndpoint(r0, q0);
        QuantizeEndpoint(r1, q1);
        uint8_t refined[16];
        const int refinedError = AssignIndices(rgba, q0, q1, refined);
        if (refinedError >= error) break;
        error = refinedError;
        std::memcpy(e0, q0, sizeof(e0));
        std::memcpy(e1, q1, sizeof(e1));
        std::memcpy(indices, refined, sizeof(indices));
    }

    // The first texel's index is stored with 3 bits, so it must be < 8: swap the ends if not
    if (indices[0] >= 8) {
        std::swap(e0, e1);
        for (uint8_t& index : indices) index = static_cast<uint8_t>(15 - index);
    }

    std::memset(out, 0, 16);
    BitWriter writer{ out };
    writer.Write(1u << 6, 7);                 // mode 6: six 0 bits, then a 1
    for (int c = 0; c < 4; ++c) {             // R0 R1 G0 G1 B0 B1 A0 A1, 7 bits each
        writer.Write(static_cast<uint32_t>(e0[c] >> 1), 7);
        writer.Write(static_cast<uint32_t>(e1[c] >> 1), 7);
    }
    writer.Write(static_cast<uint32_t>(e0[0] & 1), 1); // p-bits (same for every channel)
    writer.Write(static_cast<uint32_t>(e1[0] & 1), 1);
    writer.Write(indices[0], 3);
    for (int t = 1; t < 16; ++t) writer.Write(indices[t], 4);
}

} // namespace Bc7
