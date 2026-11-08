#include "WorldStreamer.h"
#include "DirectStorageQueue.h"
#include "Scene/Scene.h"
#include "Scene/Node.h"
#include "Utils/SimpleJson.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace Streaming {

namespace {
constexpr uint32_t kMaxInFlight = 6;   // sector reads queued at once

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// ----------------------------------------------------------------------------------------------
// Manifest
// ----------------------------------------------------------------------------------------------

bool WorldManifest::Load(const std::string& worldFile, WorldManifest& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    std::ifstream file(worldFile, std::ios::binary);
    if (!file.is_open()) return fail("cannot open " + worldFile);
    std::stringstream ss;
    ss << file.rdbuf();
    JsonValue root = JsonValue::Parse(ss.str());
    if (!root.IsObject()) return fail("not a JSON object: " + worldFile);
    if (root["format"].AsString("") != "tessera-world-1") return fail("unknown world format in " + worldFile);

    WorldManifest m;
    m.path = worldFile;
    m.rootDir = fs::path(worldFile).parent_path().string();
    m.name = root["name"].AsString(fs::path(worldFile).stem().string());
    m.cellSize = std::max(root["cellSize"].AsFloat(128.0f), 1.0f);
    m.loadRadius = std::max(root["loadRadius"].AsFloat(m.cellSize * 3.0f), m.cellSize * 0.5f);
    m.unloadRadius = std::max(root["unloadRadius"].AsFloat(m.loadRadius + m.cellSize), m.loadRadius + 1.0f);
    m.baseScene = root["baseScene"].AsString("");
    const JsonValue& sectors = root["sectors"];
    if (sectors.IsArray()) {
        for (const auto& s : sectors.arrVal) {
            WorldSectorInfo info;
            info.coord = GridCellCoord{ s["x"].AsInt(0), s["z"].AsInt(0) };
            info.file = s["file"].AsString("");
            info.lodNode = s["lodNode"].AsString("");
            info.bytes = static_cast<size_t>(s["bytes"].AsDouble(0.0));
            if (!info.file.empty()) m.sectors.push_back(info);
        }
    }
    out = std::move(m);
    return true;
}

std::string WorldManifest::Resolve(const std::string& relative) const {
    if (relative.empty()) return relative;
    fs::path p(relative);
    if (p.is_absolute() || rootDir.empty()) return p.string();
    return (fs::path(rootDir) / p).string();
}

// ----------------------------------------------------------------------------------------------
// Streamer
// ----------------------------------------------------------------------------------------------

WorldStreamer::WorldStreamer() : m_shared(std::make_shared<Shared>()) {}

WorldStreamer::~WorldStreamer() {
    // In-flight worker reads may still finish: they see the streamer is gone and drop their data
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->alive = false;
    m_shared->ready.clear();
}

void WorldStreamer::SetHooks(const Hooks& hooks) {
    m_hooks = hooks;
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->hooks = hooks;
}

std::string WorldStreamer::SectorKey(const GridCellCoord& coord) {
    return std::to_string(coord.x) + "," + std::to_string(coord.z);
}

bool WorldStreamer::Open(const std::string& worldFile) {
    WorldManifest manifest;
    std::string error;
    if (!WorldManifest::Load(worldFile, manifest, &error)) {
        std::cerr << "[WorldStreamer] " << error << std::endl;
        return false;
    }
    return Open(manifest);
}

bool WorldStreamer::Open(const WorldManifest& manifest) {
    m_manifest = manifest;
    m_sectors.clear();
    for (const auto& info : m_manifest.sectors) {
        Sector sector;
        sector.info = info;
        m_sectors[info.coord] = std::move(sector);
    }
    m_open = true;
    m_inFlight = 0;
    m_stats = WorldStreamerStats{};
    m_stats.totalSectors = static_cast<uint32_t>(m_sectors.size());
    DirectStorageQueue::GetInstance().Initialize();
    std::cout << "[WorldStreamer] Opened world '" << m_manifest.name << "': " << m_sectors.size()
              << " sectors of " << m_manifest.cellSize << " m, load radius " << m_manifest.loadRadius << " m" << std::endl;
    return true;
}

float WorldStreamer::DistanceToSector(const GridCellCoord& coord, const glm::vec3& focus) const {
    // Distance from the focus to the sector's square (0 inside it), on the ground plane
    const float size = m_manifest.cellSize;
    const float minX = coord.x * size, maxX = minX + size;
    const float minZ = coord.z * size, maxZ = minZ + size;
    const float dx = std::max({ minX - focus.x, 0.0f, focus.x - maxX });
    const float dz = std::max({ minZ - focus.z, 0.0f, focus.z - maxZ });
    return std::sqrt(dx * dx + dz * dz);
}

bool WorldStreamer::IsSectorActive(const GridCellCoord& coord) const {
    auto it = m_sectors.find(coord);
    return it != m_sectors.end() && it->second.state == SectorState::Active;
}

void WorldStreamer::PrepareOnWorker(const std::shared_ptr<Shared>& shared, const std::vector<uint8_t>& bytes,
                                    GridCellCoord coord, uint64_t generation) {
    auto prepared = std::make_shared<PreparedSector>();
    prepared->coord = coord;
    prepared->bytes = bytes.size();

    Hooks hooks;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->alive) return;
        hooks = shared->hooks;
    }

    JsonValue root = bytes.empty() ? JsonValue() : JsonValue::Parse(std::string(bytes.begin(), bytes.end()));
    prepared->scene = std::make_shared<Scene>();
    if (!root.IsObject() || !prepared->scene->Deserialize(root, nullptr)) {
        prepared->failed = true;
    } else {
        std::set<std::string> names;
        for (const auto& node : prepared->scene->GetNodes()) {
            if (node && node->type == ShapeType::Custom && !node->customMeshName.empty() &&
                node->nodeType != NodeType::Terrain) {
                names.insert(node->customMeshName);
            }
        }
        prepared->meshNames.assign(names.begin(), names.end());
        // Models that are not resident yet are loaded here, off the main thread
        for (const auto& name : prepared->meshNames) {
            if (hooks.isMeshResident && hooks.isMeshResident(name)) continue;
            if (!hooks.loadMeshCPU) continue;
            auto mesh = std::make_shared<LoadedMesh>();
            if (hooks.loadMeshCPU(name, *mesh)) prepared->loaded[name] = mesh;
        }
    }

    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->alive) shared->ready.emplace_back(generation, std::move(prepared));
}

