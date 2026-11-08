#include "ModelOptimizer.h"
#include "Scene/GltfLoader.h"
#include "Scene/MeshSimplifier.h"
#include "Scene/ObjLoader.h"
#include "Utils/SimpleJson.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <set>
#include <cctype>

namespace fs = std::filesystem;

namespace Tools {

namespace {

// One part of the model: a submesh's own vertices and indices, and the glTF material it uses
struct Part {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int material{-1};
    bool cards{false};
};

uint32_t Tris(const Part& p) { return static_cast<uint32_t>(p.indices.size() / 3); }

Part ExtractPart(const MeshData& mesh, const SubmeshSection& sub) {
    Part part;
    std::unordered_map<uint32_t, uint32_t> remap;
    const size_t end = std::min<size_t>(static_cast<size_t>(sub.firstIndex) + sub.indexCount, mesh.indices.size());
    for (size_t i = sub.firstIndex; i < end; ++i) {
        const uint32_t src = mesh.indices[i];
        if (src >= mesh.vertices.size()) continue;
        auto it = remap.find(src);
        if (it == remap.end()) {
            it = remap.emplace(src, static_cast<uint32_t>(part.vertices.size())).first;
            part.vertices.push_back(mesh.vertices[src]);
        }
        part.indices.push_back(it->second);
    }
    part.indices.resize(part.indices.size() / 3 * 3);
    part.cards = sub.alphaCutoff > 0.0f;
    return part;
}

// Keeps a deterministic share `keep` of the part's cards (connected pieces) and enlarges each
// kept card about its centre so the canopy keeps roughly the same coverage
Part ThinCards(const Part& in, float keep) {
    if (keep >= 0.999f) return in;
    // Union-find over vertices joined by triangles
    std::vector<uint32_t> parent(in.vertices.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (size_t t = 0; t + 2 < in.indices.size(); t += 3) {
        const uint32_t a = find(in.indices[t]), b = find(in.indices[t + 1]), c = find(in.indices[t + 2]);
        parent[b] = a;
        parent[c] = a;
    }
    // Cards in a stable order; keep every card whose hash falls under `keep`
    std::unordered_map<uint32_t, uint32_t> cardOf;
    std::vector<glm::vec3> centre;
    std::vector<uint32_t> count;
    for (uint32_t v = 0; v < in.vertices.size(); ++v) {
        const uint32_t root = find(v);
        auto it = cardOf.find(root);
        if (it == cardOf.end()) {
            it = cardOf.emplace(root, static_cast<uint32_t>(centre.size())).first;
            centre.emplace_back(0.0f);
            count.push_back(0);
        }
        centre[it->second] += in.vertices[v].pos;
        count[it->second]++;
    }
    for (size_t c = 0; c < centre.size(); ++c) centre[c] /= static_cast<float>(std::max(count[c], 1u));
    auto kept = [&](uint32_t card) {
        uint32_t h = card * 2654435761u;
        h ^= h >> 16;
        return (h & 0xFFFF) / 65535.0f < keep;
    };
    // Area-preserving growth: kept cards cover what the dropped ones did (capped: huge cards
    // turn into visible blobs)
    const float grow = std::min(1.0f / std::sqrt(std::max(keep, 1e-4f)), 3.5f);

    Part out;
    out.material = in.material;
    out.cards = true;
    std::unordered_map<uint32_t, uint32_t> remap;
    for (size_t t = 0; t + 2 < in.indices.size(); t += 3) {
        const uint32_t card = cardOf[find(in.indices[t])];
        if (!kept(card)) continue;
        for (int k = 0; k < 3; ++k) {
            const uint32_t src = in.indices[t + k];
            auto it = remap.find(src);
            if (it == remap.end()) {
                Vertex v = in.vertices[src];
                v.pos = centre[card] + (v.pos - centre[card]) * grow;
                it = remap.emplace(src, static_cast<uint32_t>(out.vertices.size())).first;
                out.vertices.push_back(v);
            }
            out.indices.push_back(it->second);
        }
    }
    if (out.indices.empty() && !in.indices.empty()) {
        // Never lose a whole part: keep its first card
        return ThinCards(in, std::min(1.0f, keep * 4.0f + 0.01f));
    }
    return out;
}

Part Decimate(const Part& in, uint32_t targetTris) {
    if (Tris(in) <= targetTris) return in;
    MeshData md;
    md.vertices = in.vertices;
    md.indices = in.indices;
    MeshData simplified = Geometry::MeshSimplifier::SimplifyToTriangleCount(md, std::max<uint32_t>(targetTris, 64u));
    Part out;
    out.vertices = std::move(simplified.vertices);
    out.indices = std::move(simplified.indices);
    out.material = in.material;
    out.cards = false;
    return out;
}

// The LOD: solid parts decimated and cards thinned in proportion, `target` triangles in all
std::vector<Part> BuildLod(const std::vector<Part>& parts, uint32_t target) {
    uint32_t total = 0;
    for (const auto& p : parts) total += Tris(p);
    if (total <= target) return parts;
    const float ratio = static_cast<float>(target) / static_cast<float>(total);
    std::vector<Part> lod;
    for (const auto& p : parts) {
        if (p.cards) {
            lod.push_back(ThinCards(p, ratio));
        } else {
            // Small solid parts keep a minimum so they don't vanish into a few triangles
            const uint32_t partTarget = std::max<uint32_t>(static_cast<uint32_t>(Tris(p) * ratio), std::min<uint32_t>(Tris(p), 200u));
            lod.push_back(Decimate(p, partTarget));
        }
    }
    return lod;
}

void Align4(std::vector<uint8_t>& bin) {
    while (bin.size() % 4) bin.push_back(0);
}

template <typename T>
size_t Append(std::vector<uint8_t>& bin, const T* data, size_t count) {
    Align4(bin);
    const size_t offset = bin.size();
    bin.resize(offset + count * sizeof(T));
    std::memcpy(bin.data() + offset, data, count * sizeof(T));
    return offset;
}

void StripTextureTransforms(JsonValue& material) {
    auto strip = [](JsonValue& texInfo) {
        if (texInfo.IsObject() && texInfo.HasKey("extensions")) {
            texInfo["extensions"].objVal.erase("KHR_texture_transform");
            if (texInfo["extensions"].objVal.empty()) texInfo.objVal.erase("extensions");
        }
    };
    if (material.HasKey("pbrMetallicRoughness")) {
        auto& pbr = material["pbrMetallicRoughness"];
        if (pbr.HasKey("baseColorTexture")) strip(pbr["baseColorTexture"]);
        if (pbr.HasKey("metallicRoughnessTexture")) strip(pbr["metallicRoughnessTexture"]);
    }
    for (const char* key : { "normalTexture", "occlusionTexture", "emissiveTexture" }) {
        if (material.HasKey(key)) strip(material[key]);
    }
}

bool WriteGltf(const JsonValue& source, const std::vector<Part>& parts, const std::string& outGltf, std::string& error) {
    JsonValue root = source;
    for (const char* key : { "nodes", "scenes", "scene", "meshes", "accessors", "bufferViews", "buffers", "skins", "animations" }) {
        root.objVal.erase(key);
    }
    if (root.HasKey("materials")) {
        for (auto& m : root["materials"].arrVal) StripTextureTransforms(m);
    }
    if (root.HasKey("extensionsUsed")) {
        auto& used = root["extensionsUsed"].arrVal;
        used.erase(std::remove_if(used.begin(), used.end(), [](const JsonValue& v) { return v.AsString() == "KHR_texture_transform"; }), used.end());
    }
    if (root.HasKey("extensionsRequired")) {
        auto& req = root["extensionsRequired"].arrVal;
        req.erase(std::remove_if(req.begin(), req.end(), [](const JsonValue& v) { return v.AsString() == "KHR_texture_transform"; }), req.end());
    }
    JsonValue extras = JsonValue::Object();
    extras["tesseraKeepScale"] = true;
    extras["generator"] = "Tessera ModelOptimizer";
    root["asset"]["extras"] = extras;

    std::vector<uint8_t> bin;
    JsonValue accessors = JsonValue::Array();
    JsonValue views = JsonValue::Array();
    JsonValue primitives = JsonValue::Array();
    auto addView = [&](size_t offset, size_t length, int target) {
        JsonValue v = JsonValue::Object();
        v["buffer"] = 0;
        v["byteOffset"] = static_cast<double>(offset);
        v["byteLength"] = static_cast<double>(length);
        v["target"] = target;
        views.arrVal.push_back(v);
        return static_cast<int>(views.arrVal.size() - 1);
    };
    auto addAccessor = [&](int view, int componentType, size_t count, const char* type) {
        JsonValue a = JsonValue::Object();
        a["bufferView"] = view;
        a["componentType"] = componentType;
        a["count"] = static_cast<double>(count);
        a["type"] = type;
        accessors.arrVal.push_back(a);
        return static_cast<int>(accessors.arrVal.size() - 1);
    };

    for (const auto& part : parts) {
        if (part.indices.empty()) continue;
        std::vector<float> pos, nrm, uv;
        pos.reserve(part.vertices.size() * 3);
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (const auto& v : part.vertices) {
            pos.insert(pos.end(), { v.pos.x, v.pos.y, v.pos.z });
            const glm::vec3 n = glm::length(v.normal) > 1e-6f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
            nrm.insert(nrm.end(), { n.x, n.y, n.z });
            uv.insert(uv.end(), { v.texCoord.x, v.texCoord.y });
            mn = glm::min(mn, v.pos);
            mx = glm::max(mx, v.pos);
        }
        const int posView = addView(Append(bin, pos.data(), pos.size()), pos.size() * 4, 34962);
        const int nrmView = addView(Append(bin, nrm.data(), nrm.size()), nrm.size() * 4, 34962);
        const int uvView = addView(Append(bin, uv.data(), uv.size()), uv.size() * 4, 34962);
        const int idxView = addView(Append(bin, part.indices.data(), part.indices.size()), part.indices.size() * 4, 34963);

        const int posAcc = addAccessor(posView, 5126, part.vertices.size(), "VEC3");
        JsonValue mnArr = JsonValue::Array(), mxArr = JsonValue::Array();
        for (int k = 0; k < 3; ++k) { mnArr.arrVal.push_back(mn[k]); mxArr.arrVal.push_back(mx[k]); }
        accessors.arrVal[posAcc]["min"] = mnArr;
        accessors.arrVal[posAcc]["max"] = mxArr;
        const int nrmAcc = addAccessor(nrmView, 5126, part.vertices.size(), "VEC3");
        const int uvAcc = addAccessor(uvView, 5126, part.vertices.size(), "VEC2");
        const int idxAcc = addAccessor(idxView, 5125, part.indices.size(), "SCALAR");

        JsonValue prim = JsonValue::Object();
        JsonValue attrs = JsonValue::Object();
        attrs["POSITION"] = posAcc;
        attrs["NORMAL"] = nrmAcc;
        attrs["TEXCOORD_0"] = uvAcc;
        prim["attributes"] = attrs;
        prim["indices"] = idxAcc;
        if (part.material >= 0) prim["material"] = part.material;
        primitives.arrVal.push_back(prim);
    }
    if (primitives.arrVal.empty()) {
        error = "nothing to write";
        return false;
    }

    const fs::path gltfPath(outGltf);
    const std::string binName = gltfPath.stem().string() + ".bin";
    JsonValue buffer = JsonValue::Object();
    buffer["uri"] = binName;
    buffer["byteLength"] = static_cast<double>(bin.size());
    JsonValue buffers = JsonValue::Array();
    buffers.arrVal.push_back(buffer);

    JsonValue mesh = JsonValue::Object();
    mesh["name"] = gltfPath.stem().string();
    mesh["primitives"] = primitives;
    JsonValue meshes = JsonValue::Array();
    meshes.arrVal.push_back(mesh);
    JsonValue node = JsonValue::Object();
    node["mesh"] = 0;
    node["name"] = gltfPath.stem().string();
    JsonValue nodes = JsonValue::Array();
    nodes.arrVal.push_back(node);
    JsonValue scene = JsonValue::Object();
    JsonValue sceneNodes = JsonValue::Array();
    sceneNodes.arrVal.push_back(0);
    scene["nodes"] = sceneNodes;
    JsonValue scenes = JsonValue::Array();
    scenes.arrVal.push_back(scene);

    root["buffers"] = buffers;
    root["bufferViews"] = views;
    root["accessors"] = accessors;
    root["meshes"] = meshes;
    root["nodes"] = nodes;
    root["scenes"] = scenes;
    root["scene"] = 0;

    std::ofstream binOut(gltfPath.parent_path() / binName, std::ios::binary);
    if (!binOut) { error = "cannot write " + binName; return false; }
    binOut.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    std::ofstream jsonOut(outGltf, std::ios::binary);
    if (!jsonOut) { error = "cannot write " + outGltf; return false; }
    jsonOut << root.Serialize(1);
    return true;
}

} // namespace

std::string OptimizedLodPath(const std::string& gltfPath, int lod) {
    const fs::path p(gltfPath);
    return (p.parent_path() / (p.stem().string() + "_lod" + std::to_string(lod) + ".gltf")).string();
}

namespace {

// Optimizes one loaded model: LOD files <outBase>_lod<N>.gltf
bool OptimizeLoaded(const JsonValue& source, const LoadedMesh& mesh, const std::string& outBase,
                    const std::vector<uint32_t>& lodTriangleTargets, ModelOptimizeResult& result) {

    // Material index of each submesh (the loader names them after the glTF materials)
    std::map<std::string, int> materialIndex;
    if (source.HasKey("materials")) {
        for (size_t i = 0; i < source["materials"].arrVal.size(); ++i) {
            const auto& m = source["materials"][i];
            materialIndex[m["name"].AsString("glTF_Material_" + std::to_string(i))] = static_cast<int>(i);
        }
    }
    std::vector<Part> parts;
    if (mesh.meshData.submeshes.empty()) {
        SubmeshSection whole;
        whole.firstIndex = 0;
        whole.indexCount = static_cast<uint32_t>(mesh.meshData.indices.size());
        parts.push_back(ExtractPart(mesh.meshData, whole));
        parts.back().material = source.HasKey("materials") && !source["materials"].arrVal.empty() ? 0 : -1;
    } else {
        for (const auto& sub : mesh.meshData.submeshes) {
            Part part = ExtractPart(mesh.meshData, sub);
            auto it = materialIndex.find(sub.materialName);
            part.material = it != materialIndex.end() ? it->second : -1;
            parts.push_back(std::move(part));
        }
    }
    uint32_t sourceTris = 0;
    for (const auto& p : parts) sourceTris += Tris(p);
    result.sourceTriangles += sourceTris;

    // LOD0 always (real scale, baked UV transforms); further LODs while they still reduce
    // Foliage (cards) needs more triangles than solid models to keep its canopy full
    bool hasCards = false;
    for (const auto& p : parts) hasCards = hasCards || (p.cards && Tris(p) > 0);
    std::vector<uint32_t> targets = !lodTriangleTargets.empty() ? lodTriangleTargets
                                  : hasCards ? std::vector<uint32_t>{900000, 180000, 40000}
                                             : std::vector<uint32_t>{150000, 30000, 6000};
    uint32_t previous = sourceTris;
    for (size_t lod = 0; lod < targets.size(); ++lod) {
        if (lod > 0 && previous <= targets[lod] * 3 / 2) break; // would barely change
        std::vector<Part> lodParts = BuildLod(parts, targets[lod]);
        uint32_t tris = 0;
        for (const auto& p : lodParts) tris += Tris(p);
        const std::string out = outBase + "_lod" + std::to_string(lod) + ".gltf";
        std::string error;
        if (!WriteGltf(source, lodParts, out, error)) {
            result.error = error;
            return false;
        }
        result.outputs.push_back(out);
        result.triangleCounts.push_back(tris);
        previous = tris;
    }
    return true;
}

// Root nodes of the file's scene
std::vector<int> SceneRoots(const JsonValue& source) {
    std::vector<int> roots;
    if (source.HasKey("scenes") && !source["scenes"].arrVal.empty()) {
        const int sceneIdx = std::clamp(source["scene"].AsInt(0), 0, static_cast<int>(source["scenes"].arrVal.size()) - 1);
        for (const auto& r : source["scenes"][sceneIdx]["nodes"].arrVal) roots.push_back(r.AsInt(-1));
    }
    return roots;
}

// A file is a set of variants (Poly Haven lays several trees / rocks / clumps out in a row) when
// it has several root nodes at different places; parts of one object share the origin
bool IsVariantSet(const JsonValue& source, const std::vector<int>& roots) {
    if (roots.size() < 2) return false;
    std::vector<glm::vec3> origins;
    for (int r : roots) {
        if (r < 0 || r >= static_cast<int>(source["nodes"].arrVal.size())) return false;
        const auto& n = source["nodes"][r];
        glm::vec3 t(0.0f);
        if (n["translation"].arrVal.size() >= 3) t = glm::vec3(n["translation"][0].AsFloat(), n["translation"][1].AsFloat(), n["translation"][2].AsFloat());
        origins.push_back(t);
    }
    for (size_t i = 1; i < origins.size(); ++i) {
        if (glm::distance(origins[i], origins[0]) > 0.05f) return true;
    }
    return false;
}

std::string VariantSuffix(const std::string& nodeName, const std::string& assetName, int index) {
    std::string s = nodeName;
    if (s.rfind(assetName, 0) == 0) s = s.substr(assetName.size());
    for (const char* tag : { "_LOD0", "_lod0", "_LOD1" }) {
        const size_t at = s.find(tag);
        if (at != std::string::npos) s.erase(at, std::strlen(tag));
    }
    std::string clean;
    for (char c : s) clean += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    while (!clean.empty() && clean.front() == '_') clean.erase(clean.begin());
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    return clean.empty() ? ("v" + std::to_string(index)) : clean;
}

} // namespace

ModelOptimizeResult OptimizeModel(const std::string& gltfPath, const std::vector<uint32_t>& lodTriangleTargets) {
    ModelOptimizeResult result;
    std::ifstream in(gltfPath, std::ios::binary);
    if (!in) { result.error = "cannot open " + gltfPath; return result; }
    std::stringstream ss;
    ss << in.rdbuf();
    const JsonValue source = JsonValue::Parse(ss.str());
    if (!source.IsObject()) { result.error = "not a glTF JSON file: " + gltfPath; return result; }

    const fs::path path(gltfPath);
    const std::string stem = path.stem().string();                       // e.g. fir_tree_01_1k
    const std::string base = (path.parent_path() / stem).string();
    std::string assetName = stem;                                         // e.g. fir_tree_01
    const size_t res = assetName.rfind('_');
    if (res != std::string::npos && res + 1 < assetName.size() && std::isdigit(static_cast<unsigned char>(assetName[res + 1]))) {
        assetName = assetName.substr(0, res);
    }

    // Outputs of earlier runs go first (variants may have changed)
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path.parent_path(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(stem + "_", 0) == 0 && name.find("_lod") != std::string::npos) fs::remove(entry.path(), ec);
    }

