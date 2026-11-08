#include "WorldBuilder.h"
#include "Scene/Scene.h"
#include "Scene/Node.h"
#include "Scene/Nodes/3D/TerrainNode.h"
#include "Utils/SimpleJson.h"
#include <stb_image_write.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Tools {

namespace {

// ----------------------------------------------------------------------------------------------
// World layout
// ----------------------------------------------------------------------------------------------
constexpr float kCell = 128.0f;            // sector size (m)
constexpr int kGrid = 12;                  // 12 x 12 sectors: 1536 m
constexpr float kWorld = kCell * kGrid;
const glm::vec2 kCenter(kWorld * 0.5f, kWorld * 0.5f);
constexpr float kMinH = -24.0f;            // terrain height range (all tiles alike, so edges match)
constexpr float kMaxH = 96.0f;
constexpr uint32_t kDetailRes = 128;       // sector terrain samples per side (~1 m)
constexpr uint32_t kLodRes = 32;           // stand-in tile samples per side (~4 m)
constexpr uint32_t kDetailSplat = 256;
constexpr uint32_t kLodSplat = 64;

// ----------------------------------------------------------------------------------------------
// Noise
// ----------------------------------------------------------------------------------------------
uint32_t g_seed = 7;

uint32_t Hash(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + (salt + g_seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Hash01(int x, int y, uint32_t salt) { return (Hash(x, y, salt) & 0xFFFFFF) / 16777215.0f; }

float ValueNoise(float x, float y, uint32_t salt) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Hash01(xi, yi, salt), b = Hash01(xi + 1, yi, salt);
    const float c = Hash01(xi, yi + 1, salt), d = Hash01(xi + 1, yi + 1, salt);
    return glm::mix(glm::mix(a, b, ux), glm::mix(c, d, ux), uy);
}

float Fbm(float x, float y, int octaves, uint32_t salt) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * ValueNoise(x, y, salt + o * 17);
        norm += amp;
        x = x * 2.03f + 13.7f;
        y = y * 2.03f + 7.3f;
        amp *= 0.5f;
    }
    return sum / norm;
}

