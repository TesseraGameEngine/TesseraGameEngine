#pragma once

#include <string>
#include <algorithm>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>
#include "Scene/Scene.h"

namespace Networking {

enum class SocketProtocol {
    UDP = 0,
    TCP = 1,
    LoopbackMemory = 2
};

enum class PacketType : uint8_t {
    StateReplication = 1,
    RPCInvoke = 2,
    Ping = 3,
    Pong = 4,
    ClientHello = 5,
    ServerWelcome = 6,
    DisconnectNotice = 7,
    DeclarativePropertyReplication = 8
};

struct ReplicatedNodeState {
    uint32_t networkId{0};
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 linearVelocity{0.0f};
};

struct ClientPeerInfo {
    std::string address{"127.0.0.1"};
    uint16_t port{0};
    float lastSeenSeconds{0.0f};
};

// Cross-platform / Winsock2 + Loopback Socket Abstraction
class NetSocket {
public:
    NetSocket(SocketProtocol protocol = SocketProtocol::UDP);
    ~NetSocket();

    NetSocket(const NetSocket&) = delete;
    NetSocket& operator=(const NetSocket&) = delete;
    NetSocket(NetSocket&& other) noexcept;
    NetSocket& operator=(NetSocket&& other) noexcept;

    bool Bind(const std::string& address, uint16_t port);
    bool Connect(const std::string& address, uint16_t port);
    void Close();
    bool IsOpen() const { return m_isOpen; }
    SocketProtocol GetProtocol() const { return m_protocol; }
    const std::string& GetRemoteAddress() const { return m_remoteAddress; }
    uint16_t GetRemotePort() const { return m_remotePort; }

    int Send(const uint8_t* data, size_t size);
    int SendTo(const uint8_t* data, size_t size, const std::string& address, uint16_t port);
    int Receive(std::vector<uint8_t>& outBuffer, size_t maxBytes = 4096);
    int ReceiveFrom(std::vector<uint8_t>& outBuffer, std::string& outFromAddress, uint16_t& outFromPort, bool& outConnReset, size_t maxBytes = 4096);

    // Pair two LoopbackMemory sockets for zero-config deterministic testing
    static void LinkLoopbackPair(NetSocket& a, NetSocket& b);

private:
    SocketProtocol m_protocol;
    uintptr_t m_socketHandle{static_cast<uintptr_t>(~0ULL)};
    bool m_isOpen{false};
    std::string m_remoteAddress{"127.0.0.1"};
    uint16_t m_remotePort{0};

    // Loopback queue
    NetSocket* m_loopbackPeer{nullptr};
    std::vector<std::vector<uint8_t>> m_inboxQueue;
};

class NetworkManager {
public:
    using RPCHandler = std::function<void(uint32_t targetNetworkId, const std::vector<std::string>& args)>;

    NetworkManager();
    ~NetworkManager();

    bool StartServer(uint16_t port, SocketProtocol protocol = SocketProtocol::UDP);
    bool ConnectClient(const std::string& host, uint16_t port, SocketProtocol protocol = SocketProtocol::UDP);
    void Disconnect();

    bool IsServer() const { return m_isServer; }
    bool IsConnected() const { return m_isConnected; }
    bool IsConnecting() const { return m_isConnecting; }

    static constexpr uint32_t SERVER_PEER_ID = 1;
    uint32_t GetLocalPeerId() const { return m_localPeerId; }
    void SetLocalPeerId(uint32_t peerId) { m_localPeerId = peerId; }

    NetSocket& GetSocket() { return m_socket; }

    // Per-frame network pump: handles handshakes, heartbeats, timeouts, and incoming packets
    void Update(Scene& scene, float deltaTime);

    // 1. Client-Server State Replication
    std::vector<uint8_t> BuildStateReplicationPacket(const Scene& scene);
    bool ApplyStateReplicationPacket(Scene& scene,
                                     const uint8_t* data,
                                     size_t size,
                                     float interpolationAlpha = 1.0f);

    // Broadcasts current scene replicated state over the active socket
    bool ReplicateSceneState(const Scene& scene, bool verboseLog = true);

