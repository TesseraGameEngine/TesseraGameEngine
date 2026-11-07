#include "FoliageMeshes.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FoliageMeshes {

namespace {

struct Builder {
    MeshData mesh;
    uint32_t seed{1u};

    float Random() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    }

    uint32_t Add(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, const glm::vec4& color) {
        Vertex v{};
        v.pos = p;
        v.normal = glm::normalize(n);
        v.texCoord = uv;
        v.color = color;
        mesh.vertices.push_back(v);
        return static_cast<uint32_t>(mesh.vertices.size() - 1);
    }

    void Tri(uint32_t a, uint32_t b, uint32_t c) {
        mesh.indices.insert(mesh.indices.end(), { a, b, c });
    }

    // A tapered blade from base to tip, bending toward lean; normals lean up so it lights softly
    void Blade(const glm::vec3& base, const glm::vec3& lean, float height, float width, const glm::vec4& baseColor, const glm::vec4& tipColor) {
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 side = glm::normalize(glm::cross(up, lean + glm::vec3(1e-4f, 0.0f, 0.0f)));
        const int segments = 3;
        uint32_t prevL = 0, prevR = 0;
        for (int s = 0; s <= segments; ++s) {
            const float t = static_cast<float>(s) / segments;
            const glm::vec3 center = base + up * height * t + lean * height * t * t;
            const float w = width * (1.0f - t * 0.9f);
            const glm::vec3 normal = glm::normalize(glm::normalize(glm::cross(side, glm::normalize(up + lean * 2.0f * t))) + up * 0.6f);
            const glm::vec4 color = glm::mix(baseColor, tipColor, t);
            const uint32_t l = Add(center - side * w * 0.5f, normal, glm::vec2(0.0f, 1.0f - t), color);
            const uint32_t r = Add(center + side * w * 0.5f, normal, glm::vec2(1.0f, 1.0f - t), color);
            if (s > 0) {
                Tri(prevL, prevR, r);
                Tri(prevL, r, l);
            }
            prevL = l;
            prevR = r;
        }
    }

    // A lumpy ball (icosphere-like UV sphere with noise), for canopies, bushes and rocks
    void Blob(const glm::vec3& center, const glm::vec3& radius, float lumpiness, const glm::vec4& color, float shade, int rings = 7, int segments = 10) {
        const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
        std::vector<float> bumps(static_cast<size_t>((rings + 1) * segments));
        for (float& b : bumps) b = 1.0f + (Random() - 0.5f) * 2.0f * lumpiness;
        for (int r = 0; r <= rings; ++r) {
            const float phi = glm::pi<float>() * r / rings;
            for (int s = 0; s < segments; ++s) {
                const float theta = glm::two_pi<float>() * s / segments;
                glm::vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
                const float bump = (r == 0 || r == rings) ? 1.0f : bumps[static_cast<size_t>(r * segments + s)];
                const glm::vec3 p = center + dir * radius * bump;
                // Darker underneath and toward the inside
                const float light = glm::mix(1.0f - shade, 1.0f, dir.y * 0.5f + 0.5f) * (0.9f + 0.2f * Random());
                Add(p, dir, glm::vec2(static_cast<float>(s) / segments, static_cast<float>(r) / rings), glm::vec4(glm::vec3(color) * light, 1.0f));
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segments; ++s) {
                const uint32_t a = first + r * segments + s, b = first + r * segments + (s + 1) % segments;
                const uint32_t c = first + (r + 1) * segments + s, d = first + (r + 1) * segments + (s + 1) % segments;
                Tri(a, b, c);
                Tri(b, d, c);
            }
        }
    }

    // A tapered cylinder (trunk) or a cone (topRadius 0)
    void Cylinder(const glm::vec3& base, float height, float bottomRadius, float topRadius, const glm::vec4& color, int segments = 8) {
        const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
        for (int s = 0; s < segments; ++s) {
            const float theta = glm::two_pi<float>() * s / segments;
            const glm::vec3 dir(std::cos(theta), 0.0f, std::sin(theta));
            const glm::vec3 normal = glm::normalize(dir * height + glm::vec3(0.0f, bottomRadius - topRadius, 0.0f));
            Add(base + dir * bottomRadius, normal, glm::vec2(static_cast<float>(s) / segments, 1.0f), color * glm::vec4(glm::vec3(0.8f), 1.0f));
            Add(base + glm::vec3(0.0f, height, 0.0f) + dir * topRadius, normal, glm::vec2(static_cast<float>(s) / segments, 0.0f), color);
        }
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = first + s * 2, b = first + s * 2 + 1;
            const uint32_t c = first + ((s + 1) % segments) * 2, d = first + ((s + 1) % segments) * 2 + 1;
            Tri(a, b, c);
            Tri(c, b, d);
        }
    }
};

} // namespace

std::string MeshName(int kind) {
    static const char* kNames[Count] = { "__foliage_grass", "__foliage_flowers", "__foliage_bush", "__foliage_tree", "__foliage_pine", "__foliage_rock" };
    return kNames[std::clamp(kind, 0, Count - 1)];
}

