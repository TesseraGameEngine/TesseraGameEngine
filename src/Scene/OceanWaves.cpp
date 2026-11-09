#include "OceanWaves.h"
#include "Scene/Scene.h"
#include "Scene/ParticleCollision.h"
#include <algorithm>
#include <cmath>

namespace OceanWaves {

namespace {

float g_time = 0.0f;

constexpr int kSwellWaves = 8;

// Must match OceanWave() in shaders/ocean_common.glsl (swell waves only)
struct Wave {
    glm::vec2 dir;
    float k, amplitude, steepness, omega, phase;
};

Wave MakeWave(const Node& ocean, int i) {
    static const float kSpread[kSwellWaves] = { 0.0f, 0.45f, -0.38f, 0.9f, -0.75f, 0.2f, -1.15f, 1.35f };
    const float angle = glm::radians(ocean.waveDirection);
    const glm::vec2 wind = glm::normalize(glm::vec2(std::cos(angle), std::sin(angle)) + glm::vec2(1e-5f, 0.0f));
    const float c = std::cos(kSpread[i]);
    const float s = std::sin(kSpread[i]);
    Wave w;
    w.dir = glm::vec2(c * wind.x - s * wind.y, s * wind.x + c * wind.y);
    const float ratio = 0.62f;
    const float ratioSum = 2.5679f;
    const float wavelength = std::max(ocean.waveLength, 0.5f) * std::pow(ratio, static_cast<float>(i));
    w.k = 6.2831853f / wavelength;
    w.amplitude = std::max(ocean.waveHeight, 0.0f) * 0.5f * std::pow(ratio, static_cast<float>(i)) / ratioSum;
    w.steepness = glm::clamp(ocean.waveChoppiness, 0.0f, 1.0f) / std::max(w.k * w.amplitude * kSwellWaves, 1e-4f);
    w.omega = std::sqrt(9.81f * w.k) * std::max(ocean.waveSpeed, 0.0f);
    w.phase = static_cast<float>(i) * 1.7f;
    return w;
}

// Offset of the point resting at p, its velocity, and the surface normal there
void Evaluate(const Node& ocean, const glm::vec2& p, float time, glm::vec3& offset, glm::vec3& velocity, glm::vec3& normal) {
    offset = glm::vec3(0.0f);
    velocity = glm::vec3(0.0f);
    normal = glm::vec3(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < kSwellWaves; ++i) {
        const Wave w = MakeWave(ocean, i);
        const float theta = w.k * glm::dot(w.dir, p) - w.omega * time + w.phase;
        const float s = std::sin(theta);
        const float c = std::cos(theta);
        offset.x += w.steepness * w.amplitude * w.dir.x * c;
        offset.z += w.steepness * w.amplitude * w.dir.y * c;
        offset.y += w.amplitude * s;
        // d/dt of the offset (theta falls at omega per second)
        velocity.x += w.steepness * w.amplitude * w.dir.x * w.omega * s;
        velocity.z += w.steepness * w.amplitude * w.dir.y * w.omega * s;
        velocity.y -= w.amplitude * w.omega * c;
        const float wa = w.k * w.amplitude;
        normal.x -= w.dir.x * wa * c;
        normal.z -= w.dir.y * wa * c;
        normal.y -= w.steepness * wa * s;
    }
    normal = glm::normalize(normal);
}

} // namespace

void SetTime(float seconds) { g_time = seconds; }
float Time() { return g_time; }

SurfaceSample SampleSurface(const Node& ocean, const Scene& scene, float x, float z, float time) {
    const float level = ocean.GetWorldPosition(scene).y;
    // The waves also move water sideways, so find the resting point that ends up above (x, z)
    glm::vec2 rest(x, z);
    glm::vec3 offset, velocity, normal;
    for (int i = 0; i < 4; ++i) {
        Evaluate(ocean, rest, time, offset, velocity, normal);
        rest = glm::vec2(x, z) - glm::vec2(offset.x, offset.z);
    }
    Evaluate(ocean, rest, time, offset, velocity, normal);
    SurfaceSample sample;
    sample.height = level + offset.y;
    sample.normal = normal;
    sample.velocity = velocity;
    return sample;
}

std::shared_ptr<Node> FindOceanAt(const Scene& scene, float x, float z) {
    for (const auto& node : scene.GetNodes()) {
        if (!node || !node->isOcean || node->nodeType != NodeType::Mesh) continue;
        const glm::vec3 center = node->GetWorldPosition(scene);
        const glm::vec2 half = glm::max(node->oceanSize, glm::vec2(1.0f)) * 0.5f;
        if (std::abs(x - center.x) <= half.x && std::abs(z - center.z) <= half.y) return node;
    }
    return nullptr;
}

float Spray::Random() {
    m_seed = m_seed * 1664525u + 1013904223u;
    return static_cast<float>(m_seed >> 8) / 16777216.0f;
}

void Spray::Clear() {
    m_particles.clear();
    m_sprites.clear();
    m_watches.clear();
}

void Spray::Update(const Scene& scene, float dt, const glm::vec3& focus, float range) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float time = Time();
    constexpr size_t kMaxParticles = 4000;

