#include "WorldPartitionManager.h"
#include "DirectStorageQueue.h"
#include "Scene/Scene.h"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace Streaming {

WorldPartitionManager& WorldPartitionManager::GetInstance() {
    static WorldPartitionManager instance;
    return instance;
}

WorldPartitionManager::WorldPartitionManager() {
}

WorldPartitionManager::~WorldPartitionManager() {
    Shutdown();
}

void WorldPartitionManager::Initialize(const PartitionSettings& settings) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_settings = settings;
    m_initialized = true;
    DirectStorageQueue::GetInstance().Initialize();
}

void WorldPartitionManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cells.clear();
    m_nodeCellMap.clear();
    m_initialized = false;
}

GridCellCoord WorldPartitionManager::WorldToCellCoord(const glm::vec3& worldPos) const {
    float size = m_settings.cellSize > 0.0f ? m_settings.cellSize : 64.0f;
    int32_t cx = static_cast<int32_t>(std::floor(worldPos.x / size));
    int32_t cz = static_cast<int32_t>(std::floor(worldPos.z / size));
    return GridCellCoord{cx, cz};
}

glm::vec3 WorldPartitionManager::CellToWorldCenter(const GridCellCoord& coord) const {
    float size = m_settings.cellSize > 0.0f ? m_settings.cellSize : 64.0f;
    return glm::vec3(
        (static_cast<float>(coord.x) + 0.5f) * size,
        0.0f,
        (static_cast<float>(coord.z) + 0.5f) * size
    );
}

void WorldPartitionManager::RegisterNode(std::shared_ptr<Node> node) {
    if (!node) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    GridCellCoord coord = WorldToCellCoord(node->position);

    auto& cell = m_cells[coord];
    cell.coord = coord;

    // Check if node already registered in this cell
    auto it = std::find_if(cell.nodes.begin(), cell.nodes.end(), [&node](const std::shared_ptr<Node>& n) {
        return n && n->id == node->id;
    });

    if (it == cell.nodes.end()) {
        cell.nodes.push_back(node);
        // Estimate 64KB footprint per node if no custom geometry
        cell.estimatedSizeBytes += 64 * 1024;
    }

    m_nodeCellMap[node->id] = coord;
}

void WorldPartitionManager::UnregisterNode(uint32_t nodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto mapIt = m_nodeCellMap.find(nodeId);
    if (mapIt == m_nodeCellMap.end()) return;

    GridCellCoord coord = mapIt->second;
    m_nodeCellMap.erase(mapIt);

    auto cellIt = m_cells.find(coord);
    if (cellIt != m_cells.end()) {
        auto& nodes = cellIt->second.nodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [nodeId](const std::shared_ptr<Node>& n) {
            return n && n->id == nodeId;
        }), nodes.end());

        if (cellIt->second.estimatedSizeBytes >= 64 * 1024) {
            cellIt->second.estimatedSizeBytes -= 64 * 1024;
        }
    }
}

void WorldPartitionManager::ClearAllCells() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cells.clear();
    m_nodeCellMap.clear();
}

void WorldPartitionManager::PartitionScene(Scene& scene) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cells.clear();
    m_nodeCellMap.clear();

    const auto& nodes = scene.GetNodes();
    for (const auto& node : nodes) {
        if (!node) continue;
        GridCellCoord coord = WorldToCellCoord(node->position);
        auto& cell = m_cells[coord];
        cell.coord = coord;
        cell.nodes.push_back(node);
        cell.estimatedSizeBytes += 64 * 1024;
        cell.state = CellState::Active; // Already in scene
        m_nodeCellMap[node->id] = coord;
    }
}

