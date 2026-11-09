#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct Node;
class Scene;

// The ocean's waves on the CPU: the same Gerstner sum as shaders/ocean_common.glsl, so floating
// objects, spray and the underwater check follow the waves that are drawn.
namespace OceanWaves {

// Seconds the waves have run; the renderer animates by the same clock (ubo.timeParams.x).
// main sets it once per frame.
void SetTime(float seconds);
float Time();

struct SurfaceSample {
    float height{0.0f};                  // world height of the water surface
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};            // how the water there is moving (m/s)
};

// Water surface above or below the world position (x, z)
SurfaceSample SampleSurface(const Node& ocean, const Scene& scene, float x, float z, float time);

// The ocean node covering (x, z), or nullptr
std::shared_ptr<Node> FindOceanAt(const Scene& scene, float x, float z);

// Spray where waves hit rocks and other fixed objects standing in the water. Points around each
// object's waterline watch the waves; when a crest passes one, it throws a burst of droplets.
class Spray {
public:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float age{0.0f};
        float life{1.0f};
        float size{0.3f};
        bool splash{false}; // the burst itself, drawn as an animated splash (flipbook)
        float seed{0.0f};
    };
    // Drawn as soft camera-facing sprites (VulkanPipeline::SetSprayParticles). frame >= 0: an
    // animated splash at that point of its life (0..1)
    struct Sprite {
        glm::vec3 position;
        float size;
        glm::vec4 color;
        float frame{-1.0f};
        float seed{0.0f};
    };
    // A crest breaking on an object this frame (for its crash sound)
    struct Burst {
        glm::vec3 position;
        float strength; // 0..1.5
    };

    // Only objects within range of the focus (the camera) throw spray: it can't be seen further
    void Update(const Scene& scene, float dt, const glm::vec3& focus = glm::vec3(0.0f), float range = 1e9f);
    void Clear();
    const std::vector<Sprite>& Sprites() const { return m_sprites; }
    const std::vector<Burst>& Bursts() const { return m_bursts; }

private:
    struct Watch {
        float lastHeight{0.0f};
        bool rising{false};
        float cooldown{0.0f};
        bool valid{false};
    };
    std::vector<Particle> m_particles;
    std::vector<Sprite> m_sprites;
    std::vector<Burst> m_bursts;
    std::unordered_map<uint64_t, Watch> m_watches; // by object id and point
    // Fixed objects small enough to throw spray, with their world bounds: found by walking every
    // node, so kept, and refreshed when the scene changes or once a second (an object moved in
    // the editor)
    struct Candidate { uint32_t id; glm::vec3 lo, hi; };
    std::vector<Candidate> m_candidates;
    const Scene* m_candidateScene{nullptr};
    size_t m_candidateNodeCount{0};
    float m_candidateAge{1e9f};
    uint32_t m_seed{12345u};
    float Random();
};

} // namespace OceanWaves