    m_bursts.clear();
    // The oceans, found once (FindOceanAt walks every node: too slow per object in big scenes)
    struct OceanArea { std::shared_ptr<Node> node; glm::vec2 center; glm::vec2 half; };
    std::vector<OceanArea> oceans;
    for (const auto& node : scene.GetNodes()) {
        if (!node || !node->isOcean || node->nodeType != NodeType::Mesh) continue;
        const glm::vec3 c = node->GetWorldPosition(scene);
        oceans.push_back({ node, glm::vec2(c.x, c.z), glm::max(node->oceanSize, glm::vec2(1.0f)) * 0.5f });
    }
    auto oceanAt = [&](float x, float z) -> std::shared_ptr<Node> {
        for (const auto& o : oceans) {
            if (std::abs(x - o.center.x) <= o.half.x && std::abs(z - o.center.y) <= o.half.y) return o.node;
        }
        return nullptr;
    };
    if (oceans.empty() && m_particles.empty()) { m_sprites.clear(); return; }
    // Objects that could throw spray (fixed, opaque, rock / post / pier sized)
    m_candidateAge += dt;
    if (m_candidateScene != &scene || m_candidateNodeCount != scene.GetNodes().size() || m_candidateAge > 1.0f) {
        m_candidates.clear();
        for (const auto& node : scene.GetNodes()) {
            if (!node || node->nodeType != NodeType::Mesh || node->isOcean || node->IsUINode()) continue;
            if (node->rigidBodyType == RigidBodyType::Dynamic || node->color.a < 0.99f) continue;
            const glm::mat4 world = node->GetWorldTransformMatrix(scene);
            const glm::vec3 localMin = node->type == ShapeType::Custom ? node->boundsMin : glm::vec3(-0.5f);
            const glm::vec3 localMax = node->type == ShapeType::Custom ? node->boundsMax : glm::vec3(0.5f);
            glm::vec3 lo(1e30f), hi(-1e30f);
            for (int c = 0; c < 8; ++c) {
                const glm::vec3 corner((c & 1) ? localMax.x : localMin.x, (c & 2) ? localMax.y : localMin.y, (c & 4) ? localMax.z : localMin.z);
                const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            const glm::vec2 halfSize = glm::vec2(hi.x - lo.x, hi.z - lo.z) * 0.5f;
            // Rocks, posts and piers; not whole beaches or seabeds
            if (halfSize.x > 10.0f || halfSize.y > 10.0f || halfSize.x < 0.05f || halfSize.y < 0.05f) continue;
            m_candidates.push_back({ node->id, lo, hi });
        }
        m_candidateScene = &scene;
        m_candidateNodeCount = scene.GetNodes().size();
        m_candidateAge = 0.0f;
    }
    // Watch the waterline of the ones standing in the water near the camera
    for (const Candidate& candidate : m_candidates) {
        if (oceans.empty()) break;
        const glm::vec3 lo = candidate.lo, hi = candidate.hi;
        const glm::vec3 center = (lo + hi) * 0.5f;
        const glm::vec2 halfSize = glm::vec2(hi.x - lo.x, hi.z - lo.z) * 0.5f;
        if (glm::length(glm::vec2(center.x - focus.x, center.z - focus.z)) > range + glm::length(halfSize)) continue;
        auto ocean = oceanAt(center.x, center.z);
        if (!ocean || ocean->waveHeight < 0.05f) continue;
        const float level = ocean->GetWorldPosition(scene).y;
        const float amplitude = ocean->waveHeight * 0.5f;
        if (lo.y > level + amplitude || hi.y < level - amplitude) continue;

        const float perimeter = 2.0f * 3.14159f * std::sqrt((halfSize.x * halfSize.x + halfSize.y * halfSize.y) * 0.5f);
        const int points = std::clamp(static_cast<int>(perimeter / 1.5f), 4, 24);
        for (int i = 0; i < points; ++i) {
            const float angle = 6.2831853f * (i + 0.5f) / points;
            const glm::vec2 outward(std::cos(angle), std::sin(angle));
            const glm::vec2 xz = glm::vec2(center.x, center.z) + outward * halfSize * 0.85f;
            const SurfaceSample surface = SampleSurface(*ocean, scene, xz.x, xz.y, time);
            const float h = surface.height - level;
            Watch& watch = m_watches[(static_cast<uint64_t>(candidate.id) << 8) | static_cast<uint64_t>(i)];
            watch.cooldown -= dt;
            if (watch.valid) {
                // A crest has just passed this point: throw spray, more for bigger crests
                if (watch.rising && h < watch.lastHeight && h > amplitude * 0.25f && watch.cooldown <= 0.0f) {
                    const float strength = std::clamp(h / std::max(amplitude, 0.01f), 0.0f, 1.5f);
                    m_bursts.push_back({ glm::vec3(xz.x, surface.height, xz.y), strength });
                    // The splash itself: an animated burst of white water
                    if (m_particles.size() < kMaxParticles) {
                        Particle splash;
                        splash.position = glm::vec3(xz.x, surface.height + 0.2f, xz.y);
                        splash.velocity = glm::vec3(outward.x, 1.0f, outward.y) * (0.5f + strength) + surface.velocity * 0.3f;
                        splash.life = 0.9f + 0.4f * Random();
                        splash.size = (0.8f + 1.4f * strength) * std::min(1.0f, perimeter / 3.0f + 0.4f);
                        splash.splash = true;
                        splash.seed = Random() * 100.0f;
                        m_particles.push_back(splash);
                    }
                    const int count = std::clamp(static_cast<int>((6.0f + 30.0f * strength) * std::min(1.0f, perimeter / 4.0f)), 4, 40);
                    for (int n = 0; n < count && m_particles.size() < kMaxParticles; ++n) {
                        Particle p;
                        p.position = glm::vec3(xz.x + (Random() - 0.5f) * 0.6f, surface.height, xz.y + (Random() - 0.5f) * 0.6f);
                        const float up = (1.5f + 3.5f * strength) * (0.6f + 0.4f * Random());
                        const float side = 0.5f + 1.5f * Random();
                        p.velocity = glm::vec3(outward.x * side, up, outward.y * side) + surface.velocity * 0.5f +
                                     glm::vec3(Random() - 0.5f, 0.0f, Random() - 0.5f);
                        p.life = 0.7f + 0.8f * Random();
                        p.size = (0.15f + 0.3f * Random()) * (0.7f + strength);
                        m_particles.push_back(p);
                    }
                    watch.cooldown = 0.6f;
                }
            }
            watch.rising = h > watch.lastHeight;
            watch.lastHeight = h;
            watch.valid = true;
        }
    }

    // What the droplets hit (rocks, posts, boats, the land: ParticleColliders, as particle
    // emitters use), around where they are
    ParticleColliders colliders;
    if (!m_particles.empty()) {
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const Particle& p : m_particles) {
            lo = glm::min(lo, p.position - glm::abs(p.velocity) * dt - 1.0f);
            hi = glm::max(hi, p.position + glm::abs(p.velocity) * dt + 1.0f);
        }
        ParticleColliders::Settings settings;
        settings.water = false; // the sea is handled below: droplets come out of it
        colliders.Build(scene, lo, hi, settings);
    }

