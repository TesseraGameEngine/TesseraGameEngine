#include "Liquids.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace Liquids {

namespace {

constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265f;

// The solid a liquid's mesh is treated as: primitives by their own shape, models by their box
ShapeType LiquidShape(const Node& node) {
    switch (node.type) {
        case ShapeType::Cylinder: case ShapeType::Sphere: case ShapeType::Cone: return node.type;
        default: return ShapeType::Cube;
    }
}

// Points spread evenly through the unit shape (local space, -0.5..0.5), for finding the level
// that holds a given share of the volume however the container is turned
const std::vector<glm::vec3>& ShapeSamples(ShapeType shape) {
    static std::map<int, std::vector<glm::vec3>> cache;
    auto it = cache.find(static_cast<int>(shape));
    if (it != cache.end()) return it->second;
    std::vector<glm::vec3> points;
    constexpr int n = 14;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                const glm::vec3 p = (glm::vec3(i, j, k) + 0.5f) / static_cast<float>(n) - 0.5f;
                const float r2 = p.x * p.x + p.z * p.z;
                bool inside = true;
                if (shape == ShapeType::Cylinder) inside = r2 <= 0.25f;
                else if (shape == ShapeType::Sphere) inside = r2 + p.y * p.y <= 0.25f;
                else if (shape == ShapeType::Cone) inside = r2 <= 0.25f * (0.5f - p.y) * (0.5f - p.y); // apex at the top
                if (inside) points.push_back(p);
            }
        }
    }
    return cache.emplace(static_cast<int>(shape), std::move(points)).first->second;
}

// The shape's share of its unit box
float ShapeFraction(ShapeType shape) {
    switch (shape) {
        case ShapeType::Cylinder: return kPi / 4.0f;
        case ShapeType::Sphere: return kPi / 6.0f;
        case ShapeType::Cone: return kPi / 12.0f;
        default: return 1.0f;
    }
}

// The liquid mesh's local box and world transform (models use their bounds)
glm::mat4 LiquidFrame(const Node& node, const Scene& scene) {
    glm::mat4 frame = node.GetWorldTransformMatrix(scene);
    if (node.type == ShapeType::Custom) {
        const glm::vec3 size = glm::max(node.boundsMax - node.boundsMin, glm::vec3(1e-4f));
        const glm::vec3 center = (node.boundsMax + node.boundsMin) * 0.5f;
        frame = frame * glm::mat4(glm::vec4(size.x, 0, 0, 0), glm::vec4(0, size.y, 0, 0), glm::vec4(0, 0, size.z, 0), glm::vec4(center, 1));
    }
    return frame;
}

// Volume the liquid's mesh holds (m^3)
float LiquidVolume(const Node& node, const Scene& scene) {
    const glm::mat4 frame = LiquidFrame(node, scene);
    return std::max(std::abs(glm::determinant(glm::mat3(frame))) * ShapeFraction(LiquidShape(node)), 1e-9f);
}

// Height of a liquid's (tilted) surface above the horizontal position p
float SurfaceHeightAt(const Node& liquid, const glm::vec2& p) {
    const glm::vec3 n(liquid.liquidPlane);
    return (liquid.liquidPlane.w - n.x * p.x - n.z * p.y) / std::max(n.y, 0.1f);
}

} // namespace

float ContainerVolume(const Node& node, const Scene& scene) {
    return LiquidVolume(node, scene);
}

void WorldBounds(const Node& node, const Scene& scene, glm::vec3& lo, glm::vec3& hi) {
    glm::vec3 localMin(-0.5f), localMax(0.5f);
    if (node.type == ShapeType::Custom) {
        localMin = node.boundsMin;
        localMax = node.boundsMax;
    } else if (node.type == ShapeType::Plane) {
        localMin = glm::vec3(-1.0f, 0.0f, -1.0f);
        localMax = glm::vec3(1.0f, 0.0f, 1.0f);
    }
    const glm::mat4 world = node.GetWorldTransformMatrix(scene);
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? localMax.x : localMin.x, (c & 2) ? localMax.y : localMin.y, (c & 4) ? localMax.z : localMin.z);
        const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

float Simulation::Random() {
    m_seed = m_seed * 1664525u + 1013904223u;
    return static_cast<float>(m_seed >> 8) / 16777216.0f;
}

void Simulation::Clear() {
    m_segments.clear();
    m_fluidFeeds.clear();
    m_droplets.clear();
    m_particles.clear();
}