bool WorldPartitionManager::TestFrustumIntersection(const glm::mat4& vp, const glm::vec3& center, float radius) const {
    // Extract 6 frustum planes from column-major view-projection matrix
    glm::vec4 planes[6];
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
    planes[4] = glm::vec4(vp[0][2],            vp[1][2],            vp[2][2],            vp[3][2]);            // Near (Vulkan [0,1])
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far

    for (int i = 0; i < 6; ++i) {
        float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0001f) {
            planes[i] /= length;
        }
        float dist = glm::dot(glm::vec3(planes[i]), center) + planes[i].w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

void WorldPartitionManager::ActivateCell(WorldPartitionCell& cell, Scene& activeScene) {
    if (cell.state == CellState::Active) return;

    for (auto& node : cell.nodes) {
        if (node) {
            activeScene.AttachNode(node);
        }
    }
    cell.state = CellState::Active;
}

void WorldPartitionManager::DeactivateCell(WorldPartitionCell& cell, Scene& activeScene) {
    if (cell.state != CellState::Active) return;

    for (auto& node : cell.nodes) {
        if (node) {
            activeScene.DetachNode(node->id);
        }
    }
    cell.state = CellState::Loaded;
}

void WorldPartitionManager::EvictCell(WorldPartitionCell& cell, Scene& activeScene) {
    if (cell.state == CellState::Active) {
        DeactivateCell(cell, activeScene);
    }
    if (cell.activeRequestId != 0) {
        DirectStorageQueue::GetInstance().CancelRequest(cell.activeRequestId);
        cell.activeRequestId = 0;
    }
    cell.state = CellState::Unloaded;
}

void WorldPartitionManager::Update(const glm::vec3& cameraPos, const glm::mat4& viewProj, Scene& activeScene, float deltaTime) {
    (void)deltaTime;
    if (!m_settings.enableStreaming) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    float cellRadius = m_settings.cellSize * 0.7071f; // half diagonal

    for (auto& pair : m_cells) {
        auto& cell = pair.second;
        glm::vec3 cellCenter = CellToWorldCenter(cell.coord);
        float dx = cameraPos.x - cellCenter.x;
        float dz = cameraPos.z - cellCenter.z;
        cell.distanceToCamera = std::sqrt(dx * dx + dz * dz);

        if (m_settings.enableFrustumPriority) {
            cell.inFrustum = TestFrustumIntersection(viewProj, cellCenter, cellRadius);
        } else {
            cell.inFrustum = true;
        }

        // Determine Priority
        if (cell.distanceToCamera < 50.0f) {
            cell.priority = StreamingPriority::Critical;
        } else if (cell.inFrustum && cell.distanceToCamera <= m_settings.loadingRadius) {
            cell.priority = StreamingPriority::High;
        } else if (cell.distanceToCamera <= m_settings.loadingRadius) {
            cell.priority = StreamingPriority::Normal;
        } else {
            cell.priority = StreamingPriority::Low;
        }

        // Streaming State Machine
        if (cell.distanceToCamera <= m_settings.activeRadius) {
            if (cell.state == CellState::Unloaded) {
                // Request load via DirectStorageQueue
                cell.state = CellState::Queued;
                DirectStorageRequest req;
                req.cell = cell.coord;
                req.filePath = cell.sectorFilePath;
                req.fileSize = cell.estimatedSizeBytes > 0 ? cell.estimatedSizeBytes : (64 * 1024);
                req.priority = cell.priority;

                GridCellCoord targetCoord = cell.coord;
                req.onComplete = [this, targetCoord](std::vector<uint8_t> /*bytes*/) {
                    std::lock_guard<std::mutex> innerLock(m_mutex);
                    auto it = m_cells.find(targetCoord);
                    if (it != m_cells.end() && it->second.state == CellState::Queued) {
                        it->second.state = CellState::Loaded;
                        it->second.activeRequestId = 0;
                    }
                };
                cell.activeRequestId = DirectStorageQueue::GetInstance().EnqueueRequest(req);
            } else if (cell.state == CellState::Loaded) {
                ActivateCell(cell, activeScene);
            }
        } else if (cell.distanceToCamera <= m_settings.loadingRadius) {
            if (cell.state == CellState::Active) {
                DeactivateCell(cell, activeScene);
            } else if (cell.state == CellState::Unloaded) {
                cell.state = CellState::Queued;
                DirectStorageRequest req;
                req.cell = cell.coord;
                req.filePath = cell.sectorFilePath;
                req.fileSize = cell.estimatedSizeBytes > 0 ? cell.estimatedSizeBytes : (64 * 1024);
                req.priority = cell.priority;

                GridCellCoord targetCoord = cell.coord;
                req.onComplete = [this, targetCoord](std::vector<uint8_t> /*bytes*/) {
                    std::lock_guard<std::mutex> innerLock(m_mutex);
                    auto it = m_cells.find(targetCoord);
                    if (it != m_cells.end() && it->second.state == CellState::Queued) {
                        it->second.state = CellState::Loaded;
                        it->second.activeRequestId = 0;
                    }
                };
                cell.activeRequestId = DirectStorageQueue::GetInstance().EnqueueRequest(req);
            }
        } else if (cell.distanceToCamera > m_settings.unloadRadius) {
            if (cell.state != CellState::Unloaded) {
                EvictCell(cell, activeScene);
            }
        }
    }

    EnforceMemoryBudget(activeScene);
}

void WorldPartitionManager::EnforceMemoryBudget(Scene& activeScene) {
    size_t budgetBytes = m_settings.maxMemoryBudgetMB * 1024 * 1024;
    size_t currentUsage = 0;

    std::vector<WorldPartitionCell*> evictCandidates;

    for (auto& pair : m_cells) {
        if (pair.second.state == CellState::Active || pair.second.state == CellState::Loaded) {
            currentUsage += pair.second.estimatedSizeBytes;
            if (pair.second.state == CellState::Loaded) {
                evictCandidates.push_back(&pair.second);
            }
        }
    }

    if (currentUsage > budgetBytes) {
        // Sort farthest first
        std::sort(evictCandidates.begin(), evictCandidates.end(), [](const WorldPartitionCell* a, const WorldPartitionCell* b) {
            return a->distanceToCamera > b->distanceToCamera;
        });

        for (auto* cell : evictCandidates) {
            if (currentUsage <= budgetBytes) break;
            currentUsage -= cell->estimatedSizeBytes;
            EvictCell(*cell, activeScene);
        }
    }
}

void WorldPartitionManager::ForceLoadCell(const GridCellCoord& coord, Scene& activeScene) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(coord);
    if (it != m_cells.end()) {
        ActivateCell(it->second, activeScene);
    }
}