    // 1b. Declarative Multiplayer Property Sync
    std::vector<uint8_t> BuildDeclarativeReplicationPacket(Scene& scene, float currentTime);
    bool ApplyDeclarativeReplicationPacket(Scene& scene, const uint8_t* data, size_t size);
    bool SyncDeclarativeProperties(Scene& scene, float currentTime);

    // 2. Remote Procedure Calls (RPCs)
    void RegisterRPC(const std::string& rpcName, RPCHandler handler);
    std::vector<uint8_t> BuildRPCPacket(const std::string& rpcName,
                                        uint32_t targetNetworkId,
                                        const std::vector<std::string>& args = {}) const;

    // Sends an RPC over the active socket (and executes locally if executeLocal is true)
    bool SendRPC(const std::string& rpcName,
                 uint32_t targetNetworkId,
                 const std::vector<std::string>& args = {},
                 bool executeLocal = false);

    // Processes any raw incoming packet (StateReplication or RPCInvoke)
    bool ProcessPacket(Scene& scene, const uint8_t* data, size_t size, float interpolationAlpha = 1.0f);

    // Polls the underlying socket and processes all pending packets
    size_t PollIncomingPackets(Scene& scene, float interpolationAlpha = 1.0f);

    uint32_t GetLastReplicatedTick() const { return m_replicationTick; }
    bool IsContinuousReplicationEnabled() const { return m_continuousReplication; }
    void SetContinuousReplication(bool enable) { m_continuousReplication = enable; }
    float GetReplicationTickRate() const { return m_replicationTickRate; }
    void SetReplicationTickRate(float hz) {
        m_replicationTickRate = std::clamp(hz, 1.0f, 120.0f);
        m_replicationInterval = 1.0f / m_replicationTickRate;
    }
    bool GetReplicateAllNodes() const { return m_replicateAllNodes; }
    void SetReplicateAllNodes(bool enable) { m_replicateAllNodes = enable; }
    uint32_t GetReplicatedPacketsSentCount() const { return m_replicatedPacketsSentCount; }
    uint32_t GetReplicatedPacketsRecvCount() const { return m_replicatedPacketsRecvCount; }

    // Telemetry & Monitoring Accessors
    uint64_t GetBytesSentPerSec() const { return m_bytesSentPerSec; }
    uint64_t GetBytesRecvPerSec() const { return m_bytesRecvPerSec; }
    uint32_t GetPacketsSentCount() const { return m_replicatedPacketsSentCount; }
    uint32_t GetPacketsRecvCount() const { return m_replicatedPacketsRecvCount; }
    float GetPingMs() const { return m_lastPingMs; }
    uint32_t GetConnectedPeersCount() const { return m_isServer ? static_cast<uint32_t>(m_connectedClients.size()) : (m_isConnected ? 1 : 0); }

    // Session Peers & Event Logging
    const std::vector<ClientPeerInfo>& GetConnectedClients() const { return m_connectedClients; }
    const std::vector<std::string>& GetEventLog() const { return m_eventLog; }
    void ClearNetworkLog() { m_eventLog.clear(); }
    void AddLog(const std::string& msg);

private:
    NetSocket m_socket;
    uint32_t m_localPeerId{1};
    bool m_isServer{false};
    bool m_isConnected{false};
    bool m_isConnecting{false};
    uint32_t m_replicationTick{0};
    float m_sessionElapsed{0.0f};
    float m_lastHeartbeatSent{0.0f};
    float m_lastServerPacketReceived{0.0f};
    bool m_continuousReplication{true};
    bool m_replicateAllNodes{true};
    float m_replicationTickRate{30.0f};
    float m_replicationInterval{1.0f / 30.0f};
    float m_timeSinceLastReplication{0.0f};
    uint32_t m_replicatedPacketsSentCount{0};
    uint32_t m_replicatedPacketsRecvCount{0};
    uint64_t m_bytesSentPerSec{0};
    uint64_t m_bytesRecvPerSec{0};
    uint64_t m_accumBytesSent{0};
    uint64_t m_accumBytesRecv{0};
    float m_rateCalcTimer{0.0f};
    float m_lastPingMs{0.0f};
    std::vector<ClientPeerInfo> m_connectedClients;
    std::vector<std::string> m_eventLog;
    std::map<std::string, RPCHandler> m_rpcHandlers;
};

} // namespace Networking