void Simulation::UpdateSurface(Node& liquid, const Scene& scene, float dt) {
    glm::vec3 lo, hi;
    WorldBounds(liquid, scene, lo, hi);
    const glm::vec3 center = (lo + hi) * 0.5f;
    liquid.liquidWorldMin = lo;
    liquid.liquidWorldMax = hi;
    // Ripples from a stream die away once it stops (spouts raise them again each frame)
    liquid.liquidRipple.w *= std::exp(-2.5f * dt);

    // Slosh: the surface leans toward the container's acceleration (it stays level relative to
    // gravity minus acceleration) and overshoots like a damped spring
    if (liquid.liquidTracked && dt > 1e-4f) {
        const glm::vec3 velocity = (center - liquid.liquidLastPosition) / dt;
        glm::vec3 acceleration = (velocity - liquid.liquidLastVelocity) / dt;
        acceleration = glm::clamp(acceleration, glm::vec3(-60.0f), glm::vec3(60.0f));
        const glm::vec2 target = glm::vec2(acceleration.x, acceleration.z) / kGravity * std::max(liquid.liquidSlosh, 0.0f);
        const float stiffness = 130.0f; // about 1.8 sloshes per second
        const float damping = 2.5f;
        liquid.liquidWobbleVelocity += (stiffness * (target - liquid.liquidWobble)) * dt;
        liquid.liquidWobbleVelocity *= std::exp(-damping * dt);
        liquid.liquidWobble += liquid.liquidWobbleVelocity * dt;
        const float tilt = glm::length(liquid.liquidWobble);
        if (tilt > 0.6f) liquid.liquidWobble *= 0.6f / tilt;
        liquid.liquidLastVelocity = velocity;
    } else {
        liquid.liquidWobble = glm::vec2(0.0f);
        liquid.liquidWobbleVelocity = glm::vec2(0.0f);
        liquid.liquidLastVelocity = glm::vec3(0.0f);
    }
    liquid.liquidLastPosition = center;
    liquid.liquidTracked = true;

    // The level that holds the fill's share of the container's volume, however it is turned:
    // the matching fraction of points spread through the shape lies below the surface
    const glm::vec3 normal = glm::normalize(glm::vec3(liquid.liquidWobble.x, 1.0f, liquid.liquidWobble.y));
    const glm::mat4 frame = LiquidFrame(liquid, scene);
    const auto& samples = ShapeSamples(LiquidShape(liquid));
    std::vector<float> heights(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) heights[i] = glm::dot(normal, glm::vec3(frame * glm::vec4(samples[i], 1.0f)));
    const float fill = glm::clamp(liquid.liquidFill, 0.0f, 1.0f);
    float level;
    if (fill <= 0.0f) {
        level = *std::min_element(heights.begin(), heights.end()) - 1e-4f;
    } else if (fill >= 1.0f) {
        level = *std::max_element(heights.begin(), heights.end()) + 1e-4f;
    } else {
        const size_t k = std::min(static_cast<size_t>(fill * heights.size()), heights.size() - 1);
        std::nth_element(heights.begin(), heights.begin() + k, heights.end());
        level = heights[k];
    }
    liquid.liquidPlane = glm::vec4(normal, level);
}