float Smooth(float e0, float e1, float x) {
    const float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ----------------------------------------------------------------------------------------------
// The island
// ----------------------------------------------------------------------------------------------
struct Island {
    glm::vec2 campCenter{0.0f};
    glm::vec2 seaward{1.0f, 0.0f};   // from the camp out to sea
    glm::vec2 shore{0.0f};           // where the camp's beach meets the water

    // 0 = open sea .. 1 = inland
    float Land(float x, float z) const {
        const glm::vec2 d = glm::vec2(x, z) - kCenter;
        const float r = glm::length(d * glm::vec2(0.92f, 1.08f));
        const float a = std::atan2(d.y, d.x);
        const float n = Fbm(std::cos(a) * 1.6f + 20.0f, std::sin(a) * 1.6f + 20.0f, 4, 101);
        const float coast = 500.0f + 150.0f * (n - 0.5f) * 2.0f;
        return Smooth(coast + 55.0f, coast - 85.0f, r);
    }
    float Inland(float x, float z) const {
        const glm::vec2 d = glm::vec2(x, z) - kCenter;
        const float r = glm::length(d * glm::vec2(0.92f, 1.08f));
        const float a = std::atan2(d.y, d.x);
        const float n = Fbm(std::cos(a) * 1.6f + 20.0f, std::sin(a) * 1.6f + 20.0f, 4, 101);
        const float coast = 500.0f + 150.0f * (n - 0.5f) * 2.0f;
        return Smooth(coast - 40.0f, coast - 260.0f, r);
    }
    // West coast cliffs (0..1)
    float Cliff(float x, float z) const {
        const glm::vec2 d = glm::normalize(glm::vec2(x, z) - kCenter + glm::vec2(1e-3f));
        const float west = Smooth(0.25f, 0.8f, -d.x);
        return west * Smooth(0.35f, 0.65f, Fbm(x / 160.0f, z / 160.0f, 3, 303));
    }

    float RawHeight(float x, float z) const {
        const float land = Land(x, z);
        const float inland = Inland(x, z);
        const float hills = Fbm(x / 430.0f, z / 430.0f, 5, 202);
        const float ridged = 1.0f - std::abs(2.0f * Fbm(x / 240.0f + 3.1f, z / 240.0f + 9.2f, 4, 404) - 1.0f);
        const float detail = Fbm(x / 35.0f, z / 35.0f, 3, 505) - 0.5f;
        const float hillH = 3.0f + inland * (58.0f * std::pow(hills, 1.5f) + 16.0f * ridged * ridged) + detail * 1.6f;
        // Seabed to land over the coastal band: fully land by land = 0.6 (no low ground behind
        // the beach for the sea to flood)
        float h = glm::mix(-18.0f + 4.0f * Fbm(x / 90.0f, z / 90.0f, 3, 606), hillH, Smooth(0.1f, 0.6f, land));
        // A gentle beach shelf along the shore, rising steadily from the seabed (no sandbars)
        const float beach = Smooth(0.03f, 0.18f, land) * (1.0f - Smooth(0.5f, 0.72f, land));
        const float shelf = glm::mix(-3.0f, 2.2f, Smooth(0.12f, 0.58f, land));
        h = glm::mix(h, shelf, beach * 0.92f * (1.0f - Cliff(x, z)));
        // West: a plateau ending in cliffs at the water
        const float cliff = Cliff(x, z);
        if (cliff > 0.0f) {
            const float plateau = 20.0f + 12.0f * Fbm(x / 120.0f, z / 120.0f, 3, 707);
            const float edge = Smooth(0.30f, 0.46f, land);
            h = glm::mix(h, std::max(h, glm::mix(-12.0f, plateau, edge)), cliff);
        }
        return h;
    }

    float Height(float x, float z) const {
        float h = RawHeight(x, z);
        // The camp: a flat clearing just above the beach
        const float dc = glm::distance(glm::vec2(x, z), campCenter);
        h = glm::mix(h, 3.0f, Smooth(48.0f, 26.0f, dc));
        return glm::clamp(h, kMinH + 0.01f, kMaxH - 0.01f);
    }

    glm::vec3 Normal(float x, float z) const {
        const float e = 0.75f;
        const float hx = Height(x + e, z) - Height(x - e, z);
        const float hz = Height(x, z + e) - Height(x, z - e);
        return glm::normalize(glm::vec3(-hx, 2.0f * e, -hz));
    }
    float SlopeDeg(float x, float z) const {
        return glm::degrees(std::acos(glm::clamp(Normal(x, z).y, -1.0f, 1.0f)));
    }
    // 0..1: where the forest grows (inland, not too steep, a patchy mask)
    float Forest(float x, float z) const {
        const float mask = Smooth(0.42f, 0.6f, Fbm(x / 210.0f + 5.0f, z / 210.0f + 2.0f, 4, 808));
        return mask * Smooth(0.25f, 0.6f, Inland(x, z)) * (1.0f - Smooth(30.0f, 40.0f, SlopeDeg(x, z)));
    }

    // Splat weights: x sand, y grass, z forest floor, w rock
    glm::vec4 Material(float x, float z) const {
        const float h = Height(x, z);
        const float slope = SlopeDeg(x, z);
        const float n = Fbm(x / 9.0f, z / 9.0f, 2, 909) - 0.5f;
        const float rock = glm::max(Smooth(30.0f, 42.0f, slope + n * 8.0f), Cliff(x, z) * Smooth(0.25f, 0.55f, Land(x, z)) * (1.0f - Smooth(0.6f, 0.75f, Land(x, z))));
        const float sand = (1.0f - Smooth(1.3f, 2.5f, h + n * 1.4f)) * (1.0f - rock);
        // Forest floor shows through in patches (needle litter), grass between them
        const float litter = Smooth(0.35f, 0.7f, Fbm(x / 14.0f + 3.0f, z / 14.0f + 8.0f, 3, 919));
        const float forest = Forest(x, z) * (0.35f + 0.65f * litter) * (1.0f - rock) * (1.0f - sand);
        const float grass = glm::max(1.0f - rock - sand - forest, 0.0f);
        glm::vec4 w(sand, grass, forest, rock);
        return w / glm::max(w.x + w.y + w.z + w.w, 1e-4f);
    }

    void PlaceCamp() {
        // On the east coast: walk out from the centre until the land meets the sea
        const float angle = -0.35f;
        seaward = glm::vec2(std::cos(angle), std::sin(angle));
        float r = 200.0f;
        for (; r < 760.0f; r += 1.0f) {
            const glm::vec2 p = kCenter + seaward * r;
            if (RawHeight(p.x, p.y) < 0.3f) break;
        }
        shore = kCenter + seaward * r;
        campCenter = shore - seaward * 34.0f;
    }
};

// ----------------------------------------------------------------------------------------------
// Model catalog (optimized Poly Haven models: <id>/<id>_<res>[_<variant>]_lod<N>.gltf)
// ----------------------------------------------------------------------------------------------
struct ModelVariant {
    std::string lods[3];
    int lodCount{0};
    glm::vec3 bmin{0.0f}, bmax{0.0f};
    float Height() const { return bmax.y - bmin.y; }
};

struct Catalog {
    std::map<std::string, std::vector<ModelVariant>> assets;
    const std::vector<ModelVariant>* Find(const std::string& id) const {
        auto it = assets.find(id);
        return it != assets.end() && !it->second.empty() ? &it->second : nullptr;
    }
};

bool ReadBounds(const std::string& gltf, glm::vec3& mn, glm::vec3& mx) {
    std::ifstream in(gltf, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    JsonValue root = JsonValue::Parse(ss.str());
    if (!root.IsObject() || root["meshes"].arrVal.empty()) return false;
    mn = glm::vec3(1e30f);
    mx = glm::vec3(-1e30f);
    for (const auto& prim : root["meshes"][0]["primitives"].arrVal) {
        const auto& acc = root["accessors"][static_cast<size_t>(prim["attributes"]["POSITION"].AsInt(0))];
        for (int k = 0; k < 3; ++k) {
            mn[k] = std::min(mn[k], acc["min"][k].AsFloat(0.0f));
            mx[k] = std::max(mx[k], acc["max"][k].AsFloat(0.0f));
        }
    }
    return mx.x >= mn.x;
}

Catalog LoadCatalog(const std::string& modelsDir) {
    Catalog cat;
    std::error_code ec;
    for (const auto& assetDir : fs::directory_iterator(modelsDir, ec)) {
        if (!assetDir.is_directory()) continue;
        const std::string id = assetDir.path().filename().string();
        std::vector<ModelVariant> variants;
        for (const auto& f : fs::directory_iterator(assetDir.path(), ec)) {
            const std::string name = f.path().filename().string();
            const std::string tag = "_lod0.gltf";
            if (name.size() <= tag.size() || name.compare(name.size() - tag.size(), tag.size(), tag) != 0) continue;
            const std::string base = (assetDir.path() / name.substr(0, name.size() - tag.size())).generic_string();
            ModelVariant v;
            for (int lod = 0; lod < 3; ++lod) {
                const std::string p = base + "_lod" + std::to_string(lod) + ".gltf";
                if (!fs::exists(p, ec)) break;
                v.lods[lod] = fs::relative(p, fs::current_path(), ec).generic_string();
                if (v.lods[lod].empty()) v.lods[lod] = p;
                v.lodCount = lod + 1;
            }
            if (v.lodCount > 0 && ReadBounds(base + "_lod0.gltf", v.bmin, v.bmax)) variants.push_back(v);
        }
        std::sort(variants.begin(), variants.end(), [](const ModelVariant& a, const ModelVariant& b) { return a.lods[0] < b.lods[0]; });
        if (!variants.empty()) cat.assets[id] = std::move(variants);
    }
    return cat;
}

// ----------------------------------------------------------------------------------------------
// Scattering
// ----------------------------------------------------------------------------------------------
struct Kind {
    const char* asset;
    float weight;
    float minHeight, maxHeight;  // target height (m); 0 = native size times a little jitter
    float lod1, lod2;            // LOD switch distances (m)
    float maxDraw;               // 0 = always drawn
    float radius;                // keeps others this far away (m)
    float sink;                  // pushed into the ground (m)
    bool tilt;                   // leans with the slope
    const char* variantPrefix{nullptr}; // only variants whose file names contain this
};

const Kind kForestTrees[] = {
    { "fir_tree_01",        5.0f, 16.0f, 26.0f, 40.0f, 110.0f, 0.0f, 4.5f, 0.15f, false },
    { "fir_sapling_medium", 3.0f, 6.0f, 10.0f, 30.0f, 80.0f, 450.0f, 3.0f, 0.1f, false },
    { "pine_sapling_small", 1.5f, 2.0f, 4.0f, 18.0f, 45.0f, 200.0f, 1.6f, 0.05f, false },
    { "fir_sapling",        1.5f, 1.5f, 3.2f, 18.0f, 45.0f, 200.0f, 1.4f, 0.05f, false },
    { "dead_tree_trunk",    0.4f, 0.0f, 0.0f, 20.0f, 60.0f, 220.0f, 2.0f, 0.08f, true },
};
const Kind kForestFloor[] = {
    { "fern_02",          5.0f, 0.0f, 0.0f, 12.0f, 30.0f, 80.0f, 0.8f, 0.02f, true },
    { "shrub_02",         2.0f, 0.0f, 0.0f, 15.0f, 40.0f, 110.0f, 1.2f, 0.02f, true },
    { "pine_roots",       1.0f, 0.0f, 0.0f, 12.0f, 30.0f, 70.0f, 1.0f, 0.03f, true },
    { "bark_debris_01",   1.5f, 0.0f, 0.0f, 10.0f, 25.0f, 50.0f, 0.5f, 0.0f, true },
    { "dry_branches_medium_01", 1.5f, 0.0f, 0.0f, 10.0f, 25.0f, 60.0f, 0.6f, 0.0f, true },
    { "tree_stump_01",    0.6f, 0.0f, 0.0f, 15.0f, 40.0f, 120.0f, 1.0f, 0.05f, true },
    { "tree_stump_02",    0.6f, 0.0f, 0.0f, 15.0f, 40.0f, 120.0f, 1.0f, 0.05f, true },
    { "rock_moss_set_01", 1.0f, 0.0f, 0.0f, 25.0f, 70.0f, 220.0f, 2.0f, 0.15f, true },
};
const Kind kMeadow[] = {
    { "tree_small_02",     1.0f, 4.5f, 7.5f, 30.0f, 80.0f, 0.0f, 4.0f, 0.05f, false },
    { "island_tree_01",    0.6f, 4.5f, 7.0f, 30.0f, 80.0f, 0.0f, 4.0f, 0.05f, false },
    { "island_tree_02",    0.6f, 3.5f, 6.0f, 30.0f, 80.0f, 0.0f, 4.0f, 0.05f, false },
    { "shrub_02",          3.0f, 0.0f, 0.0f, 15.0f, 40.0f, 110.0f, 1.2f, 0.02f, true },
    { "shrub_03",          3.0f, 0.0f, 0.0f, 10.0f, 25.0f, 60.0f, 0.5f, 0.0f, true },
    { "wild_rooibos_bush", 3.0f, 0.0f, 0.0f, 12.0f, 30.0f, 70.0f, 0.8f, 0.0f, true },
    { "grass_medium_02",   6.0f, 0.0f, 0.0f, 10.0f, 25.0f, 45.0f, 0.4f, 0.0f, true },
    { "grass_medium_01",   6.0f, 0.0f, 0.0f, 10.0f, 25.0f, 45.0f, 0.4f, 0.0f, true },
    { "dandelion_01",      2.0f, 0.0f, 0.0f, 8.0f, 20.0f, 35.0f, 0.3f, 0.0f, true },
    { "nettle_plant",      2.0f, 0.0f, 0.0f, 8.0f, 20.0f, 35.0f, 0.3f, 0.0f, true },
    { "boulder_01",        0.6f, 0.0f, 0.0f, 25.0f, 70.0f, 260.0f, 2.0f, 0.2f, true },
};
const Kind kCoast[] = {
    { "coast_rocks_05",      3.0f, 0.0f, 0.0f, 30.0f, 90.0f, 450.0f, 4.0f, 0.25f, true },
    { "coast_land_rocks_02", 1.5f, 0.0f, 0.0f, 40.0f, 110.0f, 600.0f, 7.0f, 0.3f, true },
    { "boulder_01",          2.0f, 0.0f, 0.0f, 25.0f, 70.0f, 260.0f, 2.0f, 0.2f, true },
    { "rock_face_01",        1.5f, 0.0f, 0.0f, 30.0f, 80.0f, 400.0f, 3.5f, 0.3f, true },
    { "namaqualand_stones_01", 2.0f, 0.0f, 0.0f, 10.0f, 25.0f, 60.0f, 0.8f, 0.0f, true },
};

struct Placement {
    std::shared_ptr<Node> node;
    float radius;
};

struct SectorBuilder {
    const Island& island;
    const Catalog& catalog;
    Scene& scene;
    std::mt19937 rng;
    std::unordered_map<int64_t, std::vector<glm::vec3>> grid; // xz + radius per 4 m cell
    int placed{0};

    SectorBuilder(const Island& i, const Catalog& c, Scene& s, uint32_t seed) : island(i), catalog(c), scene(s), rng(seed) {}

    float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

    static int64_t Key(int x, int z) { return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(z); }
    bool Free(const glm::vec2& p, float r) {
        const int cx = static_cast<int>(std::floor(p.x / 4.0f)), cz = static_cast<int>(std::floor(p.y / 4.0f));
        const int span = static_cast<int>(std::ceil((r + 5.0f) / 4.0f));
        for (int z = cz - span; z <= cz + span; ++z) {
            for (int x = cx - span; x <= cx + span; ++x) {
                auto it = grid.find(Key(x, z));
                if (it == grid.end()) continue;
                for (const glm::vec3& o : it->second) {
                    if (glm::distance(p, glm::vec2(o.x, o.y)) < r + o.z) return false;
                }
            }
        }
        return true;
    }
    void Mark(const glm::vec2& p, float r) {
        grid[Key(static_cast<int>(std::floor(p.x / 4.0f)), static_cast<int>(std::floor(p.y / 4.0f)))].push_back(glm::vec3(p, r));
    }

    const Kind* Pick(const Kind* kinds, size_t count) {
        float total = 0.0f;
        for (size_t i = 0; i < count; ++i) if (catalog.Find(kinds[i].asset)) total += kinds[i].weight;
        float r = Rand(0.0f, total);
        for (size_t i = 0; i < count; ++i) {
            if (!catalog.Find(kinds[i].asset)) continue;
            r -= kinds[i].weight;
            if (r <= 0.0f) return &kinds[i];
        }
        return nullptr;
    }

    std::shared_ptr<Node> PlaceModel(const std::string& asset, const ModelVariant& v, glm::vec3 pos, float yawDeg, float scale,
                                     float lod1, float lod2, float maxDraw, glm::vec2 tilt = glm::vec2(0.0f)) {
        auto node = scene.AddNode(ShapeType::Custom, asset);
        node->customMeshName = v.lods[0];
        node->position = pos;
        node->rotation = glm::vec3(tilt.x, yawDeg, tilt.y);
        node->scale = glm::vec3(scale);
        node->boundsMin = v.bmin;
        node->boundsMax = v.bmax;
        node->color = glm::vec4(1.0f);
        node->roughness = 1.0f;
        node->metallic = 0.0f;
        node->doubleSided = true;
        node->lightmapStatic = false;
        node->maxDrawDistance = maxDraw;
        node->lodLevels.clear();
        if (v.lodCount > 1) {
            node->enableLOD = true;
            node->lodLevels.push_back(LODLevel{ v.lods[1], lod1, 0.2f, 0 });
            if (v.lodCount > 2) node->lodLevels.push_back(LODLevel{ v.lods[2], lod2, 0.04f, 0 });
        }
        ++placed;
        return node;
    }

    bool Place(const Kind& kind, glm::vec2 p) {
        const auto* variants = catalog.Find(kind.asset);
        if (!variants) return false;
        const ModelVariant* v = nullptr;
        for (int tries = 0; tries < 8 && !v; ++tries) {
            const auto& cand = (*variants)[static_cast<size_t>(Rand(0.0f, static_cast<float>(variants->size()) - 0.001f))];
            if (!kind.variantPrefix || cand.lods[0].find(kind.variantPrefix) != std::string::npos) v = &cand;
        }
        if (!v) return false;
        float scale = Rand(0.85f, 1.2f);
        if (kind.maxHeight > 0.0f && v->Height() > 0.05f) scale = Rand(kind.minHeight, kind.maxHeight) / v->Height();
        const float footprint = kind.radius * std::max(scale, 0.5f);
        if (!Free(p, footprint)) return false;
        glm::vec2 tilt(0.0f);
        if (kind.tilt) {
            const glm::vec3 n = island.Normal(p.x, p.y);
            tilt = glm::vec2(glm::degrees(std::atan2(n.z, n.y)), -glm::degrees(std::atan2(n.x, n.y))) * 0.8f;
        }
        const float h = island.Height(p.x, p.y);
        PlaceModel(kind.asset, *v, glm::vec3(p.x, h - kind.sink * scale, p.y), Rand(0.0f, 360.0f), scale,
                   kind.lod1, kind.lod2, kind.maxDraw, tilt);
        Mark(p, footprint);
        return true;
    }

    // Jittered grid over the sector at `spacing`; each point keeps with probability(p)
    template <typename Prob>
    void Scatter(const glm::vec2& origin, float spacing, const Kind* kinds, size_t count, Prob probability) {
        const int n = static_cast<int>(kCell / spacing);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const glm::vec2 p = origin + glm::vec2((i + Rand(0.1f, 0.9f)) * spacing, (j + Rand(0.1f, 0.9f)) * spacing);
                if (Rand(0.0f, 1.0f) > probability(p)) continue;
                if (const Kind* k = Pick(kinds, count)) Place(*k, p);
            }
        }
    }
};

