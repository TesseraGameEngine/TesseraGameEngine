#include "LightmapBaker.h"
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <unordered_map>

namespace Lightmap {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kChartPadding = 2;      // texels around each chart, so filtering never reads a neighbour
constexpr float kRayOffset = 0.005f;  // ray start above the surface (metres)
constexpr float kRayFar = 100000.0f;

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t Find(uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void Join(uint32_t a, uint32_t b) {
        a = Find(a);
        b = Find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

uint32_t NextPowerOfTwo(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

// ---- Ray tracing: triangles in a bounding volume hierarchy ----

struct Triangle {
    glm::vec3 v0, e1, e2;
    glm::vec3 faceNormal;          // geometric normal, turned to agree with the vertex normals
    glm::vec3 n0, n1, n2;          // vertex normals
    glm::vec2 t0, t1, t2;          // atlas texel coordinates (lightmapped objects)
    uint32_t object{0};
    bool lightmapped{false};
};

struct BvhNode {
    glm::vec3 lo;
    uint32_t first;  // leaf: first triangle; interior: left child (right child follows it)
    glm::vec3 hi;
    uint32_t count;  // triangles in a leaf, 0 for an interior node
};

struct Hit {
    float t{kRayFar};
    float u{0.0f}, v{0.0f};
    uint32_t triangle{0};
};

class Bvh {
public:
    std::vector<Triangle> triangles;

    void Build() {
        nodes.clear();
        if (triangles.empty()) return;
        std::vector<uint32_t> order(triangles.size());
        std::iota(order.begin(), order.end(), 0u);
        std::vector<glm::vec3> centroids(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i) {
            const Triangle& t = triangles[i];
            centroids[i] = t.v0 + (t.e1 + t.e2) / 3.0f;
        }
        nodes.reserve(triangles.size() * 2);
        nodes.push_back(BvhNode{});
        struct Task { uint32_t node, begin, end; };
        std::vector<Task> stack{ { 0, 0, static_cast<uint32_t>(order.size()) } };
        while (!stack.empty()) {
            const Task task = stack.back();
            stack.pop_back();
            glm::vec3 lo(1e30f), hi(-1e30f), clo(1e30f), chi(-1e30f);
            for (uint32_t i = task.begin; i < task.end; ++i) {
                const Triangle& t = triangles[order[i]];
                for (const glm::vec3& p : { t.v0, t.v0 + t.e1, t.v0 + t.e2 }) {
                    lo = glm::min(lo, p);
                    hi = glm::max(hi, p);
                }
                clo = glm::min(clo, centroids[order[i]]);
                chi = glm::max(chi, centroids[order[i]]);
            }
            BvhNode& node = nodes[task.node];
            node.lo = lo;
            node.hi = hi;
            const uint32_t count = task.end - task.begin;
            const glm::vec3 extent = chi - clo;
            if (count <= 4 || glm::max(extent.x, glm::max(extent.y, extent.z)) < 1e-6f) {
                node.first = task.begin;
                node.count = count;
                continue;
            }
            const int axis = (extent.x > extent.y && extent.x > extent.z) ? 0 : (extent.y > extent.z ? 1 : 2);
            const uint32_t mid = task.begin + count / 2;
            std::nth_element(order.begin() + task.begin, order.begin() + mid, order.begin() + task.end,
                             [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });
            const uint32_t left = static_cast<uint32_t>(nodes.size());
            nodes.push_back(BvhNode{});
            nodes.push_back(BvhNode{});
            nodes[task.node].first = left;
            nodes[task.node].count = 0;
            stack.push_back({ left, task.begin, mid });
            stack.push_back({ left + 1, mid, task.end });
        }
        std::vector<Triangle> sorted(triangles.size());
        for (size_t i = 0; i < order.size(); ++i) sorted[i] = triangles[order[i]];
        triangles.swap(sorted);
    }

    // Nearest hit along origin + t * dir for t in (0, maxT)
    bool Intersect(const glm::vec3& origin, const glm::vec3& dir, float maxT, Hit& hit) const {
        return Traverse(origin, dir, maxT, &hit);
    }
    // Any hit before maxT
    bool Occluded(const glm::vec3& origin, const glm::vec3& dir, float maxT) const {
        return Traverse(origin, dir, maxT, nullptr);
    }

private:
    std::vector<BvhNode> nodes;

    bool Traverse(const glm::vec3& origin, const glm::vec3& dir, float maxT, Hit* hit) const {
        if (nodes.empty()) return false;
        glm::vec3 invDir;
        for (int k = 0; k < 3; ++k) {
            const float d = std::abs(dir[k]) < 1e-9f ? (dir[k] < 0.0f ? -1e-9f : 1e-9f) : dir[k];
            invDir[k] = 1.0f / d;
        }
        float best = maxT;
        bool found = false;
        uint32_t stack[64];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const BvhNode& node = nodes[stack[--top]];
            const glm::vec3 t1 = (node.lo - origin) * invDir;
            const glm::vec3 t2 = (node.hi - origin) * invDir;
            const glm::vec3 tNear = glm::min(t1, t2);
            const glm::vec3 tFar = glm::max(t1, t2);
            const float enter = std::max(std::max(tNear.x, tNear.y), std::max(tNear.z, 0.0f));
            const float exit = std::min(std::min(tFar.x, tFar.y), tFar.z);
            if (enter > exit || enter >= best) continue;
            if (node.count == 0) {
                if (top < 62) {
                    stack[top++] = node.first;
                    stack[top++] = node.first + 1;
                }
                continue;
            }
            for (uint32_t i = node.first; i < node.first + node.count; ++i) {
                const Triangle& tri = triangles[i];
                const glm::vec3 p = glm::cross(dir, tri.e2);
                const float det = glm::dot(tri.e1, p);
                if (std::abs(det) < 1e-12f) continue;
                const float invDet = 1.0f / det;
                const glm::vec3 s = origin - tri.v0;
                const float u = glm::dot(s, p) * invDet;
                if (u < 0.0f || u > 1.0f) continue;
                const glm::vec3 q = glm::cross(s, tri.e1);
                const float v = glm::dot(dir, q) * invDet;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float t = glm::dot(tri.e2, q) * invDet;
                if (t <= 1e-5f || t >= best) continue;
                if (!hit) return true;
                best = t;
                found = true;
                hit->t = t;
                hit->u = u;
                hit->v = v;
                hit->triangle = i;
            }
        }
        return found;
    }
};

float RadicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

uint32_t Hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Same as SkySunTransmittance in sky_common.glsl
glm::vec3 SunTransmittance(const glm::vec3& sunDir) {
    auto smooth = [](float e0, float e1, float x) {
        const float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const glm::vec3 tint = glm::mix(glm::vec3(1.0f, 0.38f, 0.12f), glm::vec3(1.0f), smooth(0.0f, 0.5f, sunDir.y));
    return tint * smooth(-0.04f, 0.06f, sunDir.y);
}

} // namespace

// ---- Unwrapping and packing ----

NodeLayout Unwrap(const MeshData& mesh, const glm::vec3& worldScale) {
    NodeLayout layout;
    const size_t triangleCount = mesh.indices.size() / 3;
    const glm::vec3 scale = glm::max(glm::abs(worldScale), glm::vec3(1e-4f));

    // Weld vertices by position, so triangles split by normals or UVs still count as neighbours
    std::vector<uint32_t> welded(mesh.vertices.size());
    {
        std::map<std::array<long long, 3>, uint32_t> byPosition;
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const glm::vec3& p = mesh.vertices[i].pos;
            const std::array<long long, 3> key = { std::llround(p.x * 1e4), std::llround(p.y * 1e4), std::llround(p.z * 1e4) };
            welded[i] = byPosition.emplace(key, static_cast<uint32_t>(byPosition.size())).first->second;
        }
    }

    // Each triangle faces one of six directions (in world space, so the node's scale counts)
    std::vector<uint8_t> direction(triangleCount, 0);
    for (size_t t = 0; t < triangleCount; ++t) {
        const Vertex& a = mesh.vertices[mesh.indices[t * 3]];
        const Vertex& b = mesh.vertices[mesh.indices[t * 3 + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[t * 3 + 2]];
        glm::vec3 n = glm::cross(b.pos - a.pos, c.pos - a.pos);
        if (glm::dot(n, n) < 1e-20f) n = a.normal + b.normal + c.normal;
        n /= scale;
        const glm::vec3 m = glm::abs(n);
        const int axis = (m.x >= m.y && m.x >= m.z) ? 0 : (m.y >= m.z ? 1 : 2);
        direction[t] = static_cast<uint8_t>(axis * 2 + (n[axis] < 0.0f ? 1 : 0));
    }

    // Charts: triangles joined across shared edges when they face the same direction
    UnionFind charts(triangleCount);
    {
        std::unordered_map<uint64_t, std::vector<uint32_t>> edges;
        for (size_t t = 0; t < triangleCount; ++t) {
            for (int e = 0; e < 3; ++e) {
                uint32_t a = welded[mesh.indices[t * 3 + e]];
                uint32_t b = welded[mesh.indices[t * 3 + (e + 1) % 3]];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                auto& list = edges[(static_cast<uint64_t>(a) << 32) | b];
                for (uint32_t other : list) {
                    if (direction[other] == direction[t]) charts.Join(other, static_cast<uint32_t>(t));
                }
                list.push_back(static_cast<uint32_t>(t));
            }
        }
    }

    std::unordered_map<uint32_t, uint32_t> chartOfRoot;
    std::unordered_map<uint64_t, uint32_t> vertexOf; // (chart, source vertex) -> lightmap vertex
    layout.indices.reserve(mesh.indices.size());
    for (size_t t = 0; t < triangleCount; ++t) {
        const uint32_t root = charts.Find(static_cast<uint32_t>(t));
        auto [chartIt, isNew] = chartOfRoot.emplace(root, static_cast<uint32_t>(layout.charts.size()));
        if (isNew) {
            NodeLayout::Chart chart;
            chart.minCorner = glm::vec2(1e30f);
            chart.maxCorner = glm::vec2(-1e30f);
            layout.charts.push_back(chart);
        }
        const uint32_t chart = chartIt->second;
        const int axis = direction[t] / 2;
        const int uAxis = (axis + 1) % 3;
        const int vAxis = (axis + 2) % 3;
        const float flip = (direction[t] & 1) ? -1.0f : 1.0f; // keep charts facing up on both sides
        for (int k = 0; k < 3; ++k) {
            const uint32_t source = mesh.indices[t * 3 + k];
            const uint64_t key = (static_cast<uint64_t>(chart) << 32) | source;
            auto [vertexIt, added] = vertexOf.emplace(key, static_cast<uint32_t>(layout.sourceIndex.size()));
            if (added) {
                const glm::vec3& p = mesh.vertices[source].pos;
                const glm::vec2 coord(p[uAxis] * scale[uAxis] * flip, p[vAxis] * scale[vAxis]);
                layout.sourceIndex.push_back(source);
                layout.chartCoord.push_back(coord);
                layout.vertexChart.push_back(chart);
                layout.charts[chart].minCorner = glm::min(layout.charts[chart].minCorner, coord);
                layout.charts[chart].maxCorner = glm::max(layout.charts[chart].maxCorner, coord);
            }
            layout.indices.push_back(vertexIt->second);
        }
    }
    layout.uv.assign(layout.sourceIndex.size(), glm::vec2(0.0f));
    return layout;
}

uint32_t PackAtlas(std::vector<NodeLayout>& layouts, float& texelsPerMetre, uint32_t maxSize) {
    struct Item { size_t layout; size_t chart; glm::vec2 extent; };
    std::vector<Item> items;
    for (size_t l = 0; l < layouts.size(); ++l) {
        for (size_t c = 0; c < layouts[l].charts.size(); ++c) {
            const auto& chart = layouts[l].charts[c];
            items.push_back({ l, c, glm::max(chart.maxCorner - chart.minCorner, glm::vec2(0.0f)) });
        }
    }
    if (items.empty()) return 0;
    // Tallest first (shelf packing), ties in a fixed order so a bake is repeatable
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.extent.y > b.extent.y; });

    auto texels = [&](float metres) { return std::max(static_cast<int>(std::ceil(metres * texelsPerMetre)), 1) + 2 * kChartPadding; };
    for (int attempt = 0; attempt < 64; ++attempt) {
        double area = 0.0;
        for (const Item& item : items) area += static_cast<double>(texels(item.extent.x)) * texels(item.extent.y);
        uint32_t side = std::max(NextPowerOfTwo(static_cast<uint32_t>(std::sqrt(area * 1.15)) + 1), 64u);
        for (; side <= maxSize; side *= 2) {
            int x = 0, y = 0, shelf = 0;
            bool fits = true;
            for (const Item& item : items) {
                const int w = texels(item.extent.x);
                const int h = texels(item.extent.y);
                if (x + w > static_cast<int>(side)) {
                    x = 0;
                    y += shelf;
                    shelf = 0;
                }
                if (w > static_cast<int>(side) || y + h > static_cast<int>(side)) {
                    fits = false;
                    break;
                }
                layouts[item.layout].charts[item.chart].origin = glm::ivec2(x, y);
                x += w;
                shelf = std::max(shelf, h);
            }
            if (!fits) continue;

            for (NodeLayout& layout : layouts) {
                for (size_t v = 0; v < layout.uv.size(); ++v) {
                    const auto& chart = layout.charts[layout.vertexChart[v]];
                    const glm::vec2 texel = glm::vec2(chart.origin + kChartPadding) + (layout.chartCoord[v] - chart.minCorner) * texelsPerMetre;
                    layout.uv[v] = texel / static_cast<float>(side);
                }
            }
            return side;
        }
        texelsPerMetre *= 0.85f; // doesn't fit at this density: try a coarser one
    }
    return 0;
}

std::vector<Vertex> BuildVertices(const MeshData& source, const NodeLayout& layout) {
    std::vector<Vertex> vertices(layout.sourceIndex.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = source.vertices[layout.sourceIndex[i]];
        vertices[i].lightmapUV = layout.uv[i];
    }
    return vertices;
}

// ---- Baking ----

void Baker::Start(std::vector<BakeObject> objects, BakeSettings settings) {
    Cancel();
    m_objects = std::move(objects);
    m_settings = std::move(settings);
    m_packed.clear();
    m_texelCount = 0;
    m_done = false;
    m_cancel = false;
    m_progress = 0;
    m_progressTotal = 1;
    m_thread = std::thread([this] { Run(); });
}

void Baker::Cancel() {
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
    m_cancel = false;
}

std::string Baker::Status() const {
    std::string stage;
    {
        std::lock_guard<std::mutex> lock(m_stageMutex);
        stage = m_stage;
    }
    const double fraction = static_cast<double>(m_progress.load()) / static_cast<double>(std::max<size_t>(m_progressTotal.load(), 1));
    char text[160];
    std::snprintf(text, sizeof(text), "Lightmaps: %s (%d%%)", stage.c_str(), static_cast<int>(std::min(fraction, 1.0) * 100.0));
    return text;
}

void Baker::Run() {
    auto setStage = [this](const std::string& stage) {
        std::lock_guard<std::mutex> lock(m_stageMutex);
        m_stage = stage;
    };
    setStage("preparing");
    const BakeSettings& settings = m_settings;
    const uint32_t size = settings.atlasSize;
    const size_t texelTotal = static_cast<size_t>(size) * size;

    // Scene triangles
    Bvh bvh;
    for (uint32_t o = 0; o < m_objects.size(); ++o) {
        const BakeObject& object = m_objects[o];
        const bool lightmapped = !object.atlasUV.empty();
        for (size_t i = 0; i + 2 < object.indices.size(); i += 3) {
            const uint32_t a = object.indices[i], b = object.indices[i + 1], c = object.indices[i + 2];
            Triangle tri;
            tri.v0 = object.positions[a];
            tri.e1 = object.positions[b] - tri.v0;
            tri.e2 = object.positions[c] - tri.v0;
            glm::vec3 n = glm::cross(tri.e1, tri.e2);
            if (glm::dot(n, n) < 1e-20f) continue;
            n = glm::normalize(n);
            tri.n0 = object.normals[a];
            tri.n1 = object.normals[b];
            tri.n2 = object.normals[c];
            if (glm::dot(n, tri.n0 + tri.n1 + tri.n2) < 0.0f) n = -n;
            tri.faceNormal = n;
            tri.object = o;
            tri.lightmapped = lightmapped;
            if (lightmapped) {
                tri.t0 = object.atlasUV[a] * static_cast<float>(size);
                tri.t1 = object.atlasUV[b] * static_cast<float>(size);
                tri.t2 = object.atlasUV[c] * static_cast<float>(size);
            }
            bvh.triangles.push_back(tri);
        }
    }
    bvh.Build();

    // Texel samples: the surface point at the centre of every covered texel
    struct Sample { glm::vec3 position, normal, faceNormal; uint32_t object; };
    std::vector<int32_t> sampleOf(texelTotal, -1);
    std::vector<Sample> samples;
    for (const Triangle& tri : bvh.triangles) {
        if (!tri.lightmapped) continue;
        auto edge = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) {
            return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        };
        const float area = edge(tri.t0, tri.t1, tri.t2);
        auto addSample = [&](int x, int y, float b0, float b1, float b2) {
            const size_t texel = static_cast<size_t>(y) * size + x;
            if (sampleOf[texel] >= 0) return;
            Sample s;
            s.position = tri.v0 + tri.e1 * b1 + tri.e2 * b2;
            const glm::vec3 n = tri.n0 * b0 + tri.n1 * b1 + tri.n2 * b2;
            s.normal = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : tri.faceNormal;
            if (glm::dot(s.normal, tri.faceNormal) < 0.0f) s.normal = tri.faceNormal;
            s.faceNormal = tri.faceNormal;
            s.object = tri.object;
            sampleOf[texel] = static_cast<int32_t>(samples.size());
            samples.push_back(s);
        };
        bool covered = false;
        if (std::abs(area) > 1e-9f) {
            const glm::vec2 lo = glm::floor(glm::min(tri.t0, glm::min(tri.t1, tri.t2)));
            const glm::vec2 hi = glm::ceil(glm::max(tri.t0, glm::max(tri.t1, tri.t2)));
            for (int y = std::max(static_cast<int>(lo.y), 0); y < std::min(static_cast<int>(hi.y), static_cast<int>(size)); ++y) {
                for (int x = std::max(static_cast<int>(lo.x), 0); x < std::min(static_cast<int>(hi.x), static_cast<int>(size)); ++x) {
                    const glm::vec2 p(x + 0.5f, y + 0.5f);
                    const float b0 = edge(tri.t1, tri.t2, p) / area;
                    const float b1 = edge(tri.t2, tri.t0, p) / area;
                    const float b2 = 1.0f - b0 - b1;
                    if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f) continue;
                    addSample(x, y, b0, b1, b2);
                    covered = true;
                }
            }
        }
        if (!covered) {
            // Smaller than a texel: its centre still gets a sample, unless the texel is taken
            const glm::vec2 c = (tri.t0 + tri.t1 + tri.t2) / 3.0f;
            const int x = glm::clamp(static_cast<int>(c.x), 0, static_cast<int>(size) - 1);
            const int y = glm::clamp(static_cast<int>(c.y), 0, static_cast<int>(size) - 1);
            addSample(x, y, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
        }
    }
    m_texelCount = samples.size();
    m_progressTotal = samples.size() * static_cast<size_t>(1 + settings.bounces) + 1;