MeshData Build(int kind) {
    Builder b;
    b.seed = 97u + static_cast<uint32_t>(kind) * 7919u;
    switch (kind) {
    case Grass:
    case Flowers: {
        // A tuft of blades fanning out, about 0.5 m tall
        const int blades = kind == Grass ? 9 : 6;
        for (int i = 0; i < blades; ++i) {
            const float angle = b.Random() * glm::two_pi<float>();
            const glm::vec3 base(std::cos(angle) * 0.08f * b.Random(), 0.0f, std::sin(angle) * 0.08f * b.Random());
            const glm::vec3 lean = glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (0.15f + 0.25f * b.Random());
            const float shade = 0.75f + 0.25f * b.Random();
            b.Blade(base, lean, 0.35f + 0.25f * b.Random(), 0.05f + 0.02f * b.Random(),
                    glm::vec4(glm::vec3(0.45f * shade), 1.0f), glm::vec4(glm::vec3(1.05f * shade), 1.0f));
        }
        if (kind == Flowers) {
            // Small blooms on top of some stems: white, yellow and violet
            const glm::vec4 colors[3] = { glm::vec4(0.95f, 0.95f, 0.9f, 1.0f), glm::vec4(1.0f, 0.85f, 0.2f, 1.0f), glm::vec4(0.7f, 0.45f, 0.95f, 1.0f) };
            for (int i = 0; i < 4; ++i) {
                const float angle = b.Random() * glm::two_pi<float>();
                const glm::vec3 top(std::cos(angle) * 0.12f, 0.45f + 0.15f * b.Random(), std::sin(angle) * 0.12f);
                b.Cylinder(glm::vec3(top.x * 0.5f, 0.0f, top.z * 0.5f), top.y, 0.006f, 0.004f, glm::vec4(0.3f, 0.5f, 0.2f, 1.0f), 4);
                b.Blob(top, glm::vec3(0.045f, 0.025f, 0.045f), 0.2f, colors[i % 3], 0.2f, 4, 6);
            }
        }
        break;
    }
    case Bush: {
        // A few overlapping lumpy balls, about 1 m across
        b.Blob(glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.55f, 0.45f, 0.55f), 0.18f, glm::vec4(1.0f), 0.5f);
        b.Blob(glm::vec3(0.3f, 0.35f, 0.15f), glm::vec3(0.35f, 0.3f, 0.35f), 0.2f, glm::vec4(0.9f, 1.0f, 0.9f, 1.0f), 0.5f);
        b.Blob(glm::vec3(-0.25f, 0.3f, -0.2f), glm::vec3(0.35f, 0.3f, 0.35f), 0.2f, glm::vec4(1.0f), 0.5f);
        break;
    }
    case Tree: {
        // A trunk and a crown of lumpy balls, about 6 m tall
        const glm::vec4 bark(0.38f, 0.28f, 0.2f, 1.0f);
        const glm::vec4 leaves(0.32f, 0.52f, 0.22f, 1.0f);
        b.Cylinder(glm::vec3(0.0f), 3.2f, 0.22f, 0.14f, bark, 10);
        b.Cylinder(glm::vec3(0.0f, 2.2f, 0.0f), 1.4f, 0.09f, 0.04f, bark, 6); // a branch upward
        b.Blob(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(1.9f, 1.6f, 1.9f), 0.15f, leaves, 0.55f, 9, 14);
        b.Blob(glm::vec3(0.9f, 3.6f, 0.4f), glm::vec3(1.2f, 1.0f, 1.2f), 0.18f, leaves * glm::vec4(1.05f, 1.05f, 0.95f, 1.0f), 0.55f);
        b.Blob(glm::vec3(-0.8f, 3.5f, -0.5f), glm::vec3(1.2f, 1.0f, 1.2f), 0.18f, leaves * glm::vec4(0.95f, 1.0f, 0.95f, 1.0f), 0.55f);
        b.Blob(glm::vec3(0.2f, 4.9f, -0.3f), glm::vec3(1.1f, 0.9f, 1.1f), 0.15f, leaves * glm::vec4(1.1f, 1.1f, 1.0f, 1.0f), 0.5f);
        break;
    }
    case Pine: {
        // A trunk and stacked cones, about 8 m tall
        const glm::vec4 bark(0.35f, 0.25f, 0.18f, 1.0f);
        const glm::vec4 needles(0.16f, 0.32f, 0.2f, 1.0f);
        b.Cylinder(glm::vec3(0.0f), 2.0f, 0.2f, 0.15f, bark, 8);
        const float tiers[4][3] = { { 1.4f, 3.0f, 1.9f }, { 2.9f, 2.6f, 1.5f }, { 4.3f, 2.3f, 1.1f }, { 5.6f, 2.4f, 0.75f } };
        for (const auto& tier : tiers) {
            b.Cylinder(glm::vec3(0.0f, tier[0], 0.0f), tier[1], tier[2], 0.0f, needles * glm::vec4(glm::vec3(0.9f + 0.2f * b.Random()), 1.0f), 12);
        }
        break;
    }
    case Rock:
    default: {
        // A flattened lumpy boulder, about 1 m across, sunk into the ground a little
        b.Blob(glm::vec3(0.0f, 0.25f, 0.0f), glm::vec3(0.55f, 0.4f, 0.45f), 0.25f, glm::vec4(1.0f), 0.35f, 6, 9);
        break;
    }
    }
    return b.mesh;
}

} // namespace FoliageMeshes