// ----------------------------------------------------------------------------------------------
// Terrain tiles
// ----------------------------------------------------------------------------------------------
std::string TexturePath(const std::string& assetRoot, const std::string& id) {
    return assetRoot + "/textures/" + id + "/" + id + "_diff_2k.jpg";
}

std::shared_ptr<TerrainNode> MakeTerrain(Scene& scene, const Island& island, const std::string& assetRoot, int cx, int cz,
                                        uint32_t res, uint32_t splatRes, const std::string& name) {
    auto node = std::dynamic_pointer_cast<TerrainNode>(scene.AddTerrainNode(name));
    node->SetResolution(res);
    node->terrainSize = glm::vec2(kCell, kCell);
    node->heightScale = kMaxH - kMinH;
    node->position = glm::vec3((cx + 0.5f) * kCell, kMinH, (cz + 0.5f) * kCell);
    node->heightData.assign(static_cast<size_t>(res) * res, 0.0f);
    const float step = kCell / static_cast<float>(res - 1);
    for (uint32_t j = 0; j < res; ++j) {
        for (uint32_t i = 0; i < res; ++i) {
            const float x = cx * kCell + i * step, z = cz * kCell + j * step;
            node->heightData[static_cast<size_t>(j) * res + i] = (island.Height(x, z) - kMinH) / (kMaxH - kMinH);
        }
    }
    node->isMeshDirty = true;
    node->color = glm::vec4(0.40f, 0.38f, 0.30f, 1.0f);
    node->roughness = 0.9f;
    node->metallic = 0.0f;
    node->lightmapStatic = false;
    node->layers = {
        // Scanned ground textures are earthy; the grass layer is tinted toward green (the
        // instanced grass blades on top do the rest)
        { "Sand",   TexturePath(assetRoot, "coast_sand_01"),     glm::vec3(1.0f), 7.0f, 0.95f },
        { "Grass",  TexturePath(assetRoot, "aerial_grass_rock"), glm::vec3(0.82f, 1.0f, 0.62f), 9.0f, 0.9f },
        { "Forest", TexturePath(assetRoot, "forest_ground_04"),  glm::vec3(0.92f, 0.95f, 0.85f), 7.0f, 0.92f },
        { "Rock",   TexturePath(assetRoot, "rock_face_03"),      glm::vec3(1.0f), 12.0f, 0.8f },
    };
    node->splatResolution = splatRes;
    node->splatData.assign(static_cast<size_t>(splatRes) * splatRes * 4, 0);
    for (uint32_t j = 0; j < splatRes; ++j) {
        for (uint32_t i = 0; i < splatRes; ++i) {
            const float x = cx * kCell + (i + 0.5f) * kCell / splatRes, z = cz * kCell + (j + 0.5f) * kCell / splatRes;
            const glm::vec4 w = island.Material(x, z);
            uint8_t* px = &node->splatData[(static_cast<size_t>(j) * splatRes + i) * 4];
            for (int k = 0; k < 4; ++k) px[k] = static_cast<uint8_t>(glm::clamp(w[k], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    ++node->splatVersion;
    node->foliageTypes.clear();
    node->foliage.clear();
    return node;
}

// Ground-cover grass (the engine's instanced built-in foliage) where the meadow is
void AddGroundCover(TerrainNode& terrain, const Island& island, int cx, int cz, std::mt19937& rng) {
    FoliageType grass;
    grass.name = "Meadow grass";
    grass.builtIn = 0;
    grass.color = glm::vec4(0.26f, 0.38f, 0.13f, 1.0f);
    grass.minScale = 0.7f;
    grass.maxScale = 1.35f;
    grass.castShadows = false;
    grass.drawDistance = 70.0f;
    grass.wind = true;
    FoliageType flowers;
    flowers.name = "Wild flowers";
    flowers.builtIn = 1;
    flowers.castShadows = false;
    flowers.drawDistance = 55.0f;
    terrain.foliageTypes = { grass, flowers };
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float spacing = 1.3f;
    const int n = static_cast<int>(kCell / spacing);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float lx = -kCell * 0.5f + (i + u(rng)) * spacing, lz = -kCell * 0.5f + (j + u(rng)) * spacing;
            const float x = (cx + 0.5f) * kCell + lx, z = (cz + 0.5f) * kCell + lz;
            const glm::vec4 w = island.Material(x, z);
            const float density = w.y * 0.85f + w.z * 0.18f;
            if (u(rng) > density) continue;
            FoliageInstance f;
            f.local = glm::vec2(lx, lz);
            f.rotation = u(rng) * 6.2831853f;
            f.scale = 0.7f + u(rng) * 0.65f;
            f.type = (u(rng) < 0.06f && w.y > 0.6f) ? 1u : 0u;
            terrain.foliage.push_back(f);
        }
    }
    terrain.NotifyFoliageChanged();
}

// ----------------------------------------------------------------------------------------------
// The fishing camp
// ----------------------------------------------------------------------------------------------
void BuildCamp(Scene& scene, const Island& island, const Catalog& catalog, SectorBuilder& sb) {
    const glm::vec2 c = island.campCenter;
    const glm::vec2 out = island.seaward;
    const glm::vec2 side(-out.y, out.x);
    const float facing = glm::degrees(std::atan2(-out.x, -out.y)); // yaw that turns -Z seaward
    auto at = [&](float alongSea, float alongSide) { return c + out * alongSea + side * alongSide; };
    auto first = [&](const char* id, const char* contains = nullptr) -> const ModelVariant* {
        const auto* vs = catalog.Find(id);
        if (!vs) return nullptr;
        for (const auto& v : *vs) if (!contains || v.lods[0].find(contains) != std::string::npos) return &v;
        return &vs->front();
    };
    auto put = [&](const char* id, glm::vec2 p, float yaw, float scale = 1.0f, float lift = 0.0f, const char* contains = nullptr) -> std::shared_ptr<Node> {
        const ModelVariant* v = first(id, contains);
        if (!v) return nullptr;
        auto n = sb.PlaceModel(id, *v, glm::vec3(p.x, island.Height(p.x, p.y) + lift, p.y), yaw, scale, 25.0f, 60.0f, 260.0f);
        sb.Mark(p, 1.5f);
        return n;
    };

    // The pier: from the beach straight out to sea, its deck ~1.6 m above the water
    if (const ModelVariant* pier = first("modular_wooden_pier")) {
        const float length = pier->bmax.z - pier->bmin.z;
        for (int k = 0; k < 2; ++k) {
            const glm::vec2 p = island.shore + out * (length * (k + 0.5f) - 6.0f);
            auto n = sb.PlaceModel("modular_wooden_pier", *pier, glm::vec3(p.x, -1.0f, p.y), facing, 1.0f, 40.0f, 120.0f, 0.0f);
            n->name = "Pier";
            sb.Mark(p, length * 0.5f);
        }
    }
    // An anchored ship offshore, broadside to the camp
    if (const ModelVariant* ship = first("ship_pinnace")) {
        const glm::vec2 p = island.shore + out * 120.0f + side * 25.0f;
        auto n = sb.PlaceModel("ship_pinnace", *ship, glm::vec3(p.x, -0.6f, p.y), facing + 90.0f, 0.85f, 70.0f, 180.0f, 0.0f);
        n->name = "Anchored Ship";
    }
    if (const ModelVariant* buoy = first("ocean_buoy")) {
        const glm::vec2 p = island.shore + out * 70.0f - side * 18.0f;
        sb.PlaceModel("ocean_buoy", *buoy, glm::vec3(p.x, -0.4f, p.y), 0.0f, 1.0f, 30.0f, 80.0f, 400.0f)->name = "Channel Buoy";
    }

    // Camp props
    put("stone_fire_pit", at(0.0f, 0.0f), 0.0f);
    put("wooden_picnic_table", at(-6.0f, 5.0f), facing + 15.0f);
    put("folding_wooden_stool", at(1.8f, 1.7f), 40.0f);
    put("folding_wooden_stool", at(-1.6f, 2.1f), 160.0f);
    put("wooden_barrels_01", at(9.0f, -7.0f), 30.0f, 1.0f, 0.0f, "barrel01");
    put("wooden_barrels_01", at(9.6f, -5.9f), 75.0f, 1.0f, 0.0f, "barrel02");
    put("barrel_03", at(8.2f, -5.2f), 10.0f);
    put("wooden_crate_02", at(10.5f, -3.5f), facing, 1.0f);
    put("wooden_crate_01", at(10.6f, -3.4f), facing + 5.0f, 1.0f, 0.46f);
    put("wooden_bucket_01", at(7.0f, -3.0f), 0.0f, 1.0f, 0.0f, "v0");
    put("tree_stump_01", at(-3.0f, -6.0f), 70.0f);
    put("wooden_axe", at(-3.0f, -6.0f), 20.0f, 1.0f, 0.4f);
    put("lifebuoy", at(14.0f, 3.0f), facing, 1.0f, 0.46f);

    // Light: the fire and lanterns (scripts dim them by day)
    auto light = [&](const std::string& name, glm::vec2 p, float lift, glm::vec3 colour, float intensity, float range) {
        auto l = scene.AddLightNode(LightType::Point, name);
        l->position = glm::vec3(p.x, island.Height(p.x, p.y) + lift, p.y);
        l->lightColor = colour;
        l->lightIntensity = intensity;
        l->lightRange = range;
        l->castShadows = true;
        return l;
    };
    light("Campfire Light", at(0.0f, 0.0f), 0.6f, glm::vec3(1.0f, 0.55f, 0.2f), 6.0f, 12.0f);
    const glm::vec2 tableLantern = at(-6.0f, 5.0f);
    put("wooden_lantern_01", tableLantern, 30.0f, 1.0f, 0.75f);
    light("Lantern Light 1", tableLantern, 0.98f, glm::vec3(1.0f, 0.7f, 0.35f), 2.5f, 8.0f);
    const glm::vec2 crateLantern = at(10.5f, -3.5f);
    put("wooden_lantern_01", crateLantern, 0.0f, 1.0f, 0.82f);
    light("Lantern Light 2", crateLantern, 1.05f, glm::vec3(1.0f, 0.7f, 0.35f), 2.5f, 8.0f);
}

// ----------------------------------------------------------------------------------------------
// Preview image (top down: materials, shading, objects)
// ----------------------------------------------------------------------------------------------
void WritePreview(const std::string& path, const Island& island, const std::vector<glm::vec2>& objects) {
    const int size = 768;
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 3);
    const glm::vec3 sand(0.80f, 0.72f, 0.52f), grass(0.38f, 0.55f, 0.25f), forest(0.18f, 0.32f, 0.16f), rock(0.48f, 0.46f, 0.44f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float wx = (x + 0.5f) * kWorld / size, wz = (y + 0.5f) * kWorld / size;
            const float h = island.Height(wx, wz);
            glm::vec3 c;
            if (h < 0.0f) {
                c = glm::mix(glm::vec3(0.05f, 0.18f, 0.32f), glm::vec3(0.2f, 0.5f, 0.6f), Smooth(-18.0f, 0.0f, h));
            } else {
                const glm::vec4 w = island.Material(wx, wz);
                c = sand * w.x + grass * w.y + forest * w.z + rock * w.w;
                const glm::vec3 n = island.Normal(wx, wz);
                c *= 0.55f + 0.6f * glm::max(glm::dot(n, glm::normalize(glm::vec3(-0.6f, 0.7f, -0.4f))), 0.0f);
            }
            uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 3];
            for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(glm::clamp(c[k], 0.0f, 1.0f) * 255.0f);
        }
    }
    for (const glm::vec2& o : objects) {
        const int x = static_cast<int>(o.x / kWorld * size), y = static_cast<int>(o.y / kWorld * size);
        if (x < 0 || y < 0 || x >= size || y >= size) continue;
        uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 3];
        p[0] = 20; p[1] = 60; p[2] = 15;
    }
    const glm::vec2 camp = island.campCenter / kWorld * static_cast<float>(size);
    for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
        const int x = static_cast<int>(camp.x) + dx, y = static_cast<int>(camp.y) + dy;
        if (x >= 0 && y >= 0 && x < size && y < size) {
            uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 3];
            p[0] = 255; p[1] = 60; p[2] = 20;
        }
    }
    stbi_write_png(path.c_str(), size, size, 3, px.data(), size * 3);
}