    const std::vector<int> roots = SceneRoots(source);
    bool variants = IsVariantSet(source, roots);
    if (variants) {
        // Parts of one object (a lantern's door, a bucket's handle) sit inside each other; variants
        // laid out in a row barely touch. Any two roots whose boxes mostly overlap: one object.
        std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
        for (int r : roots) {
            JsonValue one = source;
            const int sceneIdx = std::clamp(one["scene"].AsInt(0), 0, static_cast<int>(one["scenes"].arrVal.size()) - 1);
            JsonValue only = JsonValue::Array();
            only.arrVal.push_back(r);
            one["scenes"][sceneIdx]["nodes"] = only;
            const std::string json = one.Serialize(0);
            LoadedMesh m;
            if (GltfLoader::LoadFromMemory(reinterpret_cast<const uint8_t*>(json.data()), json.size(), m, path.parent_path().string(), false)) {
                boxes.emplace_back(m.boundsMin, m.boundsMax);
            }
        }
        for (size_t a = 0; a < boxes.size() && variants; ++a) {
            for (size_t b = a + 1; b < boxes.size(); ++b) {
                bool overlapAll = true;
                for (int k = 0; k < 3; ++k) {
                    const float lo = std::max(boxes[a].first[k], boxes[b].first[k]);
                    const float hi = std::min(boxes[a].second[k], boxes[b].second[k]);
                    const float smaller = std::max(std::min(boxes[a].second[k] - boxes[a].first[k], boxes[b].second[k] - boxes[b].first[k]), 1e-4f);
                    if ((hi - lo) < 0.5f * smaller) { overlapAll = false; break; }
                }
                if (overlapAll) { variants = false; break; }
            }
        }
    }
    if (variants) {
        // One model per root node, each at its own origin
        std::set<std::string> used;
        for (size_t i = 0; i < roots.size(); ++i) {
            const int r = roots[i];
            if (!source["nodes"][r].HasKey("mesh") && !source["nodes"][r].HasKey("children")) continue;
            JsonValue variant = source;
            const int sceneIdx = std::clamp(variant["scene"].AsInt(0), 0, static_cast<int>(variant["scenes"].arrVal.size()) - 1);
            JsonValue only = JsonValue::Array();
            only.arrVal.push_back(r);
            variant["scenes"][sceneIdx]["nodes"] = only;
            JsonValue zero = JsonValue::Array();
            zero.arrVal.push_back(0.0f); zero.arrVal.push_back(0.0f); zero.arrVal.push_back(0.0f);
            variant["nodes"][r]["translation"] = zero;
            const std::string json = variant.Serialize(0);
            LoadedMesh mesh;
            if (!GltfLoader::LoadFromMemory(reinterpret_cast<const uint8_t*>(json.data()), json.size(), mesh,
                                            path.parent_path().string(), false)) {
                continue;
            }
            std::string suffix = VariantSuffix(source["nodes"][r]["name"].AsString(""), assetName, static_cast<int>(i));
            while (used.count(suffix)) suffix += "_" + std::to_string(i);
            used.insert(suffix);
            if (!OptimizeLoaded(source, mesh, base + "_" + suffix, lodTriangleTargets, result)) return result;
        }
    } else {
        LoadedMesh mesh;
        if (!GltfLoader::Load(gltfPath, mesh, false)) { result.error = "could not load " + gltfPath; return result; }
        if (!OptimizeLoaded(source, mesh, base, lodTriangleTargets, result)) return result;
    }
    if (result.outputs.empty()) {
        result.error = "no geometry";
        return result;
    }
    std::ofstream(base + ".optimized") << result.outputs.size() << "\n";
    result.ok = true;
    return result;
}

int RunModelOptimizerCommandLine(int argc, char* argv[], bool& handled) {
    handled = false;
    std::string single, directory;
    std::vector<uint32_t> targets;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--optimize-model=", 0) == 0) single = arg.substr(17);
        else if (arg.rfind("--optimize-models=", 0) == 0) directory = arg.substr(18);
        else if (arg.rfind("--lod-triangles=", 0) == 0) {
            std::stringstream ss(arg.substr(16));
            std::string item;
            while (std::getline(ss, item, ',')) targets.push_back(static_cast<uint32_t>(std::stoul(item)));
        }
    }
    if (single.empty() && directory.empty()) return 0;
    handled = true;

