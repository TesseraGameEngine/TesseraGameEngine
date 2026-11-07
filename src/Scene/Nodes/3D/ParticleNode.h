#pragma once

#include "Scene/Node.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

enum class ParticleEmitterShape {
    Point = 0,
    Sphere = 1,
    Box = 2,
    Cone = 3
};

enum class ParticleRenderMode {
    BillboardAdditive = 0,
    BillboardAlpha = 1,
    MeshSphere = 2,
    MeshCube = 3,
    Distortion = 4 // heat haze: bends the scene behind it
};

// Sprite-sheet (flipbook) playback
enum class ParticleFlipbookMode {
    OverLifetime = 0, // the sheet plays over each particle's life (flipbookCycles times)
    FixedRate = 1     // flipbookFps frames per second, looping
};

// Sprite sheets the engine draws itself (ParticleSheets), used when the emitter has no texture
enum class ParticleSheet {
    None = 0, // a soft round blob
    Fire = 1,
    Smoke = 2,
    Explosion = 3,
    Sparks = 4
};

enum class ParticleTrailMode {
    None = 0,
    PerParticle = 1, // each particle leaves a fading trail (tracers, sparks, magic)
    Ribbon = 2       // one ribbon through the particles in the order they were born (sword swings)
};

enum class ParticleCollisionMode {
    None = 0,
    GroundPlane = 1, // a flat floor at collisionPlaneHeight
    Scene = 2        // solid meshes, terrain and (optionally) water
};

enum class ParticlePreset {
    Custom = 0,
    FireTorch = 1,
    CampfireSmoke = 2,
    GroundFog = 3,
    MagicSparkles = 4,
    ExplosionBurst = 5,
    Sparks = 6,
    HeatHaze = 7,
    MagicRibbon = 8,
    RainSplashes = 9
};


enum class ParticleSimulationMode {
    CPU = 0,
    GPU_Niagara = 1
};

struct ParticleInstance {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f, 1.0f, 0.0f};
    glm::vec4 color{1.0f};
    float size{0.2f};
    float age{0.0f};
    float lifetime{2.0f};
    bool alive{false};
    float rotation{0.0f};        // radians, around the view axis
    float angularVelocity{0.0f}; // radians per second
    float frameOffset{0.0f};     // flipbook frames to start at
    float seed{0.0f};            // 0..1, per particle
    float sizeScale{1.0f};       // collision spawns are smaller
    bool child{false};           // spawned by a collision (spawns no more)
    uint64_t spawnIndex{0};      // birth order (ribbons)
    uint16_t trailHead{0};       // newest point in the trail ring
    uint16_t trailCount{0};      // points in the trail (a dead particle's trail fades out first)
    float trailTimer{0.0f};
};

// One camera-facing quad for the renderer (VulkanPipeline::DrawParticles; the layout matches
// shaders/particle_common.glsl): a sprite, or a segment of a trail or ribbon
struct ParticleQuad {
    glm::vec4 a{0.0f};      // xyz = position (segment: start), w = half size (segment: half width at the start)
    glm::vec4 b{0.0f};      // sprite: xyz = velocity, w = rotation; segment: xyz = end, w = half width at the end
    glm::vec4 colorA{1.0f};
    glm::vec4 colorB{1.0f}; // segment: colour at the end
    glm::vec4 params{0.0f}; // sprite: x = flipbook frame, y = seed, z = -1; segment: z = v at the start, w = v at the end
};
static_assert(sizeof(ParticleQuad) == 80, "ParticleQuad matches particle_common.glsl");

