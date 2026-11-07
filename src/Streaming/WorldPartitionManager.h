#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "StreamingTypes.h"
#include "Scene/Node.h"

class Scene;

namespace Streaming {

struct WorldPartitionCell {
    GridCellCoord coord{0, 0};
    CellState state{CellState::Unloaded};
    StreamingPriority priority{StreamingPriority::Normal};
    std::vector<std::shared_ptr<Node>> nodes;
    std::string sectorFilePath{""};
    size_t estimatedSizeBytes{0};
    float distanceToCamera{0.0f};
    bool inFrustum{false};
    uint64_t activeRequestId{0};
};

class WorldPartitionManager {
public:
    static WorldPartitionManager& GetInstance();

    void Initialize(const PartitionSettings& settings = PartitionSettings());
    void Shutdown();

    // Configuration
    void SetSettings(const PartitionSettings& settings) { m_settings = settings; }
    const PartitionSettings& GetSettings() const { return m_settings; }
    PartitionSettings& GetSettingsMutable() { return m_settings; }

    // Spatial coordinate transforms
    GridCellCoord WorldToCellCoord(const glm::vec3& worldPos) const;
    glm::vec3 CellToWorldCenter(const GridCellCoord& coord) const;

    // Node management
    void RegisterNode(std::shared_ptr<Node> node);
    void UnregisterNode(uint32_t nodeId);
    void ClearAllCells();

    // Ingest and partition an entire existing Scene
    void PartitionScene(Scene& scene);

    // Frame update: evaluates streaming radius, camera frustum, and transitions cell states
    void Update(const glm::vec3& cameraPos, const glm::mat4& viewProj, Scene& activeScene, float deltaTime);

    // Explicit streaming actions
    void ForceLoadCell(const GridCellCoord& coord, Scene& activeScene);
    void ForceUnloadCell(const GridCellCoord& coord, Scene& activeScene);
    void ForceLoadAll(Scene& activeScene);

    // Query states
    bool IsCellLoaded(const GridCellCoord& coord) const;
    bool IsCellActive(const GridCellCoord& coord) const;
    const WorldPartitionCell* GetCell(const GridCellCoord& coord) const;
    const std::unordered_map<GridCellCoord, WorldPartitionCell, GridCellHash>& GetAllCells() const { return m_cells; }

    // Telemetry & metrics
    PartitionStats GetStats() const;

private:
    WorldPartitionManager();
    ~WorldPartitionManager();

    WorldPartitionManager(const WorldPartitionManager&) = delete;
    WorldPartitionManager& operator=(const WorldPartitionManager&) = delete;

    bool TestFrustumIntersection(const glm::mat4& viewProj, const glm::vec3& center, float radius) const;
    void EnforceMemoryBudget(Scene& activeScene);
    void ActivateCell(WorldPartitionCell& cell, Scene& activeScene);
    void DeactivateCell(WorldPartitionCell& cell, Scene& activeScene);
    void EvictCell(WorldPartitionCell& cell, Scene& activeScene);

    PartitionSettings m_settings;
    mutable std::mutex m_mutex;
    std::unordered_map<GridCellCoord, WorldPartitionCell, GridCellHash> m_cells;
    std::unordered_map<uint32_t, GridCellCoord> m_nodeCellMap;
    bool m_initialized{false};
};

} // namespace Streaming