void WorldStreamer::RequestLoad(Sector& sector) {
    sector.state = SectorState::Loading;
    sector.generation = m_nextGeneration++;
    ++m_inFlight;

    DirectStorageRequest req;
    req.cell = sector.info.coord;
    req.filePath = m_manifest.Resolve(sector.info.file);
    req.fileSize = 0; // whole file
    const float d = sector.distance;
    req.priority = d <= 0.0f ? StreamingPriority::Critical
                 : d < m_manifest.cellSize ? StreamingPriority::High
                 : d < m_manifest.loadRadius * 0.6f ? StreamingPriority::Normal
                 : StreamingPriority::Low;
    std::shared_ptr<Shared> shared = m_shared;
    const GridCellCoord coord = sector.info.coord;
    const uint64_t generation = sector.generation;
    req.onComplete = [shared, coord, generation](std::vector<uint8_t> bytes) {
        PrepareOnWorker(shared, bytes, coord, generation);
    };
    DirectStorageQueue::GetInstance().EnqueueRequest(req);
}

void WorldStreamer::HideLod(Scene& scene, Sector& sector) {
    if (sector.info.lodNode.empty() || sector.hiddenLod) return;
    auto lod = scene.GetNodeByName(sector.info.lodNode);
    if (!lod) return;
    if (m_hooks.onDetaching) m_hooks.onDetaching(scene, { lod });
    scene.DetachNode(lod->id);
    sector.hiddenLod = lod;
}

