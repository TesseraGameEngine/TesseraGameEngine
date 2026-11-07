#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace Streaming {

// 2D Spatial Cell Coordinates on the XZ ground plane
struct GridCellCoord {
    int32_t x{0};
    int32_t z{0};

    bool operator==(const GridCellCoord& other) const {
        return x == other.x && z == other.z;
    }

    bool operator!=(const GridCellCoord& other) const {
        return !(*this == other);
    }
};

struct GridCellHash {
    size_t operator()(const GridCellCoord& c) const {
        // Hash combine 32-bit coordinates
        uint64_t ux = static_cast<uint32_t>(c.x);
        uint64_t uz = static_cast<uint32_t>(c.z);
        return static_cast<size_t>((ux << 32) | uz);
    }
};

enum class CellState {
    Unloaded = 0,    // Purely on disk
    Queued = 1,      // In asynchronous I/O queue
    Loading = 2,     // Currently reading from disk
    Loaded = 3,      // Decompressed in RAM, ready for activation
    Active = 4,      // Nodes live in scene graph & physics world
    Unloading = 5    // Evicting from memory
};

enum class StreamingPriority : uint8_t {
    Critical = 0,    // Immediate camera vicinity (< 50m)
    High = 1,        // Within camera frustum view cone
    Normal = 2,      // Within loading radius
    Low = 3          // Speculative prefetch
};

struct DirectStorageRequest {
    uint64_t requestId{0};
    GridCellCoord cell{0, 0};
    std::string filePath{""};
    uint64_t fileOffset{0};
    uint64_t fileSize{0};
    StreamingPriority priority{StreamingPriority::Normal};
    std::function<void(std::vector<uint8_t>)> onComplete;
};

struct PartitionSettings {
    bool enableStreaming{true};              // Master toggle for World Partitioning
    float cellSize{64.0f};                   // Width & length of spatial sector (meters)
    float activeRadius{192.0f};              // Distance to attach nodes to active scene (meters)
    float loadingRadius{256.0f};             // Distance to trigger background I/O read (meters)
    float unloadRadius{384.0f};              // Distance to evict from memory (meters)
    size_t maxMemoryBudgetMB{1024};          // Maximum RAM/VRAM streaming budget (1 GB)
    bool enableDirectStorageFastPath{true};  // Windows DirectStorage / unbuffered overlapped I/O
    bool enableFrustumPriority{true};        // Elevate cells inside camera frustum to High priority
    bool debugDrawGrid{false};               // Draw spatial cell boundaries in viewport
};

struct PartitionStats {
    uint32_t totalRegisteredCells{0};
    uint32_t activeCellsCount{0};
    uint32_t loadedCellsCount{0};
    uint32_t queuedIORequests{0};
    float readThroughputMBs{0.0f};
    size_t currentMemoryUsageBytes{0};
    uint64_t totalBytesStreamed{0};
    uint32_t activeStreamedNodes{0};
};

} // namespace Streaming

