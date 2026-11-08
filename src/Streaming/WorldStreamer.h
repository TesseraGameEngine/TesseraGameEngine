#pragma once

// Disk-backed open world streaming.
//
// A world is a manifest (`<name>.world`, JSON) next to an always-loaded base scene and a grid of
// sector scene files:
//
//   {
//     "format": "tessera-world-1",
//     "name": "Headland",
//     "cellSize": 128,
//     "baseScene": "base.scene",
//     "loadRadius": 400, "unloadRadius": 520,
//     "sectors": [ { "x": 0, "z": 0, "file": "sectors/sector_0_0.scene",
//                    "lodNode": "LOD_0_0", "bytes": 123456 }, ... ]
//   }
//
// Sector files are ordinary .scene files (they open in the editor). Paths are relative to the
// manifest. `lodNode` names a low-detail stand-in in the base scene (e.g. a coarse terrain tile)
// that is taken out while the sector is loaded and put back when it unloads.
//
// Streaming a sector in:
//   1. worker thread (DirectStorageQueue): read the file, parse it into a temporary Scene and
//      load the models it uses that are not resident yet (CPU only)
//   2. main thread, within a per-frame time budget: make its models resident (GPU upload through
//      the engine's hooks), give its nodes fresh ids, attach them, and take out its LOD stand-in
// Streaming out detaches the nodes (after the engine's hook removes physics bodies, scripts...),
// releases the models and puts the stand-in back.

#include "StreamingTypes.h"
#include "Scene/ObjLoader.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;
struct Node;

namespace Streaming {

struct WorldSectorInfo {
    GridCellCoord coord{0, 0};
    std::string file;      // relative to the manifest
    std::string lodNode;   // base scene node replaced by this sector (optional)
    size_t bytes{0};
};

struct WorldManifest {
    std::string path;      // the .world file
    std::string rootDir;   // its directory
    std::string name;
    float cellSize{128.0f};
    float loadRadius{400.0f};
    float unloadRadius{520.0f};
    std::string baseScene; // relative to rootDir
    std::vector<WorldSectorInfo> sectors;

    static bool Load(const std::string& worldFile, WorldManifest& out, std::string* error = nullptr);
    std::string Resolve(const std::string& relative) const;
    std::string BaseScenePath() const { return Resolve(baseScene); }
};

struct WorldStreamerStats {
    uint32_t totalSectors{0};
    uint32_t activeSectors{0};
    uint32_t loadingSectors{0};   // reading / parsing on a worker
    uint32_t readySectors{0};     // parsed, waiting for the main thread
    uint32_t activeNodes{0};
    uint32_t sectorsStreamedIn{0};
    uint32_t sectorsStreamedOut{0};
    uint64_t bytesRead{0};
    float lastActivateMs{0.0f};   // main-thread time spent attaching sectors last frame
    float maxActivateMs{0.0f};
};

class WorldStreamer {
public:
    struct Hooks {
        // Worker thread (must be thread-safe): is this model already resident?
        std::function<bool(const std::string& meshName)> isMeshResident;
        // Worker thread: load a model's CPU data
        std::function<bool(const std::string& meshName, LoadedMesh& out)> loadMeshCPU;
        // Main thread: one more sector uses this model. `cpuData` is the worker's copy when it
        // loaded one (null when it was resident). Returns false when it cannot be made resident.
        std::function<bool(const std::string& meshName, LoadedMesh* cpuData)> acquireMesh;
        // Main thread: one sector fewer uses this model
        std::function<void(const std::string& meshName)> releaseMesh;
        // Main thread: nodes joined / are about to leave the scene (physics bodies, scripts,
        // terrain meshes...)
        std::function<void(Scene& scene, const std::vector<std::shared_ptr<Node>>& nodes)> onAttached;
        std::function<void(Scene& scene, const std::vector<std::shared_ptr<Node>>& nodes)> onDetaching;
    };

    WorldStreamer();
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer&) = delete;
    WorldStreamer& operator=(const WorldStreamer&) = delete;