void WorldPartitionManager::ForceUnloadCell(const GridCellCoord& coord, Scene& activeScene) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(coord);
    if (it != m_cells.end()) {
        EvictCell(it->second, activeScene);
    }
}

void WorldPartitionManager::ForceLoadAll(Scene& activeScene) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_cells) {
        ActivateCell(pair.second, activeScene);
    }
}

bool WorldPartitionManager::IsCellLoaded(const GridCellCoord& coord) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(coord);
    if (it != m_cells.end()) {
        return it->second.state == CellState::Loaded || it->second.state == CellState::Active;
    }
    return false;
}

bool WorldPartitionManager::IsCellActive(const GridCellCoord& coord) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(coord);
    if (it != m_cells.end()) {
        return it->second.state == CellState::Active;
    }
    return false;
}

const WorldPartitionCell* WorldPartitionManager::GetCell(const GridCellCoord& coord) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(coord);
    if (it != m_cells.end()) {
        return &it->second;
    }
    return nullptr;
}

PartitionStats WorldPartitionManager::GetStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    PartitionStats stats;
    stats.totalRegisteredCells = static_cast<uint32_t>(m_cells.size());
    stats.queuedIORequests = static_cast<uint32_t>(DirectStorageQueue::GetInstance().GetPendingCount());
    stats.readThroughputMBs = DirectStorageQueue::GetInstance().GetThroughputMBs();
    stats.totalBytesStreamed = DirectStorageQueue::GetInstance().GetTotalBytesRead();

    for (const auto& pair : m_cells) {
        if (pair.second.state == CellState::Active) {
            stats.activeCellsCount++;
            stats.activeStreamedNodes += static_cast<uint32_t>(pair.second.nodes.size());
            stats.currentMemoryUsageBytes += pair.second.estimatedSizeBytes;
        } else if (pair.second.state == CellState::Loaded) {
            stats.loadedCellsCount++;
            stats.currentMemoryUsageBytes += pair.second.estimatedSizeBytes;
        }
    }

    return stats;
}

} // namespace Streaming