void WorldStreamer::RestoreLod(Scene& scene, Sector& sector) {
    if (!sector.hiddenLod) return;
    if (!scene.GetNodeById(sector.hiddenLod->id)) {
        scene.AttachNode(sector.hiddenLod);
        if (m_hooks.onAttached) m_hooks.onAttached(scene, { sector.hiddenLod });
    }
    sector.hiddenLod.reset();
}

bool WorldStreamer::Activate(Scene& scene, Sector& sector) {
    const double activateStart = NowMs();
    auto prepared = sector.prepared;
    sector.prepared.reset();
    if (!prepared || prepared->failed) {
        std::cerr << "[WorldStreamer] Sector " << SectorKey(sector.info.coord) << " (" << sector.info.file
                  << ") could not be read" << std::endl;
        sector.state = SectorState::Unloaded;
        return false;
    }

    // 1. Models (GPU upload of the ones the worker loaded)
    sector.meshNames.clear();
    for (const auto& name : prepared->meshNames) {
        auto loaded = prepared->loaded.find(name);
        LoadedMesh* cpu = loaded != prepared->loaded.end() ? loaded->second.get() : nullptr;
        if (m_hooks.acquireMesh && m_hooks.acquireMesh(name, cpu)) {
            sector.meshNames.push_back(name);
        }
    }

    // 2. Nodes: fresh ids in this scene, parents and children remapped
    const auto nodes = prepared->scene->GetNodes(); // copy: the temporary scene still owns them
    std::map<uint32_t, uint32_t> remap;
    for (const auto& node : nodes) {
        if (node) remap[node->id] = scene.AllocateNodeId();
    }
    const std::string key = SectorKey(sector.info.coord);
    std::vector<std::shared_ptr<Node>> attached;
    attached.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (!node) continue;
        node->id = remap[node->id];
        auto parent = remap.find(node->parentId);
        node->parentId = parent != remap.end() ? parent->second : 0;
        for (auto& child : node->childrenIds) {
            auto c = remap.find(child);
            child = c != remap.end() ? c->second : 0;
        }
        node->childrenIds.erase(std::remove(node->childrenIds.begin(), node->childrenIds.end(), 0u), node->childrenIds.end());
        node->streamSector = key;
        scene.AttachNode(node);
        attached.push_back(node);
    }
    sector.nodeIds.clear();
    for (const auto& n : attached) sector.nodeIds.push_back(n->id);

    // 3. The detailed sector replaces its low-detail stand-in
    HideLod(scene, sector);
    if (m_hooks.onAttached && !attached.empty()) m_hooks.onAttached(scene, attached);

    sector.state = SectorState::Active;
    m_stats.sectorsStreamedIn++;
    m_stats.bytesRead += prepared->bytes;
    std::cout << "[WorldStreamer] + sector " << key << " (" << attached.size() << " nodes, "
              << prepared->bytes / 1024 << " KB, " << prepared->loaded.size() << " models loaded, "
              << static_cast<int>((NowMs() - activateStart) * 100.0) / 100.0 << " ms on the main thread)" << std::endl;
    return true;
}

void WorldStreamer::Deactivate(Scene& scene, Sector& sector, bool nodesStillInScene) {
    if (nodesStillInScene) {
        std::vector<std::shared_ptr<Node>> nodes;
        for (uint32_t id : sector.nodeIds) {
            if (auto n = scene.GetNodeById(id)) nodes.push_back(n);
        }
        if (m_hooks.onDetaching && !nodes.empty()) m_hooks.onDetaching(scene, nodes);
        for (const auto& n : nodes) scene.DetachNode(n->id);
    }
    sector.nodeIds.clear();
    for (const auto& name : sector.meshNames) {
        if (m_hooks.releaseMesh) m_hooks.releaseMesh(name);
    }
    sector.meshNames.clear();
    RestoreLod(scene, sector);
    sector.state = SectorState::Unloaded;
    m_stats.sectorsStreamedOut++;
    std::cout << "[WorldStreamer] - sector " << SectorKey(sector.info.coord) << std::endl;
}