void Simulation::RunSpout(Scene& scene, Node& spout, float dt, bool transferLiquid) {
    const glm::mat4 world = spout.GetWorldTransformMatrix(scene);
    const glm::vec3 origin(world[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]) + glm::vec3(0.0f, 0.0f, 1e-6f));
    // Tilt: how far the spout has turned from facing straight up
    const float tiltDegrees = glm::degrees(std::acos(glm::clamp(forward.y, -1.0f, 1.0f)));

    float flow = spout.pourFlow >= 0.0f ? glm::clamp(spout.pourFlow, 0.0f, 1.0f)
                                         : glm::clamp((tiltDegrees - spout.pourStartAngle) / 25.0f, 0.0f, 1.0f);
    std::shared_ptr<Node> source = spout.pourSourceId ? scene.GetNodeById(spout.pourSourceId) : nullptr;
    if (spout.pourFlow < 0.0f && source && source->isLiquid) {
        // With a source liquid it pours once the liquid's surface rises above the lip, as the
        // jug tips (fuller jugs pour sooner); full flow when it is well above it
        const float above = source->liquidPlane.w - glm::dot(glm::vec3(source->liquidPlane), origin);
        const float depthScale = std::max(0.15f * (source->liquidWorldMax.y - source->liquidWorldMin.y), 0.003f);
        flow = glm::clamp(above / depthScale, 0.0f, 1.0f);
    }
    if (source && source->isLiquid && source->liquidFill <= 0.001f) flow = 0.0f;
    spout.pourCurrentFlow = flow;
    if (flow < 0.01f) return;

    // The stream: a ballistic arc from the spout. Mass flow stays the same along it, so it
    // narrows as gravity speeds it up.
    const float startRadius = std::max(spout.pourStreamRadius, 0.001f) * std::sqrt(flow);
    const glm::vec3 startVelocity = forward * std::max(spout.pourSpeed, 0.05f) * (0.6f + 0.4f * flow);
    const float startSpeed = glm::length(startVelocity);

    // What the stream can land in: liquids, simulated fluid (it pours into the box), and solid
    // meshes (that it didn't start inside)
    struct Target { Node* node; glm::vec3 lo, hi; bool liquid; bool fluid; };
    std::vector<Target> targets;
    for (const auto& node : scene.GetNodes()) {
        if (!node || node.get() == &spout) continue;
        if (node->isFluidVolume) {
            const glm::vec3 center = node->GetWorldPosition(scene);
            const glm::vec3 half = glm::max(node->fluidSize, glm::vec3(0.05f)) * 0.5f;
            targets.push_back({ node.get(), center - half, center + half, false, true });
            continue;
        }
        if (node->nodeType != NodeType::Mesh || node->isOcean) continue;
        if (!node->isLiquid && node->color.a < 0.99f) continue; // glass walls: the liquid inside counts
        Target t{ node.get(), {}, {}, node->isLiquid, false };
        WorldBounds(*node, scene, t.lo, t.hi);
        const bool containsOrigin = glm::all(glm::greaterThanEqual(origin, t.lo)) && glm::all(glm::lessThanEqual(origin, t.hi));
        if (containsOrigin) continue; // the jug the stream comes out of
        targets.push_back(t);
    }
    Node* fluidHit = nullptr;
    glm::vec3 fluidVelocity(0.0f);

    glm::vec3 previous = origin;
    float previousRadius = startRadius;
    Node* landedIn = nullptr;
    glm::vec4 splashArea(-1e30f, -1e30f, 1e30f, 1e30f); // a liquid's container walls keep its splashes in
    glm::vec3 landing = origin;
    bool landed = false;
    const float step = 0.015f;
    for (int i = 1; i <= 160 && !landed; ++i) {
        const float t = step * i;
        const glm::vec3 velocity = startVelocity + glm::vec3(0.0f, -kGravity * t, 0.0f);
        const glm::vec3 p = origin + startVelocity * t + glm::vec3(0.0f, -0.5f * kGravity * t * t, 0.0f);
        const float radius = startRadius * std::sqrt(startSpeed / std::max(glm::length(velocity), 1e-3f));
        // A step can cross several surfaces (a liquid and the table under its glass): the
        // highest one is reached first
        const Target* hit = nullptr;
        float hitTop = -1e30f;
        for (const Target& target : targets) {
            if (p.x < target.lo.x || p.x > target.hi.x || p.z < target.lo.z || p.z > target.hi.z) continue;
            const float top = target.liquid ? std::min(SurfaceHeightAt(*target.node, glm::vec2(p.x, p.z)), target.hi.y) : target.hi.y;
            if (p.y > top || (target.liquid && p.y < target.lo.y - 0.05f) || (target.fluid && p.y < target.lo.y)) continue;
            if (top > hitTop) {
                hit = &target;
                hitTop = top;
            }
        }
        if (hit) {
            // Land on the surface between the last point and this one (simulated fluid: where the
            // stream enters its box, carrying on as particles)
            const float f = glm::clamp((previous.y - hitTop) / std::max(previous.y - p.y, 1e-5f), 0.0f, 1.0f);
            landing = hit->fluid ? p : glm::mix(previous, p, f);
            if (hit->fluid) {
                fluidHit = hit->node;
                fluidVelocity = velocity;
            }
            landedIn = hit->liquid ? hit->node : nullptr;
            if (hit->liquid) splashArea = glm::vec4(hit->lo.x, hit->lo.z, hit->hi.x, hit->hi.z);
            landed = true;
        }
        const glm::vec3 end = landed ? landing : p;
        m_segments.push_back({ previous, end, (previousRadius + radius) * 0.5f, spout.pourColor });
        previous = end;
        previousRadius = radius;
        if (p.y < origin.y - 50.0f) break;
    }
    if (!landed) return;

    // Liquid moves from the source to what it lands in (by volume)
    if (transferLiquid) {
        const float volume = kPi * startRadius * startRadius * startSpeed * dt;
        if (source && source->isLiquid) {
            source->liquidFill = std::max(source->liquidFill - volume / LiquidVolume(*source, scene), 0.0f);
        }
        if (fluidHit) {
            m_fluidFeeds.push_back({ fluidHit->id, landing, fluidVelocity, kPi * startRadius * startRadius * startSpeed });
        }
        if (landedIn) {
            landedIn->liquidFill = std::min(landedIn->liquidFill + volume / LiquidVolume(*landedIn, scene), 1.0f);
        }
    }
    if (fluidHit) return; // the fluid simulation splashes for itself
    // Rings spread from where the stream lands
    if (landedIn) landedIn->liquidRipple = glm::vec4(landing, std::max(landedIn->liquidRipple.w, flow));

    // Splashes where it lands: droplets thrown up and out, falling back into the surface
    const float impactSpeed = std::sqrt(startSpeed * startSpeed + 2.0f * kGravity * std::max(origin.y - landing.y, 0.0f));
    const float splashScale = std::clamp(startRadius / 0.012f, 0.2f, 3.0f);
    const int count = static_cast<int>(flow * 150.0f * dt + Random());
    for (int n = 0; n < count && m_particles.size() < 3000; ++n) {
        const float angle = Random() * 2.0f * kPi;
        const float out = (0.15f + 0.35f * Random()) * std::sqrt(impactSpeed);
        const float rise = (0.4f + 0.8f * Random()) * std::sqrt(impactSpeed) * 0.6f;
        Particle p;
        p.position = landing + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * startRadius;
        p.velocity = glm::vec3(std::cos(angle) * out, rise, std::sin(angle) * out);
        p.age = 0.0f;
        p.life = 0.25f + 0.35f * Random();
        p.size = (0.002f + 0.004f * Random()) * splashScale;
        p.floor = landing.y - 0.002f;
        p.area = splashArea;
        p.color = glm::vec4(glm::vec3(spout.pourColor), std::max(spout.pourColor.a, 0.5f));
        m_particles.push_back(p);
    }
}