    std::vector<std::string> inputs;
    if (!single.empty()) inputs.push_back(single);
    if (!directory.empty()) {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(directory, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".gltf") continue;
            const std::string stem = entry.path().stem().string();
            if (stem.find("_lod") != std::string::npos) continue; // our own output
            // Up to date: optimized after the source last changed
            const fs::path marker = entry.path().parent_path() / (stem + ".optimized");
            if (fs::exists(marker, ec) && fs::last_write_time(marker, ec) >= fs::last_write_time(entry.path(), ec)) continue;
            inputs.push_back(entry.path().string());
        }
        std::sort(inputs.begin(), inputs.end());
    }

    int failures = 0;
    for (const auto& path : inputs) {
        const auto start = std::chrono::steady_clock::now();
        std::cout << "[ModelOptimizer] " << path << " ..." << std::flush;
        ModelOptimizeResult r = OptimizeModel(path, targets);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!r.ok) {
            std::cout << " FAILED: " << r.error << std::endl;
            ++failures;
            continue;
        }
        std::cout << " " << r.sourceTriangles << " tris -> " << r.outputs.size() << " files (";
        for (size_t k = 0; k < r.triangleCounts.size() && k < 6; ++k) std::cout << (k ? " " : "") << r.triangleCounts[k];
        if (r.triangleCounts.size() > 6) std::cout << " ...";
        std::cout << ")";
        std::cout << " (" << static_cast<int>(secs * 10) / 10.0 << " s)" << std::endl;
    }
    std::cout << "[ModelOptimizer] " << inputs.size() - failures << " optimized, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

} // namespace Tools