    // Droplets fly up, slow down in the air and fall back; they bounce off what they hit and
    // sink when they fall back into the sea
    m_sprites.clear();
    size_t alive = 0;
    for (Particle& p : m_particles) {
        p.age += dt;
        p.velocity.y -= (p.splash ? 2.0f : 9.81f) * dt; // the splash cloud hangs in the air
        p.velocity *= std::exp(-(p.splash ? 2.5f : 0.8f) * dt);
        const glm::vec3 previous = p.position;
        p.position += p.velocity * dt;
        if (!p.splash) {
            ParticleColliders::Hit hit;
            if (colliders.Sweep(previous, p.position, 0.0f, hit)) {
                // Out to the surface, bouncing a little and sliding along it
                p.position = hit.position;
                const float into = glm::dot(p.velocity, hit.normal);
                if (into < 0.0f) p.velocity = (p.velocity - into * hit.normal) * 0.6f - into * hit.normal * 0.2f;
            }
            if (p.velocity.y < 0.0f && p.age > 0.15f) {
                if (auto ocean = oceanAt(p.position.x, p.position.z)) {
                    if (p.position.y < SampleSurface(*ocean, scene, p.position.x, p.position.z, time).height) continue;
                }
            }
        }
        if (p.age >= p.life) continue;
        m_particles[alive++] = p;
        const float t = p.age / p.life;
        if (p.splash) {
            m_sprites.push_back({ p.position, p.size * (1.0f + 0.8f * t), glm::vec4(0.95f, 0.97f, 1.0f, 0.9f), t, p.seed });
            continue;
        }
        const float fadeIn = std::min(p.age / 0.08f, 1.0f);
        const float alpha = 0.75f * fadeIn * std::pow(1.0f - t, 1.5f);
        m_sprites.push_back({ p.position, p.size * (1.0f + 1.5f * t), glm::vec4(0.95f, 0.97f, 1.0f, alpha) });
    }
    m_particles.resize(alive);
}

} // namespace OceanWaves