void Simulation::Update(Scene& scene, float dt, bool streams, bool transferLiquid) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    for (const auto& node : scene.GetNodes()) {
        if (node && node->isLiquid && node->nodeType == NodeType::Mesh) UpdateSurface(*node, scene, dt);
    }
    if (!streams) return;

    m_segments.clear();
    m_fluidFeeds.clear();
    for (const auto& node : scene.GetNodes()) {
        if (node && node->isPourSpout) RunSpout(scene, *node, dt, transferLiquid);
    }

    // Droplets arc and drop back into the surface they came from
    m_droplets.clear();
    size_t alive = 0;
    for (size_t i = 0; i < m_particles.size(); ++i) {
        Particle p = m_particles[i];
        p.age += dt;
        p.velocity.y -= kGravity * dt;
        p.position += p.velocity * dt;
        if (p.age >= p.life || (p.position.y < p.floor && p.velocity.y < 0.0f)) continue;
        // Inside a container, droplets hit the walls instead of passing through
        if (p.position.x < p.area.x || p.position.x > p.area.z) {
            p.position.x = glm::clamp(p.position.x, p.area.x, p.area.z);
            p.velocity.x *= -0.3f;
        }
        if (p.position.z < p.area.y || p.position.z > p.area.w) {
            p.position.z = glm::clamp(p.position.z, p.area.y, p.area.w);
            p.velocity.z *= -0.3f;
        }
        m_particles[alive++] = p;
        const float fade = 1.0f - p.age / p.life;
        // A short streak along the motion (about 6 ms of travel, at most a few droplet sizes)
        glm::vec3 streak = p.velocity * 0.006f;
        const float streakLength = glm::length(streak);
        if (streakLength > p.size * 3.0f) streak *= p.size * 3.0f / streakLength;
        m_droplets.push_back({ p.position, p.size, glm::vec4(glm::vec3(p.color), p.color.a * fade), streak });
    }
    m_particles.resize(alive);
}

} // namespace Liquids
