#include "ParticleNode.h"
#include "Scene/Scene.h"
#include "Scene/ParticleCollision.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

ParticleNode::ParticleNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::ParticleEmitter;
    parentId = pId;
    name = nodeName.empty() ? "Particle Emitter" : nodeName;
    color = glm::vec4(1.0f, 0.65f, 0.15f, 1.0f);
    m_particles.resize(maxParticles);
}

std::shared_ptr<Node> ParticleNode::Clone() const {
    return std::make_shared<ParticleNode>(*this);
}

float ParticleNode::NextRandom01() {
    m_rngState = m_rngState * 1664525u + 1013904223u;
    return static_cast<float>(m_rngState & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

glm::vec3 ParticleNode::SampleCurlNoise(const glm::vec3& pos) const {
    glm::vec3 p = pos * curlNoiseFrequency;
    float vx = std::sin(p.y * 2.1f + p.z * 1.3f) - std::cos(p.z * 1.7f + p.x * 0.9f);
    float vy = std::sin(p.z * 2.3f + p.x * 1.1f) - std::cos(p.x * 1.9f + p.y * 0.7f);
    float vz = std::sin(p.x * 1.7f + p.y * 2.5f) - std::cos(p.y * 1.5f + p.z * 1.2f);
    return glm::vec3(vx, vy, vz) * curlNoiseStrength;
}

void ParticleNode::ResizePool() {
    const int stride = trailMode == ParticleTrailMode::PerParticle ? std::clamp(trailLength, 2, 64) : 0;
    if (m_particles.size() != maxParticles) {
        m_particles.resize(maxParticles);
        m_trailStride = -1; // trails restart with the pool
    }
    if (stride != m_trailStride) {
        m_trailStride = stride;
        m_trailPoints.assign(static_cast<size_t>(maxParticles) * static_cast<size_t>(stride), glm::vec4(0.0f));
        for (auto& p : m_particles) {
            p.trailCount = 0;
            p.trailHead = 0;
        }
    }
}

ParticleInstance* ParticleNode::FreeSlot() {
    // A dead particle whose trail has faded
    for (auto& p : m_particles) {
        if (!p.alive && p.trailCount == 0) return &p;
    }
    return nullptr;
}

ParticleInstance* ParticleNode::SpawnSingleParticle(const glm::vec3& origin) {
    ResizePool();
    ParticleInstance* slot = FreeSlot();
    if (!slot) return nullptr;
    ParticleInstance& p = *slot;
    p = ParticleInstance{};
    p.alive = true;
    p.age = 0.0f;
    float rLife = NextRandom01();
    p.lifetime = std::max(0.05f, glm::mix(lifetimeMin, lifetimeMax, rLife));
    p.color = startColor;
    p.size = startSize;
    p.seed = NextRandom01();
    p.rotation = glm::radians(glm::mix(startRotationMin, startRotationMax, NextRandom01()));
    p.angularVelocity = glm::radians(glm::mix(angularVelocityMin, angularVelocityMax, NextRandom01()));
    p.frameOffset = flipbookRandomStart ? std::floor(NextRandom01() * static_cast<float>(FlipbookFrameCount())) : 0.0f;
    p.spawnIndex = m_nextSpawnIndex++;

    float rx = NextRandom01() * 2.0f - 1.0f;
    float ry = NextRandom01() * 2.0f - 1.0f;
    float rz = NextRandom01() * 2.0f - 1.0f;
    glm::vec3 randVec(rx, ry, rz);
    if (glm::length(randVec) > 0.0001f) {
        randVec = glm::normalize(randVec);
    } else {
        randVec = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    switch (emitterShape) {
        case ParticleEmitterShape::Point:
            p.position = origin;
            p.velocity = randVec * startSpeed;
            break;
        case ParticleEmitterShape::Sphere:
            p.position = origin + randVec * (emitterRadius * NextRandom01());
            p.velocity = randVec * startSpeed;
            break;
        case ParticleEmitterShape::Box:
            p.position = origin + glm::vec3(rx, ry, rz) * (emitterBoxExtents * 0.5f);
            p.velocity = glm::vec3(0.0f, 1.0f, 0.0f) * startSpeed;
            break;
        case ParticleEmitterShape::Cone:
        default: {
            float angleRad = glm::radians(coneAngle) * NextRandom01();
            float phi = NextRandom01() * 6.2831853f;
            glm::vec3 dir(
                std::sin(angleRad) * std::cos(phi),
                std::cos(angleRad),
                std::sin(angleRad) * std::sin(phi)
            );
            p.position = origin + glm::vec3(dir.x, 0.0f, dir.z) * (emitterRadius * NextRandom01());
            p.velocity = glm::normalize(dir) * startSpeed;
            break;
        }
    }
    return &p;
}

void ParticleNode::SpawnHitParticles(const glm::vec3& position, const glm::vec3& normal, const ParticleInstance& parent) {
    for (int i = 0; i < hitSpawnCount; ++i) {
        ParticleInstance* slot = FreeSlot();
        if (!slot) return;
        ParticleInstance& p = *slot;
        p = ParticleInstance{};
        p.alive = true;
        p.child = true;
        p.position = position + normal * 0.01f;
        // Up and out from the surface, in a wide cone around its normal
        glm::vec3 r(NextRandom01() * 2.0f - 1.0f, NextRandom01() * 2.0f - 1.0f, NextRandom01() * 2.0f - 1.0f);
        glm::vec3 dir = normal + r * 0.85f;
        if (glm::dot(dir, normal) < 0.1f) dir += normal * 0.5f;
        dir = glm::normalize(dir);
        p.velocity = dir * hitSpawnSpeed * (0.5f + NextRandom01());
        p.lifetime = std::max(0.05f, parent.lifetime * 0.35f * (0.6f + 0.8f * NextRandom01()));
        p.sizeScale = std::max(hitSpawnSize, 0.01f);
        p.size = startSize * p.sizeScale;
        p.color = parent.color;
        p.seed = NextRandom01();
        p.rotation = parent.rotation;
        p.frameOffset = parent.frameOffset;
        p.spawnIndex = m_nextSpawnIndex++;
    }
}

void ParticleNode::EmitBurst(uint32_t count, const Scene& scene) {
    if (simulationMode == ParticleSimulationMode::GPU_Niagara) {
        m_gpuPendingSpawns += count;
        return;
    }
    glm::vec3 origin = worldSpace ? GetWorldPosition(scene) : glm::vec3(0.0f);
    for (uint32_t i = 0; i < count; ++i) {
        SpawnSingleParticle(origin);
    }
}

void ParticleNode::ResetParticles() {
    for (auto& p : m_particles) {
        p.alive = false;
        p.age = 0.0f;
        p.trailCount = 0;
    }
    m_spawnAccumulator = 0.0f;
    m_gpuPendingSpawns = 0;
    m_gpuPendingTime = 0.0f;
    m_gpuReset = true;
    m_gpuAlive = 0;
}

uint32_t ParticleNode::GetAliveParticleCount() const {
    if (simulationMode == ParticleSimulationMode::GPU_Niagara) return m_gpuAlive;
    uint32_t count = 0;
    for (const auto& p : m_particles) {
        if (p.alive) ++count;
    }
    return count;
}

void ParticleNode::SetGpuFeedback(uint32_t alive, uint32_t hits, const glm::vec3& lastHit) {
    m_gpuAlive = alive;
    if (hits > 0) {
        m_collisionHits += hits;
        m_totalCollisionHits += hits;
        m_lastHitPosition = lastHit;
    }
}

std::vector<GPUParticleData> ParticleNode::BuildGPUBufferData() const {
    std::vector<GPUParticleData> out;
    out.reserve(m_particles.size());
    for (const auto& p : m_particles) {
        GPUParticleData g{};
        g.positionAndSize = glm::vec4(p.position, p.size);
        g.velocityAndAge = glm::vec4(p.velocity, p.age);
        g.color = p.color;
        g.params = glm::vec4(p.lifetime, p.alive ? 1.0f : 0.0f, 0.0f, 0.0f);
        out.push_back(g);
    }
    return out;
}

int ParticleNode::FlipbookFrameCount() const {
    const int cells = std::max(flipbookColumns, 1) * std::max(flipbookRows, 1);
    return flipbookFrames > 0 ? std::min(flipbookFrames, cells) : cells;
}

float ParticleNode::FlipbookFrame(const ParticleInstance& p) const {
    const float frames = static_cast<float>(FlipbookFrameCount());
    if (frames <= 1.0f) return 0.0f;
    float f;
    if (flipbookMode == ParticleFlipbookMode::FixedRate) {
        f = p.age * flipbookFps;
    } else {
        const float t = std::clamp(p.age / std::max(p.lifetime, 1e-4f), 0.0f, 1.0f) * std::max(flipbookCycles, 0.01f);
        // Played once, the last frame holds at the end (no wrap back to the first)
        f = (flipbookCycles <= 1.0f ? std::min(t, 0.9999f) : t - std::floor(t)) * frames;
    }
    f += p.frameOffset;
    return f - std::floor(f / frames) * frames;
}

void ParticleNode::RecordTrail(ParticleInstance& p, size_t index, float dt) {
    if (m_trailStride <= 0) return;
    glm::vec4* points = &m_trailPoints[index * static_cast<size_t>(m_trailStride)];
    const float life = TrailLifetime();
    // Points older than the trail's life drop off its tail
    while (p.trailCount > 0) {
        const int tail = (static_cast<int>(p.trailHead) - static_cast<int>(p.trailCount) + 1 + m_trailStride) % m_trailStride;
        if (m_time - points[tail].w <= life) break;
        --p.trailCount;
    }
    if (!p.alive) return;
    p.trailTimer += dt;
    if (p.trailCount == 0 || p.trailTimer >= trailInterval) {
        p.trailTimer = 0.0f;
        p.trailHead = static_cast<uint16_t>((p.trailHead + 1) % m_trailStride);
        points[p.trailHead] = glm::vec4(p.position, m_time);
        p.trailCount = static_cast<uint16_t>(std::min<int>(p.trailCount + 1, m_trailStride));
    }
}

void ParticleNode::Update(float dt, const Scene& scene) {
    if (dt <= 0.0f) return;
    if (simulationMode == ParticleSimulationMode::GPU_Niagara) {
        // The GPU steps the particles (VulkanParticleSim); here only the spawns are counted
        m_gpuPendingTime += dt;
        if (isEmitting && spawnRate > 0.0f) {
            m_spawnAccumulator += spawnRate * dt;
            const float whole = std::floor(m_spawnAccumulator);
            m_gpuPendingSpawns += static_cast<uint32_t>(whole);
            m_spawnAccumulator -= whole;
        }
        return;
    }

    ResizePool();
    m_time += dt;
    glm::vec3 origin = worldSpace ? GetWorldPosition(scene) : glm::vec3(0.0f);

    if (isEmitting && spawnRate > 0.0f) {
        m_spawnAccumulator += spawnRate * dt;
        while (m_spawnAccumulator >= 1.0f) {
            SpawnSingleParticle(origin);
            m_spawnAccumulator -= 1.0f;
        }
    }

    const glm::vec3 gravityVec(0.0f, -9.81f * gravityMultiplier, 0.0f);
    glm::vec3 attractCenter = origin + attractorOffset;
    glm::vec3 vAxis = glm::length(vortexAxis) > 0.001f ? glm::normalize(vortexAxis) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float dragFactor = std::exp(-std::max(drag, 0.0f) * dt);

    // What the particles can hit: the shapes around where they are (world space)
    ParticleColliders colliders;
    const bool colliding = collisionMode != ParticleCollisionMode::None;
    const glm::mat4 toWorld = worldSpace ? glm::mat4(1.0f) : GetWorldTransformMatrix(scene);
    const glm::mat4 toLocal = worldSpace ? glm::mat4(1.0f) : glm::inverse(toWorld);
    if (colliding) {
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const auto& p : m_particles) {
            if (!p.alive) continue;
            const glm::vec3 w = glm::vec3(toWorld * glm::vec4(p.position, 1.0f));
            const glm::vec3 reach = glm::abs(p.velocity) * dt * 2.0f + glm::vec3(p.size + 0.5f);
            lo = glm::min(lo, w - reach);
            hi = glm::max(hi, w + reach);
        }
        if (lo.x <= hi.x) {
            ParticleColliders::Settings settings;
            settings.excludeNodeId = id;
            settings.groundPlane = collisionMode == ParticleCollisionMode::GroundPlane;
            settings.groundHeight = collisionPlaneHeight;
            settings.solids = settings.terrain = collisionMode == ParticleCollisionMode::Scene;
            settings.water = collisionMode == ParticleCollisionMode::Scene && collideWithWater;
            colliders.Build(scene, lo, hi, settings);
        }
    }

    for (size_t index = 0; index < m_particles.size(); ++index) {
        ParticleInstance& p = m_particles[index];
        if (!p.alive) {
            if (p.trailCount > 0) RecordTrail(p, index, dt);
            continue;
        }

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            if (p.trailCount > 0) RecordTrail(p, index, dt);
            continue;
        }

        float normAge = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);

        // Apply forces
        glm::vec3 accel = gravityVec;

        if (enableVortexModule) {
            glm::vec3 rel = p.position - origin;
            glm::vec3 tangent = glm::cross(vAxis, rel);
            if (glm::length(tangent) > 0.0001f) {
                accel += glm::normalize(tangent) * vortexStrength;
            }
        }

        if (enableAttractorModule) {
            glm::vec3 toTarget = attractCenter - p.position;
            float dist = glm::length(toTarget);
            if (dist > 0.01f) {
                accel += (toTarget / dist) * attractorStrength;
            }
        }

        if (enableCurlNoiseModule) {
            accel += SampleCurlNoise(p.position);
        }

        p.velocity += accel * dt;
        p.velocity *= dragFactor;
        const glm::vec3 previous = p.position;
        p.position += p.velocity * dt;
        p.rotation += p.angularVelocity * dt;
        p.size = glm::mix(startSize, endSize, normAge) * p.sizeScale;
        p.color = glm::mix(startColor, endColor, normAge);

        if (colliding && !colliders.Empty()) {
            const glm::vec3 from = glm::vec3(toWorld * glm::vec4(previous, 1.0f));
            const glm::vec3 to = glm::vec3(toWorld * glm::vec4(p.position, 1.0f));
            ParticleColliders::Hit hit;
            if (colliders.Sweep(from, to, p.size * collisionRadiusScale, hit)) {
                ++m_collisionHits;
                ++m_totalCollisionHits;
                m_lastHitPosition = hit.position;
                if (!p.child && hitSpawnCount > 0) SpawnHitParticles(glm::vec3(toLocal * glm::vec4(hit.position, 1.0f)), glm::normalize(glm::mat3(toLocal) * hit.normal), p);
                if (hit.water || killOnCollision) {
                    p.alive = false;
                    p.position = glm::vec3(toLocal * glm::vec4(hit.position, 1.0f));
                    if (p.trailCount > 0) RecordTrail(p, index, dt);
                    continue;
                }
                // Bounce: the speed into the surface reverses (scaled by bounce), the speed
                // along it is slowed by friction
                glm::vec3 v = glm::mat3(toWorld) * p.velocity;
                const float into = glm::dot(v, hit.normal);
                if (into < 0.0f) {
                    const glm::vec3 along = v - into * hit.normal;
                    v = along * (1.0f - std::clamp(friction, 0.0f, 1.0f)) - into * hit.normal * std::max(bounce, 0.0f);
                }
                p.velocity = glm::mat3(toLocal) * v;
                p.position = glm::vec3(toLocal * glm::vec4(hit.position, 1.0f));
                p.age += lifetimeLossOnHit * p.lifetime;
            }
        }
        RecordTrail(p, index, dt);
    }
}

