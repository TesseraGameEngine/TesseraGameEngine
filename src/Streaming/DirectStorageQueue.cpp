#include "DirectStorageQueue.h"
#include <fstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Streaming {

DirectStorageQueue& DirectStorageQueue::GetInstance() {
    static DirectStorageQueue instance;
    return instance;
}

DirectStorageQueue::DirectStorageQueue() {
    m_lastThroughputCheck = std::chrono::steady_clock::now();
}

DirectStorageQueue::~DirectStorageQueue() {
    Shutdown();
}

void DirectStorageQueue::Initialize(size_t workerThreadCount) {
    if (m_running.load()) {
        return;
    }

    m_running.store(true);
    m_lastThroughputCheck = std::chrono::steady_clock::now();
    m_bytesInCurrentWindow = 0;
    m_cachedThroughputMBs.store(0.0f);

    size_t count = workerThreadCount > 0 ? workerThreadCount : 2;
    for (size_t i = 0; i < count; ++i) {
        m_workers.emplace_back(&DirectStorageQueue::WorkerLoop, this);
    }
}

void DirectStorageQueue::Shutdown() {
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    m_queueCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_pendingQueue.empty()) {
        m_pendingQueue.pop();
    }
}

uint64_t DirectStorageQueue::EnqueueRequest(DirectStorageRequest request) {
    if (!m_running.load()) {
        Initialize();
    }

    if (request.requestId == 0) {
        request.requestId = m_nextRequestId.fetch_add(1);
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingQueue.push(request);
    }
    m_queueCv.notify_one();

    return request.requestId;
}

bool DirectStorageQueue::CancelRequest(uint64_t requestId) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::vector<DirectStorageRequest> remaining;
    bool found = false;

    while (!m_pendingQueue.empty()) {
        DirectStorageRequest req = m_pendingQueue.top();
        m_pendingQueue.pop();
        if (req.requestId == requestId) {
            found = true;
        } else {
            remaining.push_back(req);
        }
    }

    for (const auto& req : remaining) {
        m_pendingQueue.push(req);
    }

    return found;
}

void DirectStorageQueue::CancelCellRequests(GridCellCoord cell) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::vector<DirectStorageRequest> remaining;

    while (!m_pendingQueue.empty()) {
        DirectStorageRequest req = m_pendingQueue.top();
        m_pendingQueue.pop();
        if (!(req.cell == cell)) {
            remaining.push_back(req);
        }
    }

    for (const auto& req : remaining) {
        m_pendingQueue.push(req);
    }
}

size_t DirectStorageQueue::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_pendingQueue.size();
}

float DirectStorageQueue::GetThroughputMBs() const {
    return m_cachedThroughputMBs.load();
}

uint64_t DirectStorageQueue::GetTotalBytesRead() const {
    return m_totalBytesRead.load();
}

void DirectStorageQueue::RecordReadBytes(size_t bytes) {
    m_totalBytesRead.fetch_add(bytes);

    std::lock_guard<std::mutex> lock(m_throughputMutex);
    m_bytesInCurrentWindow += bytes;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastThroughputCheck).count();
    if (elapsedMs >= 500) {
        float seconds = static_cast<float>(elapsedMs) / 1000.0f;
        float mb = static_cast<float>(m_bytesInCurrentWindow) / (1024.0f * 1024.0f);
        m_cachedThroughputMBs.store(mb / seconds);
        m_bytesInCurrentWindow = 0;
        m_lastThroughputCheck = now;
    }
}

std::vector<uint8_t> DirectStorageQueue::ReadFileSync(const std::string& filePath, uint64_t offset, uint64_t size) {
    if (filePath.empty()) {
        return {};
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    if (offset >= fileSize) {
        return {};
    }

    uint64_t readSize = (size > 0 && (offset + size <= fileSize)) ? size : (fileSize - offset);
    std::vector<uint8_t> buffer(static_cast<size_t>(readSize));

    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(readSize));

    RecordReadBytes(buffer.size());
    return buffer;
}

void DirectStorageQueue::WorkerLoop() {
    while (m_running.load()) {
        DirectStorageRequest currentRequest;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                return !m_running.load() || !m_pendingQueue.empty();
            });

            if (!m_running.load() && m_pendingQueue.empty()) {
                break;
            }

            if (!m_pendingQueue.empty()) {
                currentRequest = m_pendingQueue.top();
                m_pendingQueue.pop();
            } else {
                continue;
            }
        }

        // Process request
        std::vector<uint8_t> data;
        if (!currentRequest.filePath.empty()) {
            data = ReadFileSync(currentRequest.filePath, currentRequest.fileOffset, currentRequest.fileSize);
        } else if (currentRequest.fileSize > 0) {
            // Simulated / memory test request
            data.resize(static_cast<size_t>(currentRequest.fileSize), 0xAA);
            RecordReadBytes(data.size());
        }

        if (currentRequest.onComplete) {
            currentRequest.onComplete(std::move(data));
        }
    }
}

} // namespace Streaming