void SetupEnvironment(SceneEnvironment& env) {
    env.enableSky = true;
    env.physicalAtmosphere = true;
    env.timeOfDayEnabled = true;
    env.timeOfDay = 16.5f;
    env.dayLengthMinutes = 24.0f;
    env.latitude = 52.0f;
    env.dayOfYear = 200.0f;
    env.advanceCalendar = false;
    env.cloudsEnabled = true;
    env.cloudCoverage = 0.38f;
    env.cloudOpacity = 0.8f;
    env.volumetricClouds = true;
    env.windSpeed = 6.0f;
    env.windDirection = 70.0f;
    env.enableShadows = true;
    env.shadowDistance = 140.0f;
    env.enableGI = true;
    env.giIntensity = 1.0f;
    env.autoExposure = true;
    env.autoExposureMin = 0.25f;
    env.autoExposureMax = 6.0f;
    env.bloomIntensity = 0.22f;
    env.ssaoIntensity = 1.0f;
    env.antiAliasing = 2;
    env.sharpen = 0.3f;
    env.volumetricFogDensity = 0.0035f;
    env.volumetricHeightFalloff = 0.05f;
    env.volumetricScattering = 0.15f;
    env.occlusionCulling = true;
    env.vignette = 0.12f;
}

} // namespace