void WorldStreamer::Update(Scene& scene, const glm::vec3& focus, float budgetMs) {
    if (!m_open) return;

    // 1. Collect worker results
    std::vector<std::pair<uint64_t, std::shared_ptr<PreparedSector>>> arrived;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        arrived.swap(m_shared->ready);
    }
    for (auto& [generation, prepared] : arrived) {
        if (m_inFlight > 0) --m_inFlight;
        auto it = m_sectors.find(prepared->coord);
        if (it == m_sectors.end()) continue;
        Sector& sector = it->second;
        if (sector.state != SectorState::Loading || sector.generation != generation) continue; // abandoned
        sector.prepared = prepared;
        sector.state = SectorState::Ready;
    }

    // 2. Distances; sectors whose nodes vanished (scene reloaded, undo) are forgotten
    std::vector<Sector*> wanted;
    for (auto& [coord, sector] : m_sectors) {
        sector.distance = DistanceToSector(coord, focus);
        if (sector.state == SectorState::Active && !sector.nodeIds.empty() &&
            !scene.GetNodeById(sector.nodeIds.front())) {
            Deactivate(scene, sector, false);
        }
    }

    // 3. Far sectors go
    for (auto& [coord, sector] : m_sectors) {
        if (sector.distance <= m_manifest.unloadRadius) continue;
        if (sector.state == SectorState::Active) {
            Deactivate(scene, sector, true);
        } else if (sector.state == SectorState::Ready) {
            sector.prepared.reset();
            sector.state = SectorState::Unloaded;
        } else if (sector.state == SectorState::Loading) {
            sector.generation = 0; // the result will be ignored
            sector.state = SectorState::Unloaded;
        }
    }

    // 4. Near sectors are requested, nearest first
    for (auto& [coord, sector] : m_sectors) {
        if (sector.state == SectorState::Unloaded && sector.distance <= m_manifest.loadRadius) wanted.push_back(&sector);
    }
    std::sort(wanted.begin(), wanted.end(), [](const Sector* a, const Sector* b) { return a->distance < b->distance; });
    for (Sector* sector : wanted) {
        if (m_inFlight >= kMaxInFlight) break;
        RequestLoad(*sector);
    }

    // 5. Ready sectors join the scene within the frame budget, nearest first
    std::vector<Sector*> ready;
    for (auto& [coord, sector] : m_sectors) {
        if (sector.state == SectorState::Ready) ready.push_back(&sector);
    }
    std::sort(ready.begin(), ready.end(), [](const Sector* a, const Sector* b) { return a->distance < b->distance; });
    const double start = NowMs();
    for (Sector* sector : ready) {
        if (NowMs() - start > budgetMs && sector != ready.front()) break;
        Activate(scene, *sector);
    }
    m_stats.lastActivateMs = static_cast<float>(NowMs() - start);
    m_stats.maxActivateMs = std::max(m_stats.maxActivateMs, m_stats.lastActivateMs);
}