void ParticleNode::BuildQuads(const Scene& scene, const glm::vec3& cameraPos, bool sorted, std::vector<ParticleQuad>& out) const {
    const size_t first = out.size();
    const glm::mat4 toWorld = worldSpace ? glm::mat4(1.0f) : GetWorldTransformMatrix(scene);
    const glm::mat3 toWorldDir(toWorld);
    auto world = [&](const glm::vec3& p) { return glm::vec3(toWorld * glm::vec4(p, 1.0f)); };
    const float halfWidth = 0.5f * std::max(trailWidth, 0.0f);

    if (trailMode == ParticleTrailMode::Ribbon) {
        // One strip through the living particles, oldest to newest
        std::vector<const ParticleInstance*> order;
        for (const auto& p : m_particles) {
            if (p.alive) order.push_back(&p);
        }
        std::sort(order.begin(), order.end(), [](const ParticleInstance* x, const ParticleInstance* y) { return x->spawnIndex < y->spawnIndex; });
        const float segments = static_cast<float>(std::max<size_t>(order.size(), 2) - 1);
        for (size_t i = 0; i + 1 < order.size(); ++i) {
            const ParticleInstance& a = *order[i];
            const ParticleInstance& b = *order[i + 1];
            ParticleQuad q;
            q.a = glm::vec4(world(a.position), a.size * halfWidth);
            q.b = glm::vec4(world(b.position), b.size * halfWidth);
            q.colorA = a.color;
            q.colorB = b.color;
            q.params = glm::vec4(0.0f, a.seed, static_cast<float>(i) / segments * trailTextureTiling,
                                 static_cast<float>(i + 1) / segments * trailTextureTiling);
            out.push_back(q);
        }
    } else if (trailMode == ParticleTrailMode::PerParticle && m_trailStride > 0 && m_trailPoints.size() >= m_particles.size() * static_cast<size_t>(m_trailStride)) {
        const float life = TrailLifetime();
        for (size_t index = 0; index < m_particles.size(); ++index) {
            const ParticleInstance& p = m_particles[index];
            if (p.trailCount == 0) continue;
            const glm::vec4* points = &m_trailPoints[index * static_cast<size_t>(m_trailStride)];
            // From the particle (while it lives) back along the recorded points
            glm::vec3 head = p.alive ? p.position : glm::vec3(points[p.trailHead]);
            float headAge = 0.0f;
            const float width = p.size * halfWidth;
            for (int k = p.alive ? 0 : 1; k < p.trailCount; ++k) {
                const int slot = (static_cast<int>(p.trailHead) - k + m_trailStride) % m_trailStride;
                const glm::vec3 tail = glm::vec3(points[slot]);
                const float tailAge = std::clamp((m_time - points[slot].w) / life, 0.0f, 1.0f);
                if (glm::dot(tail - head, tail - head) > 1e-10f) {
                    ParticleQuad q;
                    q.a = glm::vec4(world(head), width * (trailTaper ? 1.0f - headAge : 1.0f));
                    q.b = glm::vec4(world(tail), width * (trailTaper ? 1.0f - tailAge : 1.0f));
                    q.colorA = glm::vec4(glm::vec3(p.color), p.color.a * (1.0f - headAge));
                    q.colorB = glm::vec4(glm::vec3(p.color), p.color.a * (1.0f - tailAge));
                    q.params = glm::vec4(0.0f, p.seed, headAge * trailTextureTiling, tailAge * trailTextureTiling);
                    out.push_back(q);
                }
                head = tail;
                headAge = tailAge;
            }
        }
    }

    if (trailMode == ParticleTrailMode::None || trailDrawParticles) {
        for (const auto& p : m_particles) {
            if (!p.alive) continue;
            ParticleQuad q;
            q.a = glm::vec4(world(p.position), std::max(0.005f, p.size) * 0.5f);
            q.b = glm::vec4(toWorldDir * p.velocity * velocityStretch, p.rotation);
            q.colorA = p.color;
            q.colorB = p.color;
            q.params = glm::vec4(FlipbookFrame(p), p.seed, -1.0f, 0.0f);
            out.push_back(q);
        }
    }

    if (sorted) {
        auto center = [](const ParticleQuad& q) { return q.params.z < -0.5f ? glm::vec3(q.a) : (glm::vec3(q.a) + glm::vec3(q.b)) * 0.5f; };
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), [&](const ParticleQuad& x, const ParticleQuad& y) {
            const glm::vec3 dx = center(x) - cameraPos;
            const glm::vec3 dy = center(y) - cameraPos;
            return glm::dot(dx, dx) > glm::dot(dy, dy);
        });
    }
}