bool BuildHeadlandWorld(const WorldBuildOptions& options, std::string& error) {
    const auto start = std::chrono::steady_clock::now();
    g_seed = options.seed;
    const std::string modelsDir = options.assetRoot + "/models";
    Catalog catalog = LoadCatalog(modelsDir);
    if (catalog.assets.empty()) {
        error = "no optimized models in " + modelsDir + " (run tools/fetch_assets.py, then Tessera --optimize-models=" + modelsDir + ")";
        return false;
    }
    std::cout << "[WorldBuilder] " << catalog.assets.size() << " model assets" << std::endl;

    Island island;
    island.PlaceCamp();
    const int campCx = static_cast<int>(island.campCenter.x / kCell), campCz = static_cast<int>(island.campCenter.y / kCell);

    std::error_code ec;
    fs::create_directories(fs::path(options.outputDir) / "sectors", ec);
    for (const auto& old : fs::directory_iterator(fs::path(options.outputDir) / "sectors", ec)) fs::remove(old.path(), ec);

    // --- Sectors -------------------------------------------------------------------------------
    JsonValue sectors = JsonValue::Array();
    std::vector<glm::vec2> previewObjects;
    size_t totalObjects = 0;
    for (int cz = 0; cz < kGrid; ++cz) {
        for (int cx = 0; cx < kGrid; ++cx) {
            // Only sectors with land in them get a detailed version
            float maxH = -1e9f;
            for (int j = 0; j <= 8; ++j) for (int i = 0; i <= 8; ++i) {
                maxH = std::max(maxH, island.Height(cx * kCell + i * kCell / 8.0f, cz * kCell + j * kCell / 8.0f));
            }
            if (maxH < -1.5f) continue;

            Scene scene;
            scene.Clear();
            auto terrain = MakeTerrain(scene, island, options.assetRoot, cx, cz, kDetailRes, kDetailSplat, "Terrain " + std::to_string(cx) + "," + std::to_string(cz));
            std::mt19937 rng(options.seed * 7919u + static_cast<uint32_t>(cz * kGrid + cx));
            AddGroundCover(*terrain, island, cx, cz, rng);

            SectorBuilder sb(island, catalog, scene, options.seed * 104729u + static_cast<uint32_t>(cz * kGrid + cx));
            const glm::vec2 origin(cx * kCell, cz * kCell);
            if (cx == campCx && cz == campCz) BuildCamp(scene, island, catalog, sb);
            // Keep the camp clearing open
            auto awayFromCamp = [&](const glm::vec2& p) { return Smooth(30.0f, 55.0f, glm::distance(p, island.campCenter)); };
            auto aboveWater = [&](const glm::vec2& p) { return island.Height(p.x, p.y) > 0.4f ? 1.0f : 0.0f; };
            // Trees, then what grows under them, then meadows, then the shore
            sb.Scatter(origin, 7.0f, kForestTrees, std::size(kForestTrees), [&](const glm::vec2& p) {
                return island.Forest(p.x, p.y) * 0.95f * aboveWater(p) * awayFromCamp(p);
            });
            sb.Scatter(origin, 4.0f, kForestFloor, std::size(kForestFloor), [&](const glm::vec2& p) {
                return island.Forest(p.x, p.y) * 0.45f * aboveWater(p) * awayFromCamp(p);
            });
            sb.Scatter(origin, 5.0f, kMeadow, std::size(kMeadow), [&](const glm::vec2& p) {
                const glm::vec4 w = island.Material(p.x, p.y);
                return w.y * 0.32f * aboveWater(p) * (0.4f + 0.6f * awayFromCamp(p));
            });
            sb.Scatter(origin, 7.0f, kCoast, std::size(kCoast), [&](const glm::vec2& p) {
                const float h = island.Height(p.x, p.y);
                const float shore = Smooth(-3.0f, 0.0f, h) * (1.0f - Smooth(2.5f, 6.0f, h));
                return (shore * 0.45f + island.Material(p.x, p.y).w * 0.08f) * awayFromCamp(p);
            });
            // Big formations: rock shelves on the shore, cliff faces along the west
            for (int k = 0; k < 2; ++k) {
                const glm::vec2 p = origin + glm::vec2(sb.Rand(10.0f, kCell - 10.0f), sb.Rand(10.0f, kCell - 10.0f));
                const float h = island.Height(p.x, p.y);
                if (h > -2.5f && h < 1.0f && island.Cliff(p.x, p.y) < 0.2f && sb.Rand(0.0f, 1.0f) < 0.35f && sb.Free(p, 25.0f)) {
                    const char* id = sb.Rand(0.0f, 1.0f) < 0.5f ? "coast_rocks_01" : "coast_rocks_02";
                    if (const auto* vs = catalog.Find(id)) {
                        sb.PlaceModel(id, vs->front(), glm::vec3(p.x, h - 0.8f, p.y), sb.Rand(0.0f, 360.0f), sb.Rand(0.45f, 0.7f), 60.0f, 160.0f, 0.0f);
                        sb.Mark(p, 22.0f);
                    }
                }
            }
            for (int k = 0; k < 6; ++k) {
                const glm::vec2 p = origin + glm::vec2(sb.Rand(5.0f, kCell - 5.0f), sb.Rand(5.0f, kCell - 5.0f));
                const float h = island.Height(p.x, p.y);
                if (island.Cliff(p.x, p.y) > 0.5f && h > -6.0f && h < 6.0f && sb.Free(p, 18.0f)) {
                    const char* id = sb.Rand(0.0f, 1.0f) < 0.5f ? "coastal_cliff_01" : "coastal_cliff_02";
                    if (const auto* vs = catalog.Find(id)) {
                        // Long axis along the coast: perpendicular to the slope's fall line
                        const glm::vec3 n = island.Normal(p.x, p.y);
                        const float yaw = glm::degrees(std::atan2(n.x, n.z));
                        const float s = sb.Rand(1.3f, 1.9f);
                        sb.PlaceModel(id, vs->front(), glm::vec3(p.x, h - 3.0f, p.y), yaw, s, 70.0f, 180.0f, 0.0f);
                        sb.Mark(p, 30.0f);
                    }
                }
            }

            for (const auto& n : scene.GetNodes()) {
                if (n->nodeType == NodeType::Mesh && n->maxDrawDistance == 0.0f) previewObjects.emplace_back(n->position.x, n->position.z);
            }
            totalObjects += sb.placed;

            const std::string rel = "sectors/sector_" + std::to_string(cx) + "_" + std::to_string(cz) + ".scene";
            const std::string file = (fs::path(options.outputDir) / rel).string();
            if (!scene.SaveToFile(file)) {
                error = "cannot write " + file;
                return false;
            }
            JsonValue s = JsonValue::Object();
            s["x"] = cx;
            s["z"] = cz;
            s["file"] = rel;
            s["lodNode"] = "LOD_" + std::to_string(cx) + "_" + std::to_string(cz);
            s["bytes"] = static_cast<double>(fs::file_size(file, ec));
            sectors.arrVal.push_back(s);
        }
    }

    // --- Base scene ----------------------------------------------------------------------------
    Scene base;
    base.Clear();
    SetupEnvironment(base.GetEnvironment());
    auto sun = base.AddLightNode(LightType::Directional, "Sun");
    sun->position = glm::vec3(kCenter.x, 200.0f, kCenter.y);
    sun->rotation = glm::vec3(-35.0f, 120.0f, 0.0f);
    sun->lightColor = glm::vec3(1.0f, 0.95f, 0.88f);
    sun->lightIntensity = 3.2f;
    sun->castShadows = true;

    auto ocean = base.AddNode(ShapeType::Plane, "Ocean");
    ocean->isOcean = true;
    ocean->position = glm::vec3(kCenter.x, 0.0f, kCenter.y);
    ocean->oceanSize = glm::vec2(8000.0f, 8000.0f);
    ocean->waveHeight = 0.9f;
    ocean->waveLength = 28.0f;
    ocean->waveDirection = 250.0f;
    ocean->waveChoppiness = 0.55f;
    ocean->whitecaps = 0.3f;
    ocean->waterClarity = 7.0f;
    ocean->shoreFoamWidth = 1.6f;
    ocean->lightmapStatic = false;

    for (int cz = 0; cz < kGrid; ++cz) {
        for (int cx = 0; cx < kGrid; ++cx) {
            auto lod = MakeTerrain(base, island, options.assetRoot, cx, cz, kLodRes, kLodSplat,
                                   "LOD_" + std::to_string(cx) + "_" + std::to_string(cz));
            // A touch lower, so the detailed tile next to it always covers the seam
            lod->position.y -= 0.15f;
        }
    }

    // The player: at the camp, looking out to sea
    const glm::vec2 startXZ = island.campCenter - island.seaward * 12.0f + glm::vec2(-island.seaward.y, island.seaward.x) * 4.0f;
    auto cam = base.AddCameraNode("Player");
    cam->position = glm::vec3(startXZ.x, island.Height(startXZ.x, startXZ.y) + 1.7f, startXZ.y);
    cam->rotation = glm::vec3(-4.0f, glm::degrees(std::atan2(-island.seaward.x, -island.seaward.y)), 0.0f);
    cam->cameraFov = 70.0f;
    cam->cameraNear = 0.1f;
    cam->cameraFar = 4000.0f;
    cam->scriptPath = "scripts/headland/Player.lua";
    base.SetActiveCamera(cam->id);

    // Viewpoints (empty nodes): the player's number keys and --start-at=<name> jump to them
    {
        auto yawToward = [](glm::vec2 d) { return glm::degrees(std::atan2(-d.x, -d.y)); };
        auto addView = [&](const std::string& name, glm::vec3 pos, float yaw, float pitch) {
            auto v = base.AddEmptyNode(name);
            v->position = pos;
            v->rotation = glm::vec3(pitch, yaw, 0.0f);
        };
        addView("View: Camp", cam->position, cam->rotation.y, cam->rotation.x);
        const glm::vec2 pierEnd = island.shore + island.seaward * 30.0f;
        addView("View: Pier", glm::vec3(pierEnd.x, 3.4f, pierEnd.y), yawToward(-island.seaward), -5.0f);
        // Deepest forest, highest hill and a cliff edge, found on a coarse grid
        glm::vec2 forest(kCenter), hill(kCenter), cliff(kCenter);
        float bestForest = -1.0f, bestHill = -1e9f, bestCliff = -1.0f;
        for (float z = 0.0f; z < kWorld; z += 8.0f) {
            for (float x = 0.0f; x < kWorld; x += 8.0f) {
                const float h = island.Height(x, z);
                const float f = island.Forest(x, z) * island.Inland(x, z) * (h > 3.0f ? 1.0f : 0.0f);
                if (f > bestForest + 0.02f) { bestForest = f; forest = glm::vec2(x, z); }
                if (h > bestHill) { bestHill = h; hill = glm::vec2(x, z); }
                // On the plateau, 15-25 m back from where the cliff drops to the sea
                const float c = island.Cliff(x, z) * (h > 12.0f ? 1.0f : 0.0f) * (island.Height(x - 20.0f, z) < 2.0f ? 1.0f : 0.0f);
                if (c > bestCliff) { bestCliff = c; cliff = glm::vec2(x, z); }
            }
        }
        addView("View: Forest", glm::vec3(forest.x, island.Height(forest.x, forest.y) + 1.7f, forest.y), 35.0f, 4.0f);
        addView("View: Hilltop", glm::vec3(hill.x, bestHill + 2.0f, hill.y), yawToward(island.campCenter - hill), -6.0f);
        // Out at sea, looking back at the cliffs (in flight: well above the water)
        const glm::vec2 cliffView = cliff + glm::vec2(-140.0f, 70.0f);
        addView("View: Cliffs", glm::vec3(cliffView.x, 34.0f, cliffView.y), yawToward(cliff - cliffView), -9.0f);
        const glm::vec3 aerial(kCenter.x + 380.0f, 230.0f, kCenter.y + 520.0f);
        addView("View: Overview", aerial, yawToward(kCenter - glm::vec2(aerial.x, aerial.z)), -22.0f);
    }

    // HUD (scripts/headland/Hud.lua fills it in) and the world director (time, weather, lights,
    // ray tracing switches)
    {
        auto place = [](const std::shared_ptr<Node>& n, UIAnchor anchor, glm::vec2 pos, glm::vec2 size) {
            n->uiAnchor = anchor;
            n->uiPosition = pos;
            n->uiSize = size;
        };
        auto canvas = base.AddUICanvasNode("HUD");
        canvas->scriptPath = "scripts/headland/Hud.lua";
        auto panel = base.AddUIPanelNode("HUD Panel", canvas->id);
        place(panel, UIAnchor::TopLeft, glm::vec2(16.0f, 16.0f), glm::vec2(520.0f, 112.0f));
        panel->uiColor = glm::vec4(0.03f, 0.04f, 0.05f, 0.55f);
        panel->uiBorderThickness = 0.0f;
        panel->uiRounding = 6.0f;
        auto title = base.AddUITextNode("HEADLAND", "HUD Title", panel->id);
        place(title, UIAnchor::TopLeft, glm::vec2(14.0f, 8.0f), glm::vec2(490.0f, 24.0f));
        title->uiFontSize = 20.0f;
        title->uiTextColor = glm::vec4(1.0f, 0.9f, 0.72f, 1.0f);
        auto stats = base.AddUITextNode("", "HUD Stats", panel->id);
        place(stats, UIAnchor::TopLeft, glm::vec2(14.0f, 36.0f), glm::vec2(490.0f, 70.0f));
        stats->uiFontSize = 15.0f;
        stats->uiTextColor = glm::vec4(0.9f, 0.93f, 0.97f, 1.0f);
        auto compass = base.AddUITextNode("", "HUD Compass", canvas->id);
        place(compass, UIAnchor::TopCenter, glm::vec2(0.0f, 16.0f), glm::vec2(260.0f, 28.0f));
        compass->uiFontSize = 18.0f;
        compass->uiTextColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
        auto help = base.AddUITextNode("WASD move  |  Shift sprint  |  hold RMB look  |  F fly  |  1-6 viewpoints  |  "
                                       "F1 RT shadows  |  F2 RT reflections  |  F3 stats  |  F4 weather  |  F5 +3 hours",
                                       "HUD Help", canvas->id);
        place(help, UIAnchor::BottomLeft, glm::vec2(16.0f, -14.0f), glm::vec2(1200.0f, 22.0f));
        help->uiFontSize = 14.0f;
        help->uiTextColor = glm::vec4(0.88f, 0.9f, 0.94f, 0.85f);

        auto director = base.AddEmptyNode("World Director");
        director->scriptPath = "scripts/headland/WorldDirector.lua";
    }

    const std::string baseFile = (fs::path(options.outputDir) / "base.scene").string();
    if (!base.SaveToFile(baseFile)) {
        error = "cannot write " + baseFile;
        return false;
    }

    // --- Manifest ------------------------------------------------------------------------------
    JsonValue world = JsonValue::Object();
    world["format"] = "tessera-world-1";
    world["name"] = "Headland";
    world["cellSize"] = kCell;
    world["baseScene"] = "base.scene";
    world["loadRadius"] = 420.0f;
    world["unloadRadius"] = 540.0f;
    world["sectors"] = sectors;
    std::ofstream((fs::path(options.outputDir) / "headland.world").string(), std::ios::binary) << world.Serialize(2);

    if (!options.previewPng.empty()) WritePreview(options.previewPng, island, previewObjects);

    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[WorldBuilder] Headland: " << sectors.arrVal.size() << " land sectors, " << totalObjects
              << " placed models, camp in sector " << campCx << "," << campCz << " (" << static_cast<int>(secs * 10) / 10.0 << " s)" << std::endl;
    return true;
}

int RunWorldBuilderCommandLine(int argc, char* argv[], bool& handled) {
    handled = false;
    WorldBuildOptions options;
    bool build = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--build-world=", 0) == 0) { options.outputDir = arg.substr(14); build = true; }
        else if (arg.rfind("--world-seed=", 0) == 0) options.seed = static_cast<unsigned>(std::stoul(arg.substr(13)));
        else if (arg.rfind("--world-preview=", 0) == 0) options.previewPng = arg.substr(16);
        else if (arg.rfind("--world-assets=", 0) == 0) options.assetRoot = arg.substr(15);
    }
    if (!build) return 0;
    handled = true;
    std::string error;
    if (!BuildHeadlandWorld(options, error)) {
        std::cerr << "[WorldBuilder] " << error << std::endl;
        return 1;
    }
    return 0;
}

} // namespace Tools
