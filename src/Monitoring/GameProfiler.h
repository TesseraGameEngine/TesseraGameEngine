#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

class Scene;
class Node;
namespace Gameplay { class GameLoop; }
namespace Networking { class NetworkManager; }

namespace Monitoring {

struct OrphanNodeInfo {
    uint32_t id{0};
    std::string name;
    uint32_t missingParentId{0};
};

struct GameResourceStats {
    // Memory
    size_t ramWorkingSetBytes{0};
    size_t ramPeakWorkingSetBytes{0};
    size_t ramCommittedBytes{0};
    size_t vramEstimatedBytes{0};
    size_t sceneHeapEstimatedBytes{0};

    // CPU
    float cpuUsagePercent{0.0f};
    float gameFrameTimeMs{0.0f};
    float gameFps{0.0f};
    float gameTickTimeMs{0.0f};

    // Disk I/O
    uint64_t diskReadBytesPerSec{0};
    uint64_t diskWriteBytesPerSec{0};
    uint64_t totalDiskReadBytes{0};
    uint64_t totalDiskWriteBytes{0};

    // Network
    uint64_t netBytesSentPerSec{0};
    uint64_t netBytesRecvPerSec{0};
    uint32_t netPacketsSent{0};
    uint32_t netPacketsRecv{0};
    float netPingMs{0.0f};
    uint32_t netActivePeers{0};

    // Nodes & Objects
    uint32_t totalNodes{0};
    uint32_t meshNodes{0};
    uint32_t lightNodes{0};
    uint32_t cameraNodes{0};
    uint32_t audioNodes{0};
    uint32_t particleNodes{0};
    uint32_t uiNodes{0};
    uint32_t colliderNodes{0};
    uint32_t characterNodes{0};
    uint32_t terrainNodes{0};
    uint32_t activeEntities{0};

    // Orphan Nodes
    uint32_t orphanNodeCount{0};
    std::vector<OrphanNodeInfo> orphanNodes;

    // Physics
    uint32_t activeDynamicBodies{0};
    uint32_t staticBodies{0};
    uint32_t activeJoints{0};
    uint32_t groundedCharacters{0};
    float physicsStepTimeMs{0.0f};

    // Rendering (Game Camera)
    uint32_t gameDrawCalls{0};
    uint64_t gameTrianglesCount{0};
    uint64_t gameVerticesCount{0};
    uint32_t activeLightsEvaluated{0};
    uint32_t shadowPassesActive{0};
    glm::vec2 gameViewportResolution{1280.0f, 720.0f};
};

struct CPUStageTiming {
    std::string name;
    float durationMs{0.0f};
    float percentage{0.0f};
    float minMs{9999.0f};
    float maxMs{0.0f};
    float avgMs{0.0f};
    uint32_t color{0xFFFFFFFF};
};

struct GPUPassTiming {
    std::string passName;
    float estimatedDurationMs{0.0f}; // measured (GPU timestamps), smoothed
    float percentage{0.0f};
    std::string description;
    uint32_t color{0xFFFFFFFF};
};

class GameProfiler {
public:
    static GameProfiler& Get();

    GameProfiler();
    ~GameProfiler() = default;

    // Called once per game loop tick during Play Mode
    void Update(Scene* playScene, Gameplay::GameLoop* gameLoop, Networking::NetworkManager* netManager, float deltaTime);

    // Timing markers for game simulation stages
    void BeginStage(const std::string& stageName);
    void EndStage(const std::string& stageName);

    // Rendering metrics feed from Game Camera render pass
    void RecordGameRenderMetrics(uint32_t drawCalls, uint64_t triangles, uint64_t vertices,
                                uint32_t lightsCount, uint32_t shadowPasses, glm::vec2 resolution);

    // Accessors - returns smoothed & latched stats for rock-solid, crisp and legible UI text
    const GameResourceStats& GetStats() const { return m_displayStats; }
    const GameResourceStats& GetRawStats() const { return m_stats; }
    const std::vector<CPUStageTiming>& GetCPUStages() const { return m_displayCpuStages; }
    const std::vector<GPUPassTiming>& GetGPUPasses() const { return m_displayGpuPasses; }
    // The game view's passes as the GPU timed them (main.cpp passes them on each frame)
    void SetMeasuredGPUPasses(std::vector<GPUPassTiming> passes);
    const std::vector<GPUPassTiming>& GetMeasuredGPUPasses() const { return m_measuredGpuPasses; }
    const std::vector<float>& GetFrameTimeHistory() const { return m_frameTimeHistory; }

    bool IsGameRunning() const { return m_isGameRunning; }
    void SetGameRunning(bool running) { m_isGameRunning = running; }
    void Reset();

    // Scene orphan nodes fixer: reparents orphaned nodes to root (parentId = 0)
    void ReparentOrphanNodesToRoot(Scene* playScene);

private:
    void SampleSystemResources(float dt);
    void AuditSceneNodesAndOrphans(Scene* playScene);
    void CollectPhysicsStats(Gameplay::GameLoop* gameLoop);
    void CollectNetworkStats(Networking::NetworkManager* netManager);
    void ComputeCPUProfilings(float dt);
    void ComputeGPUProfilings(float dt);

    GameResourceStats m_stats;
    GameResourceStats m_displayStats;
    bool m_isGameRunning{false};

    // Rolling history for plot graphs (120 frames)
    static constexpr size_t HISTORY_SAMPLES = 120;
    std::vector<float> m_frameTimeHistory;
    size_t m_historyIndex{0};

    std::vector<CPUStageTiming> m_cpuStages;
    std::vector<CPUStageTiming> m_displayCpuStages;
    std::vector<GPUPassTiming> m_gpuPasses;
    std::vector<GPUPassTiming> m_displayGpuPasses;
    std::vector<GPUPassTiming> m_measuredGpuPasses;

    // Display refresh throttling: refreshes UI text numbers ~3 times per second (300ms)
    // so numbers are completely crisp, stable, and legible without blurring or vibrating.
    float m_displayUpdateTimer{0.0f};
    static constexpr float DISPLAY_UPDATE_INTERVAL = 0.30f;

    // Internal timing map
    struct StageTimestamp {
        std::chrono::high_resolution_clock::time_point startTime;
        float accumulatedMs{0.0f};
        float minMs{999.0f};
        float maxMs{0.0f};
        float runningAvgMs{0.0f};
        uint32_t sampleCount{0};
    };
    std::vector<std::pair<std::string, StageTimestamp>> m_stageTimers;

    // System I/O & CPU tracking state
    uint64_t m_lastDiskReadBytes{0};
    uint64_t m_lastDiskWriteBytes{0};
    uint64_t m_lastNetBytesSent{0};
    uint64_t m_lastNetBytesRecv{0};
    uint64_t m_lastProcessCpuTime{0};
    uint64_t m_lastSystemTime{0};
    float m_sampleTimer{0.0f};
};

} // namespace Monitoring

