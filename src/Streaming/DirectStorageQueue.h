#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include "StreamingTypes.h"

namespace Streaming {

class DirectStorageQueue {
public:
    static DirectStorageQueue& GetInstance();

    void Initialize(size_t workerThreadCount = 2);
    void Shutdown();

    // Enqueue an asynchronous I/O request
    uint64_t EnqueueRequest(DirectStorageRequest request);

    // Cancel pending requests
    bool CancelRequest(uint64_t requestId);
    void CancelCellRequests(GridCellCoord cell);

    // Throughput & Telemetry
    size_t GetPendingCount() const;
    float GetThroughputMBs() const;
    uint64_t GetTotalBytesRead() const;
    bool IsRunning() const { return m_running; }

    // DirectStorage Unbuffered Direct I/O
    bool IsDirectStorageUnbufferedActive() const { return m_unbufferedEnabled; }
    void SetDirectStorageUnbufferedEnabled(bool enabled) { m_unbufferedEnabled = enabled; }

    // Synchronous immediate read fallback (e.g., for Critical stall avoidance)
    std::vector<uint8_t> ReadFileSync(const std::string& filePath, uint64_t offset = 0, uint64_t size = 0);

private:
    DirectStorageQueue();
    ~DirectStorageQueue();

    DirectStorageQueue(const DirectStorageQueue&) = delete;
    DirectStorageQueue& operator=(const DirectStorageQueue&) = delete;

    void WorkerLoop();
    void RecordReadBytes(size_t bytes);

    struct RequestComparator {
        bool operator()(const DirectStorageRequest& a, const DirectStorageRequest& b) const {
            // Lower numerical priority value = higher urgency (Critical: 0 < High: 1 ...)
            if (a.priority != b.priority) {
                return static_cast<uint8_t>(a.priority) > static_cast<uint8_t>(b.priority);
            }
            return a.requestId > b.requestId; // FIFO for equal priority
        }
    };

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_unbufferedEnabled{true};
    std::atomic<uint64_t> m_nextRequestId{1};
    std::vector<std::thread> m_workers;

    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::priority_queue<DirectStorageRequest, std::vector<DirectStorageRequest>, RequestComparator> m_pendingQueue;

    // Throughput tracking
    mutable std::mutex m_throughputMutex;
    std::atomic<uint64_t> m_totalBytesRead{0};
    std::chrono::steady_clock::time_point m_lastThroughputCheck;
    std::atomic<float> m_cachedThroughputMBs{0.0f};
    size_t m_bytesInCurrentWindow{0};
};

} // namespace Streaming