void ParticleNode::ApplyPreset(ParticlePreset preset) {
    if (preset == ParticlePreset::Custom) return;
    // Every preset starts from the plain look and adds its own
    builtinSheet = ParticleSheet::None;
    flipbookColumns = flipbookRows = 1;
    flipbookFrames = 0;
    flipbookMode = ParticleFlipbookMode::OverLifetime;
    flipbookCycles = 1.0f;
    flipbookRandomStart = false;
    startRotationMin = startRotationMax = 0.0f;
    angularVelocityMin = angularVelocityMax = 0.0f;
    velocityStretch = 0.0f;
    drag = 0.0f;
    trailMode = ParticleTrailMode::None;
    collisionMode = ParticleCollisionMode::None;
    killOnCollision = false;
    hitSpawnCount = 0;
    lifetimeLossOnHit = 0.0f;
    softness = 0.4f;
    lit = true;
    glowIntensity = 2.5f;
    selfGlow = 0.0f;
    enableVortexModule = false;
    enableAttractorModule = false;
    enableCurlNoiseModule = false;
    isEmitting = true;
    looping = true;
    texturePath.clear();
    switch (preset) {
        case ParticlePreset::FireTorch:
            renderMode = ParticleRenderMode::BillboardAdditive;
            builtinSheet = ParticleSheet::Fire;
            flipbookColumns = flipbookRows = 8;
            flipbookRandomStart = true;
            flipbookMode = ParticleFlipbookMode::FixedRate;
            flipbookFps = 30.0f;
            startRotationMin = -15.0f;
            startRotationMax = 15.0f;
            emitterShape = ParticleEmitterShape::Cone;
            coneAngle = 18.0f;
            emitterRadius = 0.2f;
            startSpeed = 1.1f;
            gravityMultiplier = -0.2f;
            lifetimeMin = 0.45f;
            lifetimeMax = 0.75f;
            startSize = 0.7f;
            endSize = 0.3f;
            startColor = glm::vec4(1.0f, 0.92f, 0.85f, 1.0f);
            endColor = glm::vec4(1.0f, 0.65f, 0.45f, 0.0f);
            glowIntensity = 1.8f;
            spawnRate = 40.0f;
            maxParticles = 300;
            enableCurlNoiseModule = true;
            curlNoiseStrength = 0.8f;
            curlNoiseFrequency = 1.2f;
            softness = 0.3f;
            break;

        case ParticlePreset::CampfireSmoke:
            renderMode = ParticleRenderMode::BillboardAlpha;
            builtinSheet = ParticleSheet::Smoke;
            flipbookColumns = flipbookRows = 8;
            flipbookRandomStart = true;
            startRotationMin = 0.0f;
            startRotationMax = 360.0f;
            angularVelocityMin = -20.0f;
            angularVelocityMax = 20.0f;
            emitterShape = ParticleEmitterShape::Cone;
            coneAngle = 22.0f;
            emitterRadius = 0.35f;
            startSpeed = 1.5f;
            gravityMultiplier = -0.30f;
            lifetimeMin = 2.5f;
            lifetimeMax = 4.5f;
            startSize = 0.6f;
            endSize = 2.8f;
            startColor = glm::vec4(0.45f, 0.45f, 0.47f, 0.55f);
            endColor = glm::vec4(0.30f, 0.30f, 0.32f, 0.0f);
            spawnRate = 18.0f;
            maxParticles = 300;
            drag = 0.3f;
            enableCurlNoiseModule = true;
            curlNoiseStrength = 0.8f;
            curlNoiseFrequency = 0.6f;
            softness = 0.8f;
            break;

        case ParticlePreset::GroundFog:
            renderMode = ParticleRenderMode::BillboardAlpha;
            builtinSheet = ParticleSheet::Smoke;
            flipbookColumns = flipbookRows = 8;
            flipbookRandomStart = true;
            flipbookMode = ParticleFlipbookMode::FixedRate;
            flipbookFps = 2.0f;
            startRotationMin = 0.0f;
            startRotationMax = 360.0f;
            angularVelocityMin = -5.0f;
            angularVelocityMax = 5.0f;
            emitterShape = ParticleEmitterShape::Box;
            emitterBoxExtents = glm::vec3(8.0f, 0.4f, 8.0f);
            startSpeed = 0.15f;
            gravityMultiplier = 0.0f;
            lifetimeMin = 4.0f;
            lifetimeMax = 7.0f;
            startSize = 3.0f;
            endSize = 4.0f;
            startColor = glm::vec4(0.82f, 0.86f, 0.90f, 0.22f);
            endColor = glm::vec4(0.78f, 0.82f, 0.88f, 0.0f);
            spawnRate = 20.0f;
            maxParticles = 400;
            enableCurlNoiseModule = true;
            curlNoiseStrength = 0.4f;
            curlNoiseFrequency = 0.3f;
            softness = 1.5f;
            break;

        case ParticlePreset::MagicSparkles:
            renderMode = ParticleRenderMode::BillboardAdditive;
            builtinSheet = ParticleSheet::Sparks;
            flipbookColumns = flipbookRows = 8;
            flipbookRandomStart = true;
            flipbookMode = ParticleFlipbookMode::FixedRate;
            flipbookFps = 20.0f;
            emitterShape = ParticleEmitterShape::Sphere;
            emitterRadius = 0.8f;
            startSpeed = 0.9f;
            gravityMultiplier = -0.15f;
            lifetimeMin = 1.5f;
            lifetimeMax = 3.0f;
            startSize = 0.25f;
            endSize = 0.04f;
            startColor = glm::vec4(0.25f, 0.85f, 1.0f, 1.0f);
            endColor = glm::vec4(0.85f, 0.20f, 1.0f, 0.0f);
            spawnRate = 50.0f;
            maxParticles = 500;
            enableVortexModule = true;
            vortexStrength = 2.5f;
            vortexAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            enableCurlNoiseModule = true;
            curlNoiseStrength = 0.6f;
            curlNoiseFrequency = 1.0f;
            trailMode = ParticleTrailMode::PerParticle;
            trailLength = 12;
            trailInterval = 0.03f;
            trailWidth = 0.5f;
            break;

        case ParticlePreset::ExplosionBurst:
            // Alpha-blended so the smoke hides what's behind it; the fire in it glows
            renderMode = ParticleRenderMode::BillboardAlpha;
            selfGlow = 5.0f;
            builtinSheet = ParticleSheet::Explosion;
            flipbookColumns = flipbookRows = 8;
            startRotationMin = 0.0f;
            startRotationMax = 360.0f;
            emitterShape = ParticleEmitterShape::Sphere;
            emitterRadius = 0.4f;
            startSpeed = 4.0f;
            gravityMultiplier = -0.1f;
            drag = 2.5f;
            lifetimeMin = 0.9f;
            lifetimeMax = 1.4f;
            startSize = 1.6f;
            endSize = 3.2f;
            startColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.7f);
            spawnRate = 0.0f;
            maxParticles = 200;
            isEmitting = false;
            looping = false;
            enableCurlNoiseModule = true;
            curlNoiseStrength = 1.5f;
            curlNoiseFrequency = 1.5f;
            softness = 1.0f;
            break;

        case ParticlePreset::Sparks:
            renderMode = ParticleRenderMode::BillboardAdditive;
            emitterShape = ParticleEmitterShape::Cone;
            coneAngle = 35.0f;
            emitterRadius = 0.05f;
            startSpeed = 6.0f;
            gravityMultiplier = 1.0f;
            lifetimeMin = 0.8f;
            lifetimeMax = 1.6f;
            startSize = 0.06f;
            endSize = 0.03f;
            startColor = glm::vec4(1.0f, 0.85f, 0.45f, 1.0f);
            endColor = glm::vec4(1.0f, 0.35f, 0.05f, 0.0f);
            glowIntensity = 6.0f;
            spawnRate = 60.0f;
            maxParticles = 600;
            velocityStretch = 0.025f;
            trailMode = ParticleTrailMode::PerParticle;
            trailLength = 8;
            trailInterval = 0.02f;
            trailWidth = 0.6f;
            collisionMode = ParticleCollisionMode::Scene;
            bounce = 0.4f;
            friction = 0.3f;
            softness = 0.0f;
            break;

        case ParticlePreset::HeatHaze:
            renderMode = ParticleRenderMode::Distortion;
            emitterShape = ParticleEmitterShape::Cone;
            coneAngle = 10.0f;
            emitterRadius = 0.3f;
            startSpeed = 1.2f;
            gravityMultiplier = -0.1f;
            lifetimeMin = 1.0f;
            lifetimeMax = 1.8f;
            startSize = 0.8f;
            endSize = 1.6f;
            startColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            distortionStrength = 0.02f;
            spawnRate = 14.0f;
            maxParticles = 100;
            enableCurlNoiseModule = true;
            curlNoiseStrength = 0.4f;
            curlNoiseFrequency = 1.0f;
            softness = 0.3f;
            break;

        case ParticlePreset::MagicRibbon:
            renderMode = ParticleRenderMode::BillboardAdditive;
            emitterShape = ParticleEmitterShape::Point;
            startSpeed = 0.0f;
            gravityMultiplier = 0.0f;
            lifetimeMin = lifetimeMax = 0.6f;
            startSize = 0.35f;
            endSize = 0.0f;
            startColor = glm::vec4(0.4f, 0.8f, 1.0f, 1.0f);
            endColor = glm::vec4(0.6f, 0.2f, 1.0f, 0.0f);
            spawnRate = 90.0f;
            maxParticles = 200;
            trailMode = ParticleTrailMode::Ribbon;
            trailWidth = 1.0f;
            trailDrawParticles = false;
            softness = 0.0f;
            break;

        case ParticlePreset::RainSplashes:
            renderMode = ParticleRenderMode::BillboardAlpha;
            emitterShape = ParticleEmitterShape::Box;
            emitterBoxExtents = glm::vec3(6.0f, 0.1f, 6.0f);
            startSpeed = 0.0f;
            gravityMultiplier = 1.0f;
            lifetimeMin = lifetimeMax = 3.0f;
            startSize = 0.03f;
            endSize = 0.03f;
            startColor = glm::vec4(0.75f, 0.8f, 0.9f, 0.6f);
            endColor = glm::vec4(0.75f, 0.8f, 0.9f, 0.6f);
            velocityStretch = 0.03f;
            spawnRate = 150.0f;
            maxParticles = 1500;
            collisionMode = ParticleCollisionMode::Scene;
            killOnCollision = true;
            hitSpawnCount = 3;
            hitSpawnSpeed = 1.2f;
            hitSpawnSize = 0.6f;
            lit = true;
            softness = 0.0f;
            break;

        default:
            break;
    }
    ResetParticles();
}