    void SetHooks(const Hooks& hooks);

    // Opens a world (manifest only; the base scene is loaded by the caller)
    bool Open(const std::string& worldFile);
    bool Open(const WorldManifest& manifest);
    bool IsOpen() const { return m_open; }
    const WorldManifest& GetManifest() const { return m_manifest; }

    // Takes over the streamed state of `other` (whose scene `scene` is a clone of): sectors
    // already streamed into the clone stay, with their models acquired again for this streamer.
    void Adopt(const WorldStreamer& other, Scene& scene);

    // Streams sectors in and out around `focus`. At most `budgetMs` of main-thread time is spent
    // attaching new sectors (at least one sector is attached per frame when any is ready).
    void Update(Scene& scene, const glm::vec3& focus, float budgetMs = 4.0f);

    // Detaches everything streamed in and puts the stand-ins back (the scene is as loaded)
    void Close(Scene& scene);
    // Forgets everything without touching a scene (the scene it streamed into is gone or was
    // replaced): models are released, the world closes
    void ReleaseAll();

    // Temporarily restores the base scene (for saving it), then puts the streamed state back
    void SuspendForSave(Scene& scene);
    void ResumeAfterSave(Scene& scene);

    // Waits until every sector within the load radius of `focus` is attached (loading screens,
    // tests, screenshots)
    void LoadAround(Scene& scene, const glm::vec3& focus, float timeoutSeconds = 30.0f);

    WorldStreamerStats GetStats() const;
    float DistanceToSector(const GridCellCoord& coord, const glm::vec3& focus) const;
    bool IsSectorActive(const GridCellCoord& coord) const;
    static std::string SectorKey(const GridCellCoord& coord);

private:
    enum class SectorState { Unloaded, Loading, Ready, Active };

    // Parsed on a worker, attached on the main thread
    struct PreparedSector {
        GridCellCoord coord{0, 0};
        std::shared_ptr<Scene> scene;
        std::vector<std::string> meshNames;                       // every model it uses
        std::unordered_map<std::string, std::shared_ptr<LoadedMesh>> loaded; // loaded by the worker
        size_t bytes{0};
        bool failed{false};
    };

    struct Sector {
        WorldSectorInfo info;
        SectorState state{SectorState::Unloaded};
        uint64_t generation{0};                    // bumps when a load is abandoned
        std::vector<uint32_t> nodeIds;             // attached nodes
        std::vector<std::string> meshNames;        // models acquired for it
        std::shared_ptr<Node> hiddenLod;           // the stand-in taken out while active
        std::shared_ptr<PreparedSector> prepared;
        float distance{0.0f};
    };

    // State shared with worker callbacks (outlives the streamer if a read is in flight)
    struct Shared {
        std::mutex mutex;
        std::vector<std::pair<uint64_t, std::shared_ptr<PreparedSector>>> ready; // (generation, data)
        Hooks hooks;
        bool alive{true};
    };

    void RequestLoad(Sector& sector);
    bool Activate(Scene& scene, Sector& sector);
    void Deactivate(Scene& scene, Sector& sector, bool nodesStillInScene);
    void HideLod(Scene& scene, Sector& sector);
    void RestoreLod(Scene& scene, Sector& sector);
    static void PrepareOnWorker(const std::shared_ptr<Shared>& shared, const std::vector<uint8_t>& bytes,
                                GridCellCoord coord, uint64_t generation);

    WorldManifest m_manifest;
    bool m_open{false};
    std::unordered_map<GridCellCoord, Sector, GridCellHash> m_sectors;
    std::shared_ptr<Shared> m_shared;
    Hooks m_hooks;
    uint64_t m_nextGeneration{1};
    uint32_t m_inFlight{0};
    WorldStreamerStats m_stats;

    // SuspendForSave bookkeeping
    std::vector<std::shared_ptr<Node>> m_suspendedNodes;
    std::vector<std::shared_ptr<Node>> m_suspendedLods;
};

} // namespace Streaming