// GPU-aligned particle struct for compute / SSBO Niagara-style simulation
struct alignas(16) GPUParticleData {
    glm::vec4 positionAndSize{0.0f, 0.0f, 0.0f, 0.2f};
    glm::vec4 velocityAndAge{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 params{2.0f, 1.0f, 0.0f, 0.0f}; // x = lifetime, y = alive
};

class ParticleNode : public Node {
public:
    ParticleNode(const std::string& name = "Particle Emitter", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    void Update(float dt, const Scene& scene);
    void EmitBurst(uint32_t count, const Scene& scene);
    void ResetParticles();
    void ApplyPreset(ParticlePreset preset);

    uint32_t GetAliveParticleCount() const;
    const std::vector<ParticleInstance>& GetParticles() const { return m_particles; }
    std::vector<GPUParticleData> BuildGPUBufferData() const;

    // Quads to draw this emitter's particles and trails (CPU simulation), back to front from the
    // camera when sorted (alpha-blended emitters)
    void BuildQuads(const Scene& scene, const glm::vec3& cameraPos, bool sorted, std::vector<ParticleQuad>& out) const;
    // Flipbook frame of a particle (with its fraction, for blending)
    float FlipbookFrame(const ParticleInstance& p) const;
    int FlipbookFrameCount() const;
    // Whether a built-in sheet is drawn (only without a texture)
    bool UsesBuiltinSheet() const { return texturePath.empty() && builtinSheet != ParticleSheet::None; }
    // Particles that hit something since the last call (Lua: self:GetParticleHits())
    uint32_t TakeCollisionHits() { uint32_t n = m_collisionHits; m_collisionHits = 0; return n; }
    uint32_t TotalCollisionHits() const { return m_totalCollisionHits; }
    glm::vec3 LastHitPosition() const { return m_lastHitPosition; }
    float TrailLifetime() const { return static_cast<float>(std::max(trailLength, 2)) * std::max(trailInterval, 0.001f); }

    // GPU simulation (VulkanParticleSim): the time and spawns waiting for the next GPU step
    float TakeGpuTime() { float t = m_gpuPendingTime; m_gpuPendingTime = 0.0f; return t; }
    uint32_t TakeGpuSpawns() { uint32_t n = m_gpuPendingSpawns; m_gpuPendingSpawns = 0; return n; }
    bool TakeGpuReset() { bool r = m_gpuReset; m_gpuReset = false; return r; }
    // What the GPU reports back (alive particles and hits, a frame or two late)
    void SetGpuFeedback(uint32_t alive, uint32_t hits, const glm::vec3& lastHit);

    ParticleSimulationMode simulationMode{ParticleSimulationMode::CPU};
    ParticleEmitterShape emitterShape{ParticleEmitterShape::Cone};
    ParticleRenderMode renderMode{ParticleRenderMode::BillboardAdditive};

    uint32_t maxParticles{1000};
    float spawnRate{40.0f};
    float lifetimeMin{1.0f};
    float lifetimeMax{2.5f};
    float startSpeed{3.5f};
    float startSize{0.25f};
    float endSize{0.05f};
    glm::vec4 startColor{1.0f, 0.65f, 0.15f, 1.0f};
    glm::vec4 endColor{0.9f, 0.1f, 0.05f, 0.0f};
    float gravityMultiplier{-0.5f}; // Negative rises like fire/smoke, positive falls

    float emitterRadius{0.5f};
    glm::vec3 emitterBoxExtents{1.0f, 1.0f, 1.0f};
    float coneAngle{25.0f};

    bool isEmitting{true};
    bool looping{true};
    bool worldSpace{true};

    // Niagara-style Modular Force Stack
    bool enableVortexModule{false};
    float vortexStrength{3.0f};
    glm::vec3 vortexAxis{0.0f, 1.0f, 0.0f};

    bool enableAttractorModule{false};
    glm::vec3 attractorOffset{0.0f, 3.0f, 0.0f};
    float attractorStrength{5.0f};

    bool enableCurlNoiseModule{false};
    float curlNoiseStrength{1.5f};
    float curlNoiseFrequency{1.0f};
    float drag{0.0f}; // air resistance (1/s)

    // Rotation (degrees)
    float startRotationMin{0.0f};
    float startRotationMax{0.0f};
    float angularVelocityMin{0.0f};
    float angularVelocityMax{0.0f};

    // Look
    ParticleSheet builtinSheet{ParticleSheet::None};
    int flipbookColumns{1};
    int flipbookRows{1};
    int flipbookFrames{0}; // 0 = columns x rows
    ParticleFlipbookMode flipbookMode{ParticleFlipbookMode::OverLifetime};
    float flipbookFps{24.0f};
    float flipbookCycles{1.0f};
    bool flipbookRandomStart{false};
    bool flipbookBlend{true};        // cross-fade between frames
    float softness{0.4f};            // soft particles: fade over this distance (m) in front of what's behind (0 = off)
    float cameraFade{0.3f};          // fade out this close to the camera (m)
    bool lit{true};                  // alpha-blended particles: lit by the sun, sky and lights
    float glowIntensity{2.5f};       // additive particles: brightness
    float selfGlow{0.0f};            // alpha-blended particles: how much the sprite's bright parts glow (fire in smoke)
    float velocityStretch{0.0f};     // stretch along the motion (seconds of travel; sparks, rain)
    float distortionStrength{0.03f}; // heat haze: how far the scene bends (share of the screen)

    // Trails and ribbons
    ParticleTrailMode trailMode{ParticleTrailMode::None};
    int trailLength{16};             // points per trail
    float trailInterval{0.02f};      // seconds between points
    float trailWidth{1.0f};          // of the particle's size
    bool trailTaper{true};           // narrows towards the tail
    bool trailDrawParticles{true};   // also draw the particles themselves
    float trailTextureTiling{1.0f};  // texture repeats along a trail

    // Collision
    ParticleCollisionMode collisionMode{ParticleCollisionMode::None};
    float collisionPlaneHeight{0.0f}; // world height of the ground plane
    bool collideWithWater{true};      // Scene mode: particles sink into oceans and liquids
    float collisionRadiusScale{0.5f}; // particle radius = size x this
    float bounce{0.35f};              // restitution
    float friction{0.25f};            // share of the speed along the surface lost per hit
    bool killOnCollision{false};
    float lifetimeLossOnHit{0.0f};    // share of the lifetime used up per hit
    int hitSpawnCount{0};             // particles thrown up where one hits (splashes, sparks)
    float hitSpawnSpeed{2.0f};
    float hitSpawnSize{0.4f};         // of the start size

private:
    ParticleInstance* SpawnSingleParticle(const glm::vec3& origin);
    ParticleInstance* FreeSlot();
    void SpawnHitParticles(const glm::vec3& position, const glm::vec3& normal, const ParticleInstance& parent);
    void RecordTrail(ParticleInstance& p, size_t index, float dt);
    void ResizePool();
    glm::vec3 SampleCurlNoise(const glm::vec3& pos) const;

    std::vector<ParticleInstance> m_particles;
    std::vector<glm::vec4> m_trailPoints; // trailLength per particle: xyz, w = time recorded
    int m_trailStride{0};
    float m_time{0.0f};
    uint64_t m_nextSpawnIndex{1};
    uint32_t m_collisionHits{0};
    uint32_t m_totalCollisionHits{0};
    glm::vec3 m_lastHitPosition{0.0f};
    float m_gpuPendingTime{0.0f};
    uint32_t m_gpuPendingSpawns{0};
    bool m_gpuReset{true};
    uint32_t m_gpuAlive{0};
    float m_spawnAccumulator{0.0f};
    uint32_t m_rngState{1337u};
    float NextRandom01();
};