namespace {

// Texture path field that accepts textures dragged from the asset browser
void TextureField(const char* label, std::string& path) {
    char buffer[260];
    std::strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    if (ImGui::InputTextWithHint(label, "none: built-in sheet or a soft dot", buffer, sizeof(buffer))) path = buffer;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_TEXTURE_PATH")) {
            path = std::string(static_cast<const char*>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
        }
        ImGui::EndDragDropTarget();
    }
}

} // namespace

void ParticleNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Particle System & Niagara Modules", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 1-Click VFX Presets
        const char* presetNames[] = {
            "Select 1-Click VFX Preset...",
            "Fire / Torch Flame",
            "Campfire Smoke",
            "Ground Fog / Mist",
            "Magic Sparkles",
            "Explosion Burst",
            "Sparks (bounce, trails)",
            "Heat Haze",
            "Magic Ribbon",
            "Rain Splashes"
        };
        static int selectedPreset = 0;
        if (ImGui::Combo("VFX Preset", &selectedPreset, presetNames, IM_ARRAYSIZE(presetNames))) {
            if (selectedPreset > 0) {
                ApplyPreset(static_cast<ParticlePreset>(selectedPreset));
                if (!isEmitting) EmitBurst(60, scene);
                selectedPreset = 0;
            }
        }

        const char* renderModes[] = {
            "Billboard (Additive Glow)",
            "Billboard (Alpha Blend)",
            "3D Mesh (Sphere)",
            "3D Mesh (Cube)",
            "Distortion (Heat Haze)"
        };
        int curRMode = static_cast<int>(renderMode);
        if (ImGui::Combo("Render Mode", &curRMode, renderModes, IM_ARRAYSIZE(renderModes))) {
            renderMode = static_cast<ParticleRenderMode>(curRMode);
        }
        ImGui::Separator();
        const char* simModes[] = { "CPU Particle Emitter", "GPU Niagara Compute Stack" };
        int curSim = static_cast<int>(simulationMode);
        if (ImGui::Combo("Simulation Backend", &curSim, simModes, IM_ARRAYSIZE(simModes))) {
            simulationMode = static_cast<ParticleSimulationMode>(curSim);
            ResetParticles();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("GPU: a compute shader moves the particles (hundreds of thousands), with the same forces,\n"
                              "collision (scene shapes, terrain, water), flipbooks and trails. Alpha particles are not sorted.");
        }

        const char* shapes[] = { "Point", "Sphere", "Box", "Cone" };
        int curShape = static_cast<int>(emitterShape);
        if (ImGui::Combo("Emitter Shape", &curShape, shapes, IM_ARRAYSIZE(shapes))) {
            emitterShape = static_cast<ParticleEmitterShape>(curShape);
        }

        ImGui::Checkbox("Is Emitting", &isEmitting);
        ImGui::SameLine();
        ImGui::Checkbox("Looping", &looping);
        ImGui::SameLine();
        ImGui::Checkbox("World Space", &worldSpace);

        int maxP = static_cast<int>(maxParticles);
        if (ImGui::DragInt("Max Particles", &maxP, 10, 10, simulationMode == ParticleSimulationMode::GPU_Niagara ? 1000000 : 100000)) {
            maxParticles = static_cast<uint32_t>(std::max(10, maxP));
        }

        ImGui::SliderFloat("Spawn Rate (/s)", &spawnRate, 0.0f, simulationMode == ParticleSimulationMode::GPU_Niagara ? 100000.0f : 1000.0f, "%.1f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloatRange2("Lifetime (Min/Max)", &lifetimeMin, &lifetimeMax, 0.05f, 0.05f, 60.0f);
        ImGui::SliderFloat("Start Speed", &startSpeed, 0.0f, 50.0f, "%.1f m/s");
        ImGui::DragFloat("Start Size", &startSize, 0.01f, 0.001f, 20.0f);
        ImGui::DragFloat("End Size", &endSize, 0.01f, 0.0f, 20.0f);
        ImGui::ColorEdit4("Start Color", glm::value_ptr(startColor));
        ImGui::ColorEdit4("End Color", glm::value_ptr(endColor));
        ImGui::SliderFloat("Gravity Multiplier", &gravityMultiplier, -5.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Drag", &drag, 0.0f, 10.0f, "%.2f /s");
        ImGui::DragFloatRange2("Start Rotation", &startRotationMin, &startRotationMax, 1.0f, -360.0f, 360.0f, "%.0f deg");
        ImGui::DragFloatRange2("Spin", &angularVelocityMin, &angularVelocityMax, 1.0f, -720.0f, 720.0f, "%.0f deg/s");

        if (emitterShape == ParticleEmitterShape::Sphere || emitterShape == ParticleEmitterShape::Cone) {
            ImGui::SliderFloat("Emitter Radius", &emitterRadius, 0.01f, 25.0f);
        }
        if (emitterShape == ParticleEmitterShape::Cone) {
            ImGui::SliderFloat("Cone Angle", &coneAngle, 1.0f, 89.0f);
        }
        if (emitterShape == ParticleEmitterShape::Box) {
            ImGui::DragFloat3("Box Extents", glm::value_ptr(emitterBoxExtents), 0.1f, 0.01f, 100.0f);
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Niagara Force Modules:");
        ImGui::Checkbox("Vortex Force Module", &enableVortexModule);
        if (enableVortexModule) {
            ImGui::SliderFloat("Vortex Strength", &vortexStrength, -25.0f, 25.0f);
            ImGui::DragFloat3("Vortex Axis", glm::value_ptr(vortexAxis), 0.05f);
        }

        ImGui::Checkbox("Point Attractor Module", &enableAttractorModule);
        if (enableAttractorModule) {
            ImGui::DragFloat3("Attractor Offset", glm::value_ptr(attractorOffset), 0.1f);
            ImGui::SliderFloat("Attractor Strength", &attractorStrength, -50.0f, 50.0f);
        }

        ImGui::Checkbox("3D Curl Noise Turbulence", &enableCurlNoiseModule);
        if (enableCurlNoiseModule) {
            ImGui::SliderFloat("Curl Strength", &curlNoiseStrength, 0.0f, 25.0f);
            ImGui::SliderFloat("Curl Frequency", &curlNoiseFrequency, 0.05f, 10.0f);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Active Particles: %u / %u   Hits: %u", GetAliveParticleCount(), maxParticles, m_totalCollisionHits);
        if (ImGui::Button("Emit Burst (50)")) {
            EmitBurst(50, scene);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            ResetParticles();
        }
    }

    const bool sprites = renderMode != ParticleRenderMode::MeshSphere && renderMode != ParticleRenderMode::MeshCube;
    if (sprites && ImGui::CollapsingHeader("Look: Sprite Sheet, Soft Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        TextureField("Texture", texturePath);
        if (texturePath.empty()) {
            const char* sheets[] = { "None (soft dot)", "Fire", "Smoke", "Explosion", "Sparks" };
            int sheet = static_cast<int>(builtinSheet);
            if (ImGui::Combo("Built-in Sheet", &sheet, sheets, IM_ARRAYSIZE(sheets))) {
                builtinSheet = static_cast<ParticleSheet>(sheet);
                if (builtinSheet != ParticleSheet::None) flipbookColumns = flipbookRows = 8;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animated 8 x 8 sprite sheets the engine draws itself (used without a texture).");
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragInt("Columns", &flipbookColumns, 0.1f, 1, 32);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragInt("Rows", &flipbookRows, 0.1f, 1, 32);
        if (FlipbookFrameCount() > 1 || flipbookColumns * flipbookRows > 1) {
            ImGui::DragInt("Frames (0 = all)", &flipbookFrames, 0.2f, 0, flipbookColumns * flipbookRows);
            const char* modes[] = { "Over Lifetime", "Fixed Rate" };
            int mode = static_cast<int>(flipbookMode);
            if (ImGui::Combo("Playback", &mode, modes, IM_ARRAYSIZE(modes))) flipbookMode = static_cast<ParticleFlipbookMode>(mode);
            if (flipbookMode == ParticleFlipbookMode::FixedRate) ImGui::SliderFloat("Frames / s", &flipbookFps, 0.5f, 120.0f, "%.1f");
            else ImGui::SliderFloat("Cycles per Life", &flipbookCycles, 0.1f, 10.0f, "%.2f");
            ImGui::Checkbox("Random Start Frame", &flipbookRandomStart);
            ImGui::SameLine();
            ImGui::Checkbox("Blend Frames", &flipbookBlend);
        }
        ImGui::SliderFloat("Softness (m)", &softness, 0.0f, 5.0f, softness > 0.0f ? "%.2f" : "Off");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft particles: fade out where they meet the scene behind them, hiding the hard line.");
        ImGui::SliderFloat("Camera Fade (m)", &cameraFade, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Velocity Stretch (s)", &velocityStretch, 0.0f, 0.2f, velocityStretch > 0.0f ? "%.3f" : "Off");
        if (renderMode == ParticleRenderMode::BillboardAlpha) {
            ImGui::Checkbox("Lit (sun, sky, lights)", &lit);
            ImGui::SliderFloat("Self Glow", &selfGlow, 0.0f, 20.0f, selfGlow > 0.0f ? "%.2f" : "Off");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The sprite's bright parts give light (the fire in an explosion's smoke).");
        }
        if (renderMode == ParticleRenderMode::BillboardAdditive) ImGui::SliderFloat("Glow Intensity", &glowIntensity, 0.0f, 20.0f, "%.2f");
        if (renderMode == ParticleRenderMode::Distortion) ImGui::SliderFloat("Distortion", &distortionStrength, 0.0f, 0.15f, "%.3f");
    }

    if (!sprites && ImGui::CollapsingHeader("Look: Mesh Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Self Glow", &selfGlow, 0.0f, 20.0f, selfGlow > 0.0f ? "%.2f" : "Off");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lit spheres or cubes; with glow they shine by their own colour too (embers).");
        ImGui::DragFloatRange2("Spin", &angularVelocityMin, &angularVelocityMax, 1.0f, -720.0f, 720.0f, "%.0f deg/s");
    }

    if (ImGui::CollapsingHeader("Trails & Ribbons")) {
        const char* modes[] = { "None", "Per Particle (trails)", "Ribbon (through the particles)" };
        int mode = static_cast<int>(trailMode);
        if (ImGui::Combo("Trail Mode", &mode, modes, IM_ARRAYSIZE(modes))) trailMode = static_cast<ParticleTrailMode>(mode);
        if (trailMode == ParticleTrailMode::PerParticle) {
            ImGui::SliderInt("Points", &trailLength, 2, 64);
            ImGui::SliderFloat("Point Interval (s)", &trailInterval, 0.005f, 0.25f, "%.3f");
            ImGui::TextDisabled("Trail length: %.2f s", TrailLifetime());
        }
        if (trailMode != ParticleTrailMode::None) {
            ImGui::SliderFloat("Width (of size)", &trailWidth, 0.05f, 4.0f, "%.2f");
            ImGui::Checkbox("Taper", &trailTaper);
            ImGui::SameLine();
            ImGui::Checkbox("Draw Particles Too", &trailDrawParticles);
            ImGui::SliderFloat("Texture Tiling", &trailTextureTiling, 0.1f, 20.0f, "%.2f");
        }
    }

    if (ImGui::CollapsingHeader("Collision")) {
        const char* modes[] = { "None", "Ground Plane", "Scene (meshes, terrain, water)" };
        int mode = static_cast<int>(collisionMode);
        if (ImGui::Combo("Collide With", &mode, modes, IM_ARRAYSIZE(modes))) collisionMode = static_cast<ParticleCollisionMode>(mode);
        if (collisionMode == ParticleCollisionMode::GroundPlane) ImGui::DragFloat("Plane Height (m)", &collisionPlaneHeight, 0.05f);
        if (collisionMode == ParticleCollisionMode::Scene) ImGui::Checkbox("Sink Into Water", &collideWithWater);
        if (collisionMode != ParticleCollisionMode::None) {
            ImGui::SliderFloat("Radius (of size)", &collisionRadiusScale, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Die On Hit", &killOnCollision);
            if (!killOnCollision) {
                ImGui::SliderFloat("Bounce", &bounce, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Life Lost per Hit", &lifetimeLossOnHit, 0.0f, 1.0f, "%.2f");
            }
            ImGui::SliderInt("Spawn on Hit", &hitSpawnCount, 0, 16);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Particles thrown up where one hits (splashes, sparks). They don't spawn more.");
            if (hitSpawnCount > 0) {
                ImGui::SliderFloat("Spawn Speed", &hitSpawnSpeed, 0.0f, 20.0f, "%.1f m/s");
                ImGui::SliderFloat("Spawn Size (of start)", &hitSpawnSize, 0.05f, 2.0f, "%.2f");
            }
        }
    }
}