void WorldStreamer::Adopt(const WorldStreamer& other, Scene& scene) {
    Open(other.m_manifest);
    // Sectors the other streamer had attached are already in this (cloned) scene
    std::map<std::string, std::vector<uint32_t>> bySector;
    for (const auto& node : scene.GetNodes()) {
        if (node && !node->streamSector.empty()) bySector[node->streamSector].push_back(node->id);
    }
    for (const auto& [coord, otherSector] : other.m_sectors) {
        if (otherSector.state != SectorState::Active) continue;
        auto it = m_sectors.find(coord);
        auto ids = bySector.find(SectorKey(coord));
        if (it == m_sectors.end() || ids == bySector.end()) continue;
        Sector& sector = it->second;
        sector.nodeIds = ids->second;
        for (const auto& name : otherSector.meshNames) {
            if (m_hooks.acquireMesh && m_hooks.acquireMesh(name, nullptr)) sector.meshNames.push_back(name);
        }
        // The stand-in was already out of the scene that was cloned: keep a copy to put back
        if (otherSector.hiddenLod) sector.hiddenLod = otherSector.hiddenLod->Clone();
        if (sector.hiddenLod) sector.hiddenLod->id = otherSector.hiddenLod->id;
        sector.state = SectorState::Active;
    }
}

void WorldStreamer::Close(Scene& scene) {
    if (!m_open) return;
    for (auto& [coord, sector] : m_sectors) {
        if (sector.state == SectorState::Active) Deactivate(scene, sector, true);
        sector.prepared.reset();
        sector.generation = 0;
        sector.state = SectorState::Unloaded;
    }
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->ready.clear();
    }
    m_inFlight = 0;
    m_open = false;
}

void WorldStreamer::ReleaseAll() {
    for (auto& [coord, sector] : m_sectors) {
        for (const auto& name : sector.meshNames) {
            if (m_hooks.releaseMesh) m_hooks.releaseMesh(name);
        }
        sector.meshNames.clear();
        sector.nodeIds.clear();
        sector.hiddenLod.reset();
        sector.prepared.reset();
        sector.generation = 0;
        sector.state = SectorState::Unloaded;
    }
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->ready.clear();
    }
    m_inFlight = 0;
    m_open = false;
}

void WorldStreamer::SuspendForSave(Scene& scene) {
    m_suspendedNodes.clear();
    m_suspendedLods.clear();
    for (const auto& node : scene.GetNodes()) {
        if (node && !node->streamSector.empty()) m_suspendedNodes.push_back(node);
    }
    for (const auto& n : m_suspendedNodes) scene.DetachNode(n->id);
    for (auto& [coord, sector] : m_sectors) {
        if (sector.hiddenLod && !scene.GetNodeById(sector.hiddenLod->id)) {
            scene.AttachNode(sector.hiddenLod);
            m_suspendedLods.push_back(sector.hiddenLod);
        }
    }
}

void WorldStreamer::ResumeAfterSave(Scene& scene) {
    for (const auto& lod : m_suspendedLods) scene.DetachNode(lod->id);
    for (const auto& n : m_suspendedNodes) scene.AttachNode(n);
    m_suspendedNodes.clear();
    m_suspendedLods.clear();
}

void WorldStreamer::LoadAround(Scene& scene, const glm::vec3& focus, float timeoutSeconds) {
    if (!m_open) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeoutSeconds * 1000.0f));
    while (std::chrono::steady_clock::now() < deadline) {
        Update(scene, focus, 1e9f);
        bool pending = false;
        for (const auto& [coord, sector] : m_sectors) {
            if (sector.distance <= m_manifest.loadRadius && sector.state != SectorState::Active) {
                pending = true;
                break;
            }
        }
        if (!pending) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cerr << "[WorldStreamer] LoadAround timed out" << std::endl;
}

WorldStreamerStats WorldStreamer::GetStats() const {
    WorldStreamerStats s = m_stats;
    s.totalSectors = static_cast<uint32_t>(m_sectors.size());
    s.activeSectors = s.loadingSectors = s.readySectors = s.activeNodes = 0;
    for (const auto& [coord, sector] : m_sectors) {
        switch (sector.state) {
            case SectorState::Active: s.activeSectors++; s.activeNodes += static_cast<uint32_t>(sector.nodeIds.size()); break;
            case SectorState::Loading: s.loadingSectors++; break;
            case SectorState::Ready: s.readySectors++; break;
            default: break;
        }
    }
    return s;
}

} // namespace Streaming