    auto parallelFor = [this](size_t count, const auto& body) {
        const unsigned workers = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
        std::atomic<size_t> next{0};
        std::vector<std::thread> threads;
        for (unsigned w = 0; w < workers; ++w) {
            threads.emplace_back([&] {
                for (;;) {
                    const size_t begin = next.fetch_add(64);
                    if (begin >= count || m_cancel.load()) break;
                    const size_t end = std::min(count, begin + 64);
                    for (size_t i = begin; i < end; ++i) body(i);
                    m_progress += end - begin;
                }
            });
        }
        for (auto& t : threads) t.join();
    };

    auto skyRadiance = [&](const glm::vec3& d) -> glm::vec3 {
        if (settings.skyCube.empty() || settings.skyFaceSize == 0) {
            return d.y >= 0.0f ? settings.flatSky : settings.flatGround;
        }
        static const glm::vec3 kFaceDirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        const glm::vec3 m = glm::abs(d);
        const int axis = (m.x >= m.y && m.x >= m.z) ? 0 : (m.y >= m.z ? 1 : 2);
        const int face = axis * 2 + (d[axis] < 0.0f ? 1 : 0);
        const glm::vec3 f = kFaceDirs[face];
        const glm::vec3 up = std::abs(f.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::vec3 side = glm::normalize(glm::cross(f, up));
        const glm::vec3 faceUp = glm::cross(side, f);
        const float major = std::max(glm::dot(d, f), 1e-6f);
        const float u = glm::dot(d, side) / major;
        const float v = -glm::dot(d, faceUp) / major;
        const int n = static_cast<int>(settings.skyFaceSize);
        const int x = glm::clamp(static_cast<int>((u * 0.5f + 0.5f) * n), 0, n - 1);
        const int y = glm::clamp(static_cast<int>((v * 0.5f + 0.5f) * n), 0, n - 1);
        return settings.skyCube[static_cast<size_t>(face) * n * n + static_cast<size_t>(y) * n + x];
    };

    // Direct light reaching a point (irradiance, before the 1 / pi of a diffuse surface)
    auto directLight = [&](const glm::vec3& origin, const glm::vec3& normal) -> glm::vec3 {
        glm::vec3 total(0.0f);
        for (const LightData& light : settings.lights) {
            const float type = light.position.w;
            glm::vec3 L;
            glm::vec3 radiance = glm::vec3(light.color) * light.color.a;
            float maxT = kRayFar;
            if (type > 0.5f) {
                const glm::vec3 toLight = glm::vec3(light.position) - origin;
                const float dist = glm::length(toLight);
                const float range = std::max(light.spotDirAndRange.w, 0.1f);
                if (dist >= range || dist < 1e-4f) continue;
                L = toLight / dist;
                const float rangeFactor = glm::clamp(1.0f - std::pow(dist / range, 4.0f), 0.0f, 1.0f);
                float attenuation = rangeFactor * rangeFactor / (1.0f + 0.09f * dist + 0.032f * dist * dist);
                if (type > 1.5f) {
                    const float theta = glm::dot(-L, glm::normalize(glm::vec3(light.spotDirAndRange)));
                    const float epsilon = std::max(light.spotAnglesAndShadow.x - light.spotAnglesAndShadow.y, 0.0001f);
                    const float spot = glm::clamp((theta - light.spotAnglesAndShadow.y) / epsilon, 0.0f, 1.0f);
                    attenuation *= spot * spot;
                }
                radiance *= attenuation;
                maxT = dist - 0.01f;
            } else {
                L = glm::normalize(glm::vec3(light.position));
                if (settings.sky && glm::dot(L, settings.skyDirection) > 0.9999f) radiance *= SunTransmittance(L);
            }
            const float NdotL = glm::dot(normal, L);
            if (NdotL <= 0.0f || glm::dot(radiance, glm::vec3(1.0f)) < 1e-5f) continue;
            if (bvh.Occluded(origin, L, maxT)) continue;
            total += radiance * NdotL;
        }
        return total;
    };

    // Grids for looking up a lightmapped surface's light where a ray hits it
    std::vector<glm::vec3> direct(texelTotal, glm::vec3(0.0f));
    std::vector<glm::vec3> indirect(texelTotal, glm::vec3(0.0f));
    std::vector<uint8_t> filled(texelTotal, 0);  // 1 = has a value (own sample or filled in)
    std::vector<uint8_t> invalid(samples.size(), 0); // sample inside other geometry

    // Fills empty texels next to filled ones with their neighbours' average
    auto dilate = [&](std::vector<glm::vec3>& values, std::vector<uint8_t>& mask, int passes) {
        std::vector<uint8_t> next;
        for (int pass = 0; pass < passes; ++pass) {
            next = mask;
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    const size_t i = static_cast<size_t>(y) * size + x;
                    if (mask[i]) continue;
                    glm::vec3 sum(0.0f);
                    int count = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int>(size) || ny >= static_cast<int>(size)) continue;
                            const size_t j = static_cast<size_t>(ny) * size + nx;
                            if (!mask[j]) continue;
                            sum += values[j];
                            ++count;
                        }
                    }
                    if (count > 0) {
                        values[i] = sum / static_cast<float>(count);
                        next[i] = 1;
                    }
                }
            }
            mask.swap(next);
        }
    };
    auto sampleMask = [&]() {
        std::vector<uint8_t> mask(texelTotal, 0);
        for (size_t t = 0; t < texelTotal; ++t) {
            if (sampleOf[t] >= 0 && !invalid[sampleOf[t]]) mask[t] = 1;
        }
        return mask;
    };
    std::vector<size_t> texelOfSample(samples.size());
    for (size_t t = 0; t < texelTotal; ++t) {
        if (sampleOf[t] >= 0) texelOfSample[sampleOf[t]] = t;
    }

    // 1. Direct light at every texel
    setStage("direct light");
    parallelFor(samples.size(), [&](size_t s) {
        const Sample& sample = samples[s];
        direct[texelOfSample[s]] = directLight(sample.position + sample.faceNormal * kRayOffset, sample.normal);
    });
    if (m_cancel) { m_done = true; return; }
    filled = sampleMask();
    dilate(direct, filled, kChartPadding);
    const std::vector<uint8_t> directFilled = filled;

    // Light leaving the surface a ray hits, using the previous bounce's indirect light
    auto hitRadiance = [&](const glm::vec3& origin, const glm::vec3& dir, const std::vector<glm::vec3>& previous,
                           const std::vector<uint8_t>& previousFilled, bool& backface) -> glm::vec3 {
        Hit hit;
        backface = false;
        if (!bvh.Intersect(origin, dir, kRayFar, hit)) return skyRadiance(dir);
        const Triangle& tri = bvh.triangles[hit.triangle];
        if (glm::dot(dir, tri.faceNormal) > 0.0f) {
            backface = true;
            return glm::vec3(0.0f);
        }
        const BakeObject& object = m_objects[tri.object];
        glm::vec3 light(0.0f);
        glm::vec3 bounce(0.0f);
        bool found = false;
        if (tri.lightmapped) {
            const glm::vec2 tc = tri.t0 * (1.0f - hit.u - hit.v) + tri.t1 * hit.u + tri.t2 * hit.v;
            const int x = glm::clamp(static_cast<int>(tc.x), 0, static_cast<int>(size) - 1);
            const int y = glm::clamp(static_cast<int>(tc.y), 0, static_cast<int>(size) - 1);
            const size_t i = static_cast<size_t>(y) * size + x;
            if (directFilled[i]) {
                light = direct[i];
                if (previousFilled[i]) bounce = previous[i];
                found = true;
            }
        }
        if (!found) {
            const glm::vec3 p = origin + dir * hit.t;
            glm::vec3 n = tri.n0 * (1.0f - hit.u - hit.v) + tri.n1 * hit.u + tri.n2 * hit.v;
            n = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : tri.faceNormal;
            light = directLight(p + tri.faceNormal * kRayOffset, n);
        }
        return object.albedo * (light / kPi + settings.giStrength * bounce) + object.emissive;
    };

    // 2. Bounces: each gathers the sky and the light leaving surfaces from the bounce before
    const int samplesPerTexel = std::max(settings.samples, 1);
    std::vector<glm::vec3> previous(texelTotal, glm::vec3(0.0f));
    std::vector<uint8_t> previousFilled(texelTotal, 0);
    for (int bounce = 1; bounce <= settings.bounces; ++bounce) {
        setStage("bounce " + std::to_string(bounce) + " of " + std::to_string(settings.bounces));
        std::fill(indirect.begin(), indirect.end(), glm::vec3(0.0f));
        parallelFor(samples.size(), [&](size_t s) {
            if (invalid[s]) return;
            const Sample& sample = samples[s];
            const glm::vec3 origin = sample.position + sample.faceNormal * kRayOffset;
            // Tangent frame around the shading normal (Duff et al.)
            const glm::vec3& n = sample.normal;
            const float sign = n.z >= 0.0f ? 1.0f : -1.0f;
            const float a = -1.0f / (sign + n.z);
            const float b = n.x * n.y * a;
            const glm::vec3 tangent(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
            const glm::vec3 bitangent(b, sign + n.y * n.y * a, -n.y);
            // Stratified directions, rotated differently at each texel
            const uint32_t seed = Hash(static_cast<uint32_t>(texelOfSample[s]) * 9781u + 1u);
            const float r1 = static_cast<float>(seed & 0xFFFFu) / 65536.0f;
            const float r2 = static_cast<float>(seed >> 16) / 65536.0f;
            glm::vec3 sum(0.0f);
            int backfaces = 0;
            for (int i = 0; i < samplesPerTexel; ++i) {
                const float u1 = std::fmod((i + 0.5f) / samplesPerTexel + r1, 1.0f);
                const float u2 = std::fmod(RadicalInverse(static_cast<uint32_t>(i)) + r2, 1.0f);
                const float radius = std::sqrt(u1);
                const float phi = 2.0f * kPi * u2;
                glm::vec3 dir = tangent * (radius * std::cos(phi)) + bitangent * (radius * std::sin(phi)) +
                                n * std::sqrt(std::max(0.0f, 1.0f - u1));
                const float below = glm::dot(dir, sample.faceNormal);
                if (below < 0.01f) dir = glm::normalize(dir - sample.faceNormal * (2.0f * below - 0.02f));
                bool backface = false;
                sum += hitRadiance(origin, dir, previous, previousFilled, backface);
                if (backface) ++backfaces;
            }
            // Mostly seeing the inside of other objects: the texel is buried (e.g. floor under a wall)
            if (bounce == 1 && backfaces * 4 > samplesPerTexel) invalid[s] = 1;
            // Cosine-weighted average radiance: what the shader multiplies by albedo
            indirect[texelOfSample[s]] = sum / static_cast<float>(samplesPerTexel);
        });
        if (m_cancel) { m_done = true; return; }
        filled = sampleMask();
        dilate(indirect, filled, kChartPadding);
        previous = indirect;
        previousFilled = filled;
    }

    // 3. Smooth the remaining noise across texels of the same surface, then fill the padding
    setStage("finishing");
    std::vector<uint8_t> own = sampleMask();
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed = indirect;
        for (size_t s = 0; s < samples.size(); ++s) {
            if (invalid[s]) continue;
            const size_t t = texelOfSample[s];
            const int x = static_cast<int>(t % size), y = static_cast<int>(t / size);
            glm::vec3 sum = indirect[t] * 2.0f;
            float weight = 2.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(size) || ny >= static_cast<int>(size)) continue;
                    const size_t j = static_cast<size_t>(ny) * size + nx;
                    if (!own[j]) continue;
                    const Sample& other = samples[sampleOf[j]];
                    if (other.object != samples[s].object || glm::dot(other.normal, samples[s].normal) < 0.9f) continue;
                    sum += indirect[j];
                    weight += 1.0f;
                }
            }
            smoothed[t] = sum / weight;
        }
        indirect.swap(smoothed);
    }
    filled = own;
    dilate(indirect, filled, kChartPadding + 1);

    m_packed.assign(texelTotal, 0u);
    for (size_t t = 0; t < texelTotal; ++t) {
        if (filled[t]) m_packed[t] = glm::packF2x11_1x10(glm::max(indirect[t], glm::vec3(0.0f)));
    }
    m_progress = m_progressTotal.load();
    m_done = true;
}

} // namespace Lightmap
