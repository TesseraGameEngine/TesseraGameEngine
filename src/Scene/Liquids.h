#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Scene;
struct Node;

// Liquids in containers, and pouring between them (all faked, as games do it):
// - A Liquid node is a mesh inside a container, cut off at its fill level by shader.frag; the
//   surface tilts and sloshes when the container accelerates.
// - A Pour Spout node streams liquid along its forward direction (-Z) once that has turned
//   further from straight up than its start angle: a ballistic arc that narrows as it speeds
//   up, drawn as refractive water. The stream drains the spout's source liquid and fills the
//   liquid it lands in, with splashes.
namespace Liquids {

struct StreamSegment {
    glm::vec3 start;
    glm::vec3 end;
    float radius;
    glm::vec4 color;
};

// A droplet drawn as a soft sprite, stretched along its motion
struct Droplet {
    glm::vec3 position;
    float size;
    glm::vec4 color;
    glm::vec3 stretch; // world-space streak (zero = round)
};

class Simulation {
public:
    // Moves every liquid surface (fill level and slosh) and, with streams, runs the pour spouts
    // and splashes. transferLiquid changes fill amounts (Play mode); otherwise streams are shown
    // without draining or filling anything.
    void Update(Scene& scene, float dt, bool streams, bool transferLiquid);
    void Clear();

    const std::vector<StreamSegment>& Segments() const { return m_segments; }
    // Streams pouring into a Fluid Volume this frame: the fluid simulation adds particles there
    struct FluidFeed {
        uint32_t fluidNodeId;
        glm::vec3 position;
        glm::vec3 velocity;
        float volumePerSecond; // m^3/s
    };
    const std::vector<FluidFeed>& FluidFeeds() const { return m_fluidFeeds; }
    const std::vector<Droplet>& Droplets() const { return m_droplets; }

private:
    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        float age;
        float life;
        float size;
        float floor; // dies below this height (the surface it splashed out of)
        glm::vec4 area; // stays inside this x / z box (the container it splashed in): min x, min z, max x, max z
        glm::vec4 color;
    };
    void UpdateSurface(Node& liquid, const Scene& scene, float dt);
    void RunSpout(Scene& scene, Node& spout, float dt, bool transferLiquid);
    float Random();

    std::vector<StreamSegment> m_segments;
    std::vector<Droplet> m_droplets;
    std::vector<Particle> m_particles;
    std::vector<FluidFeed> m_fluidFeeds;
    uint32_t m_seed{98765u};
};

// How much a Liquid node's container holds (m^3): its shape's volume

float ContainerVolume(const Node& node, const Scene& scene);

// World-space box of a node's mesh
void WorldBounds(const Node& node, const Scene& scene, glm::vec3& lo, glm::vec3& hi);

} // namespace Liquids
