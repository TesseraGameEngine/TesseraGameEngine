#include "NetworkManager.h"
#include "Scripting/LuaScriptSystem.h"
#include "Physics/PhysicsWorld.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace Networking {

namespace {
#ifdef _WIN32
std::once_flag g_winsockInitFlag;
void EnsureWinsockInitialized() {
    std::call_once(g_winsockInitFlag, []() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    });
}
#endif

void WriteU32(std::vector<uint8_t>& buf, uint32_t val) {
    uint8_t b[4];
    std::memcpy(b, &val, 4);
    buf.insert(buf.end(), b, b + 4);
}

void WriteFloat(std::vector<uint8_t>& buf, float val) {
    uint8_t b[4];
    std::memcpy(b, &val, 4);
    buf.insert(buf.end(), b, b + 4);
}

void WriteString(std::vector<uint8_t>& buf, const std::string& str) {
    WriteU32(buf, static_cast<uint32_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

bool ReadU32(const uint8_t* data, size_t size, size_t& offset, uint32_t& outVal) {
    if (offset + 4 > size) return false;
    std::memcpy(&outVal, data + offset, 4);
    offset += 4;
    return true;
}

bool ReadFloat(const uint8_t* data, size_t size, size_t& offset, float& outVal) {
    if (offset + 4 > size) return false;
    std::memcpy(&outVal, data + offset, 4);
    offset += 4;
    return true;
}

bool ReadString(const uint8_t* data, size_t size, size_t& offset, std::string& outStr) {
    uint32_t len = 0;
    if (!ReadU32(data, size, offset, len)) return false;
    if (offset + len > size) return false;
    outStr.assign(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return true;
}
} // namespace

// ============================================================================
// NetSocket
// ============================================================================
NetSocket::NetSocket(SocketProtocol protocol) : m_protocol(protocol) {}

NetSocket::~NetSocket() {
    Close();
}

NetSocket::NetSocket(NetSocket&& other) noexcept
    : m_protocol(other.m_protocol),
      m_socketHandle(other.m_socketHandle),
      m_isOpen(other.m_isOpen),
      m_remoteAddress(std::move(other.m_remoteAddress)),
      m_remotePort(other.m_remotePort),
      m_loopbackPeer(other.m_loopbackPeer),
      m_inboxQueue(std::move(other.m_inboxQueue)) {
    other.m_socketHandle = static_cast<uintptr_t>(~0ULL);
    other.m_isOpen = false;
    other.m_loopbackPeer = nullptr;
    if (m_loopbackPeer) {
        m_loopbackPeer->m_loopbackPeer = this;
    }
}

NetSocket& NetSocket::operator=(NetSocket&& other) noexcept {
    if (this != &other) {
        Close();
        m_protocol = other.m_protocol;
        m_socketHandle = other.m_socketHandle;
        m_isOpen = other.m_isOpen;
        m_remoteAddress = std::move(other.m_remoteAddress);
        m_remotePort = other.m_remotePort;
        m_loopbackPeer = other.m_loopbackPeer;
        m_inboxQueue = std::move(other.m_inboxQueue);
        other.m_socketHandle = static_cast<uintptr_t>(~0ULL);
        other.m_isOpen = false;
        other.m_loopbackPeer = nullptr;
        if (m_loopbackPeer) {
            m_loopbackPeer->m_loopbackPeer = this;
        }
    }
    return *this;
}

void NetSocket::LinkLoopbackPair(NetSocket& a, NetSocket& b) {
    a.Close();
    b.Close();
    a.m_protocol = SocketProtocol::LoopbackMemory;
    b.m_protocol = SocketProtocol::LoopbackMemory;
    a.m_loopbackPeer = &b;
    b.m_loopbackPeer = &a;
    a.m_isOpen = true;
    b.m_isOpen = true;
}

bool NetSocket::Bind(const std::string& address, uint16_t port) {
    Close();
    if (m_protocol == SocketProtocol::LoopbackMemory) {
        m_isOpen = true;
        return true;
    }

#ifdef _WIN32
    EnsureWinsockInitialized();
    int type = (m_protocol == SocketProtocol::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (m_protocol == SocketProtocol::TCP) ? IPPROTO_TCP : IPPROTO_UDP;
    SOCKET s = socket(AF_INET, type, proto);
    if (s == INVALID_SOCKET) return false;

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (address.empty() || address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
    }

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }
    if (m_protocol == SocketProtocol::TCP) {
        listen(s, SOMAXCONN);
    }

    m_socketHandle = static_cast<uintptr_t>(s);
    m_isOpen = true;
    return true;
#else
    return false;
#endif
}

bool NetSocket::Connect(const std::string& address, uint16_t port) {
    m_remoteAddress = address.empty() ? "127.0.0.1" : address;
    m_remotePort = port;

    if (m_protocol == SocketProtocol::LoopbackMemory) {
        m_isOpen = true;
        return true;
    }

#ifdef _WIN32
    EnsureWinsockInitialized();
    Close();
    m_remoteAddress = address.empty() ? "127.0.0.1" : address;
    m_remotePort = port;

    int type = (m_protocol == SocketProtocol::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (m_protocol == SocketProtocol::TCP) ? IPPROTO_TCP : IPPROTO_UDP;
    SOCKET s = socket(AF_INET, type, proto);
    if (s == INVALID_SOCKET) return false;

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    if (m_protocol == SocketProtocol::UDP) {
        // Bind to an ephemeral local port so recvfrom can receive server replies immediately
        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(0);
        localAddr.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR) {
            closesocket(s);
            return false;
        }
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, m_remoteAddress.c_str(), &addr.sin_addr);
        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    m_socketHandle = static_cast<uintptr_t>(s);
    m_isOpen = true;
    return true;
#else
    return false;
#endif
}

void NetSocket::Close() {
    if (m_protocol == SocketProtocol::LoopbackMemory) {
        if (m_loopbackPeer && m_loopbackPeer->m_loopbackPeer == this) {
            m_loopbackPeer->m_loopbackPeer = nullptr;
        }
        m_loopbackPeer = nullptr;
        m_inboxQueue.clear();
        m_isOpen = false;
        return;
    }

#ifdef _WIN32
    if (m_socketHandle != static_cast<uintptr_t>(~0ULL)) {
        closesocket(static_cast<SOCKET>(m_socketHandle));
        m_socketHandle = static_cast<uintptr_t>(~0ULL);
    }
#endif
    m_isOpen = false;
}

int NetSocket::SendTo(const uint8_t* data, size_t size, const std::string& address, uint16_t port) {
    if (!m_isOpen || !data || size == 0) return -1;

    if (m_protocol == SocketProtocol::LoopbackMemory) {
        if (!m_loopbackPeer) return -1;
        m_loopbackPeer->m_inboxQueue.emplace_back(data, data + size);
        return static_cast<int>(size);
    }

#ifdef _WIN32
    SOCKET s = static_cast<SOCKET>(m_socketHandle);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, address.empty() ? "127.0.0.1" : address.c_str(), &addr.sin_addr);
    return sendto(s, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
#else
    return -1;
#endif
}

int NetSocket::Send(const uint8_t* data, size_t size) {
    if (!m_isOpen || !data || size == 0) return -1;

    if (m_protocol == SocketProtocol::LoopbackMemory) {
        if (!m_loopbackPeer) return -1;
        m_loopbackPeer->m_inboxQueue.emplace_back(data, data + size);
        return static_cast<int>(size);
    }

    if (m_protocol == SocketProtocol::UDP && m_remotePort != 0) {
        return SendTo(data, size, m_remoteAddress, m_remotePort);
    }

#ifdef _WIN32
    SOCKET s = static_cast<SOCKET>(m_socketHandle);
    return send(s, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
#else
    return -1;
#endif
}

int NetSocket::ReceiveFrom(std::vector<uint8_t>& outBuffer, std::string& outFromAddress, uint16_t& outFromPort, bool& outConnReset, size_t maxBytes) {
    outConnReset = false;
    if (!m_isOpen) return -1;

    if (m_protocol == SocketProtocol::LoopbackMemory) {
        if (m_inboxQueue.empty()) return 0;
        outBuffer = std::move(m_inboxQueue.front());
        m_inboxQueue.erase(m_inboxQueue.begin());
        outFromAddress = "127.0.0.1";
        outFromPort = 0;
        return static_cast<int>(outBuffer.size());
    }

#ifdef _WIN32
    outBuffer.resize(maxBytes);
    SOCKET s = static_cast<SOCKET>(m_socketHandle);
    sockaddr_in fromAddr{};
    int fromLen = sizeof(fromAddr);
    int bytesRead = recvfrom(s, reinterpret_cast<char*>(outBuffer.data()), static_cast<int>(maxBytes), 0,
                             reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
    if (bytesRead > 0) {
        outBuffer.resize(static_cast<size_t>(bytesRead));
        char ipStr[INET_ADDRSTRLEN] = "127.0.0.1";
        inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));
        outFromAddress = ipStr;
        outFromPort = ntohs(fromAddr.sin_port);
        if (m_remotePort == 0) {
            m_remoteAddress = outFromAddress;
            m_remotePort = outFromPort;
        }
        return bytesRead;
    } else if (bytesRead == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAECONNRESET) {
            outConnReset = true;
        }
    }
    outBuffer.clear();
    return 0;
#else
    return -1;
#endif
}

int NetSocket::Receive(std::vector<uint8_t>& outBuffer, size_t maxBytes) {
    std::string fromAddr;
    uint16_t fromPort = 0;
    bool connReset = false;
    return ReceiveFrom(outBuffer, fromAddr, fromPort, connReset, maxBytes);
}

// ============================================================================
// NetworkManager
// ============================================================================
NetworkManager::NetworkManager() = default;

NetworkManager::~NetworkManager() {
    Disconnect();
}

void NetworkManager::AddLog(const std::string& msg) {
    std::cout << "[Network] " << msg << std::endl;
    m_eventLog.push_back(msg);
    if (m_eventLog.size() > 100) {
        m_eventLog.erase(m_eventLog.begin());
    }
}

bool NetworkManager::StartServer(uint16_t port, SocketProtocol protocol) {
    Disconnect();
    m_socket = NetSocket(protocol);
    if (!m_socket.Bind("0.0.0.0", port)) {
        AddLog("[Server] ERROR: Failed to bind UDP port " + std::to_string(port) + " (port already in use).");
        return false;
    }
    m_isServer = true;
    m_localPeerId = SERVER_PEER_ID;
    m_isConnected = true;
    m_isConnecting = false;
    m_sessionElapsed = 0.0f;
    m_lastHeartbeatSent = 0.0f;
    m_connectedClients.clear();
    m_replicatedPacketsSentCount = 0;
    m_replicatedPacketsRecvCount = 0;
    m_bytesSentPerSec = 0;
    m_bytesRecvPerSec = 0;
    m_accumBytesSent = 0;
    m_accumBytesRecv = 0;
    m_rateCalcTimer = 0.0f;
    m_lastPingMs = 0.0f;
    m_timeSinceLastReplication = 0.0f;
    AddLog("[Server] Started Host / Dedicated Server on UDP port " + std::to_string(port) + ". Waiting for clients...");
    return true;
}

bool NetworkManager::ConnectClient(const std::string& host, uint16_t port, SocketProtocol protocol) {
    Disconnect();
    m_socket = NetSocket(protocol);
    if (!m_socket.Connect(host, port)) {
        AddLog("[Client] ERROR: Failed to open UDP client socket for " + host + ":" + std::to_string(port));
        return false;
    }
    m_isServer = false;
    m_localPeerId = 2; // Default client peer ID 
    m_sessionElapsed = 0.0f;
    m_lastHeartbeatSent = 0.0f;
    m_lastServerPacketReceived = 0.0f;

    if (protocol == SocketProtocol::LoopbackMemory) {
        m_isConnected = true;
        m_isConnecting = false;
        AddLog("[Client] Connected via LoopbackMemory.");
    } else {
        m_isConnected = true;
        m_isConnecting = true;
        uint8_t hello = static_cast<uint8_t>(PacketType::ClientHello);
        m_socket.Send(&hello, 1);
        AddLog("[Client] Connecting to server " + host + ":" + std::to_string(port) + "...");
    }
    return true;
}

void NetworkManager::Disconnect() {
    if (m_socket.IsOpen() && (m_isConnected || m_isConnecting)) {
        uint8_t bye = static_cast<uint8_t>(PacketType::DisconnectNotice);
        if (m_isServer) {
            for (const auto& c : m_connectedClients) {
                m_socket.SendTo(&bye, 1, c.address, c.port);
            }
            AddLog("[Server] Stopped server (notified " + std::to_string(m_connectedClients.size()) + " client(s)).");
        } else {
            m_socket.Send(&bye, 1);
            AddLog("[Client] Disconnected from session.");
        }
    }
    m_socket.Close();
    m_isServer = false;
    m_isConnected = false;
    m_isConnecting = false;
    m_connectedClients.clear();
    m_replicatedPacketsSentCount = 0;
    m_replicatedPacketsRecvCount = 0;
    m_bytesSentPerSec = 0;
    m_bytesRecvPerSec = 0;
    m_accumBytesSent = 0;
    m_accumBytesRecv = 0;
    m_rateCalcTimer = 0.0f;
    m_lastPingMs = 0.0f;
    m_timeSinceLastReplication = 0.0f;
}

void NetworkManager::Update(Scene& scene, float deltaTime) {
    if (!m_socket.IsOpen() || (!m_isConnected && !m_isConnecting)) return;

    m_sessionElapsed += deltaTime;
    m_rateCalcTimer += deltaTime;
    if (m_rateCalcTimer >= 1.0f) {
        m_bytesSentPerSec = static_cast<uint64_t>(m_accumBytesSent / m_rateCalcTimer);
        m_bytesRecvPerSec = static_cast<uint64_t>(m_accumBytesRecv / m_rateCalcTimer);
        m_accumBytesSent = 0;
        m_accumBytesRecv = 0;
        m_rateCalcTimer = 0.0f;
    }

    // 1. Poll incoming datagrams
    std::vector<uint8_t> buf;
    std::string fromAddr;
    uint16_t fromPort = 0;
    bool connReset = false;

    while (true) {
        int bytes = m_socket.ReceiveFrom(buf, fromAddr, fromPort, connReset);
        if (connReset && !m_isServer) {
            // Windows Winsock ICMP Port Unreachable: server is not running or was shut down!
            AddLog("[Client] Disconnected: Server at " + m_socket.GetRemoteAddress() + ":" +
                   std::to_string(m_socket.GetRemotePort()) + " is offline or closed the port.");
            m_socket.Close();
            m_isConnected = false;
            m_isConnecting = false;
            return;
        }
        if (bytes <= 0) break;
        m_accumBytesRecv += static_cast<uint64_t>(bytes);

        PacketType type = static_cast<PacketType>(buf[0]);
        if (m_isServer) {
            // Find or register client peer
            auto it = std::find_if(m_connectedClients.begin(), m_connectedClients.end(),
                [&](const ClientPeerInfo& c) { return c.address == fromAddr && c.port == fromPort; });

            if (type == PacketType::DisconnectNotice) {
                if (it != m_connectedClients.end()) {
                    m_connectedClients.erase(it);
                    AddLog("[Server] Client disconnected: " + fromAddr + ":" + std::to_string(fromPort) +
                           " (Active clients: " + std::to_string(m_connectedClients.size()) + ")");
                }
                continue;
            }

            if (it == m_connectedClients.end() && fromPort != 0) {
                ClientPeerInfo info;
                info.address = fromAddr;
                info.port = fromPort;
                info.lastSeenSeconds = m_sessionElapsed;
                m_connectedClients.push_back(info);
                AddLog("[Server] Client connected: " + fromAddr + ":" + std::to_string(fromPort) +
                       " (Active clients: " + std::to_string(m_connectedClients.size()) + ")");
            } else if (it != m_connectedClients.end()) {
                it->lastSeenSeconds = m_sessionElapsed;
            }

            if (type == PacketType::ClientHello) {
                uint8_t welcome = static_cast<uint8_t>(PacketType::ServerWelcome);
                m_socket.SendTo(&welcome, 1, fromAddr, fromPort);
            } else if (type == PacketType::Ping) {
                uint8_t pong = static_cast<uint8_t>(PacketType::Pong);
                m_socket.SendTo(&pong, 1, fromAddr, fromPort);
            } else {
                ProcessPacket(scene, buf.data(), buf.size(), 1.0f);
                if (type == PacketType::StateReplication && m_connectedClients.size() > 1) {
                    for (const auto& c : m_connectedClients) {
                        if (c.address != fromAddr || c.port != fromPort) {
                            m_socket.SendTo(buf.data(), buf.size(), c.address, c.port);
                        }
                    }
                }
            }
        } else {
            // Client receiving from Server
            m_lastServerPacketReceived = m_sessionElapsed;
            if (type == PacketType::ServerWelcome) {
                if (m_isConnecting) {
                    m_isConnecting = false;
                    m_isConnected = true;
                    AddLog("[Client] Connected to Server " + fromAddr + ":" + std::to_string(fromPort) + "!");
                }
            } else if (type == PacketType::DisconnectNotice) {
                AddLog("[Client] Disconnected: Server " + fromAddr + ":" + std::to_string(fromPort) + " shut down.");
                m_socket.Close();
                m_isConnected = false;
                m_isConnecting = false;
                return;
            } else if (type == PacketType::Pong) {
                if (m_isConnecting) {
                    m_isConnecting = false;
                    m_isConnected = true;
                    AddLog("[Client] Connected to Server " + fromAddr + ":" + std::to_string(fromPort) + "!");
                }
            } else {
                ProcessPacket(scene, buf.data(), buf.size(), 1.0f);
            }
        }
    }

    // 2. Periodic Heartbeat & Timeout Checks (for UDP sockets)
    if (m_socket.GetProtocol() == SocketProtocol::UDP) {
        if (!m_isServer) {
            if (m_sessionElapsed - m_lastHeartbeatSent >= 0.4f) {
                m_lastHeartbeatSent = m_sessionElapsed;
                uint8_t pkt = static_cast<uint8_t>(m_isConnecting ? PacketType::ClientHello : PacketType::Ping);
                m_socket.Send(&pkt, 1);
            }
            // Timeout if server hasn't responded within 2.0s
            if (m_sessionElapsed - m_lastServerPacketReceived > 2.0f) {
                if (m_isConnecting) {
                    AddLog("[Client] Connection timed out: No server responded at " +
                           m_socket.GetRemoteAddress() + ":" + std::to_string(m_socket.GetRemotePort()));
                } else {
                    AddLog("[Client] Lost connection to server (heartbeat timed out).");
                }
                m_socket.Close();
                m_isConnected = false;
                m_isConnecting = false;
                return;
            }
        } else {
            // Prune timed-out clients on the server (> 3.0s without ping)
            for (auto it = m_connectedClients.begin(); it != m_connectedClients.end();) {
                if (m_sessionElapsed - it->lastSeenSeconds > 3.0f) {
                    AddLog("[Server] Client timed out: " + it->address + ":" + std::to_string(it->port) +
                           " (Active clients: " + std::to_string(m_connectedClients.size() - 1) + ")");
                    it = m_connectedClients.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // 3. Continuous bidirectional live state replication (30 Hz default)
    if (m_isConnected && !m_isConnecting && m_continuousReplication) {
        if ((m_isServer && !m_connectedClients.empty()) || !m_isServer) {
            m_timeSinceLastReplication += deltaTime;
            if (m_timeSinceLastReplication >= m_replicationInterval) {
                m_timeSinceLastReplication = 0.0f;
                ReplicateSceneState(scene, false);
                SyncDeclarativeProperties(scene, m_sessionElapsed);
            }
        }
    }
}

std::vector<uint8_t> NetworkManager::BuildStateReplicationPacket(const Scene& scene) {
    std::vector<ReplicatedNodeState> states;
    for (const auto& node : scene.GetNodes()) {
        if (!node) continue;
        if (node->nodeType == NodeType::Camera && node->name.find("Editor") != std::string::npos) continue;

        bool shouldReplicate = node->replicateTransform || m_replicateAllNodes ||
                               (node->rigidBodyType != RigidBodyType::None && node->rigidBodyType != RigidBodyType::Static) ||
                               !node->scriptPath.empty() || node->nodeType == NodeType::Character;
        if (!shouldReplicate) continue;

        // Multiplayer Authority:
        // A peer ONLY broadcasts state replication for nodes that it has authority over!
        // (If multiplayerAuthority == 0, fallback to democratic local active check)
        bool hasAuthority = (node->multiplayerAuthority == 0)
                            ? (node->IsLocallyActive() || m_isServer)
                            : (node->multiplayerAuthority == m_localPeerId);
        if (!hasAuthority) continue;

        ReplicatedNodeState st;
        st.networkId = (node->networkId != 0) ? node->networkId : node->id;
        st.position = node->position;
        st.rotation = node->rotation;
        st.scale = node->scale;
        st.linearVelocity = node->linearVelocity;
        states.push_back(st);
    }

    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(PacketType::StateReplication));
    WriteU32(packet, ++m_replicationTick);
    WriteU32(packet, static_cast<uint32_t>(states.size()));

    for (const auto& st : states) {
        WriteU32(packet, st.networkId);
        WriteFloat(packet, st.position.x);
        WriteFloat(packet, st.position.y);
        WriteFloat(packet, st.position.z);
        WriteFloat(packet, st.rotation.x);
        WriteFloat(packet, st.rotation.y);
        WriteFloat(packet, st.rotation.z);
        WriteFloat(packet, st.scale.x);
        WriteFloat(packet, st.scale.y);
        WriteFloat(packet, st.scale.z);
        WriteFloat(packet, st.linearVelocity.x);
        WriteFloat(packet, st.linearVelocity.y);
        WriteFloat(packet, st.linearVelocity.z);
    }
    return packet;
}

bool NetworkManager::ApplyStateReplicationPacket(Scene& scene,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 float interpolationAlpha) {
    if (!data || size < 9) return false;
    if (data[0] != static_cast<uint8_t>(PacketType::StateReplication)) return false;

    size_t offset = 1;
    uint32_t tick = 0, count = 0;
    if (!ReadU32(data, size, offset, tick)) return false;
    if (!ReadU32(data, size, offset, count)) return false;

    m_replicationTick = std::max(m_replicationTick, tick);
    float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);

    for (uint32_t i = 0; i < count; ++i) {
        ReplicatedNodeState st;
        if (!ReadU32(data, size, offset, st.networkId)) return false;
        if (!ReadFloat(data, size, offset, st.position.x) ||
            !ReadFloat(data, size, offset, st.position.y) ||
            !ReadFloat(data, size, offset, st.position.z) ||
            !ReadFloat(data, size, offset, st.rotation.x) ||
            !ReadFloat(data, size, offset, st.rotation.y) ||
            !ReadFloat(data, size, offset, st.rotation.z) ||
            !ReadFloat(data, size, offset, st.scale.x) ||
            !ReadFloat(data, size, offset, st.scale.y) ||
            !ReadFloat(data, size, offset, st.scale.z) ||
            !ReadFloat(data, size, offset, st.linearVelocity.x) ||
            !ReadFloat(data, size, offset, st.linearVelocity.y) ||
            !ReadFloat(data, size, offset, st.linearVelocity.z)) {
            return false;
        }

        std::shared_ptr<Node> matchedNode = nullptr;
        for (const auto& node : scene.GetNodes()) {
            if (!node) continue;
            uint32_t nodeNetId = (node->networkId != 0) ? node->networkId : node->id;
            if (nodeNetId == st.networkId) {
                matchedNode = node;
                break;
            }
        }
        // Fallback: if single candidate mesh/shape node exists in client scene, match it
        if (!matchedNode) {
            int candidateCount = 0;
            std::shared_ptr<Node> singleCandidate = nullptr;
            for (const auto& node : scene.GetNodes()) {
                if (node && (node->nodeType == NodeType::Mesh || node->nodeType == NodeType::Empty)) {
                    ++candidateCount;
                    singleCandidate = node;
                }
            }
            if (candidateCount == 1) {
                matchedNode = singleCandidate;
            }
        }
        // If still not found, auto-spawn the replicated entity on the client!
        if (!matchedNode) {
            matchedNode = scene.AddNode(ShapeType::Cube, "Entity_" + std::to_string(st.networkId));
            if (matchedNode) {
                matchedNode->networkId = st.networkId;
                matchedNode->replicateTransform = true;
            }
        }

        if (matchedNode) {
            // Multiplayer Authority:
            // If this peer has authority over this node, we do NOT let remote peers override our state!
            bool hasAuthority = (matchedNode->multiplayerAuthority == 0)
                                ? matchedNode->IsLocallyActive()
                                : (matchedNode->multiplayerAuthority == m_localPeerId);
            if (hasAuthority) {
                continue; // We are the authority, ignore incoming packets for this node!
            }

            matchedNode->position = glm::mix(matchedNode->position, st.position, alpha);
            matchedNode->rotation = glm::mix(matchedNode->rotation, st.rotation, alpha);
            matchedNode->scale = glm::mix(matchedNode->scale, st.scale, alpha);
            matchedNode->linearVelocity = st.linearVelocity;

            // Also synchronize physics if Jolt is active
            if (auto* physics = Scripting::LuaScriptSystem::Get().GetPhysicsWorld()) {
                if (physics->HasBodyForNode(matchedNode->id)) {
                    physics->SetPosition(matchedNode->id, matchedNode->position);
                    physics->SetLinearVelocity(matchedNode->id, matchedNode->linearVelocity);
                }
            }
        }
    }
    m_replicatedPacketsRecvCount++;
    if (m_replicatedPacketsRecvCount == 1 || (tick % 150 == 0)) {
        std::string role = m_isServer ? "[Server]" : "[Client]";
        AddLog(role + " Receiving live replication (tick #" + std::to_string(tick) + ", " + std::to_string(count) + " node(s)).");
    }
    return true;
}

bool NetworkManager::ReplicateSceneState(const Scene& scene, bool verboseLog) {
    auto pkt = BuildStateReplicationPacket(scene);
    if (m_isServer && !m_connectedClients.empty()) {
        int sentCount = 0;
        for (const auto& c : m_connectedClients) {
            if (m_socket.SendTo(pkt.data(), pkt.size(), c.address, c.port) > 0) {
                ++sentCount;
                m_accumBytesSent += pkt.size();
            }
        }
        m_replicatedPacketsSentCount++;
        if (verboseLog || (m_replicatedPacketsSentCount == 1) || (m_replicationTick % 150 == 0)) {
            AddLog("[Server] Replicating live state (tick #" + std::to_string(m_replicationTick) +
                   ") to " + std::to_string(sentCount) + " client(s).");
        }
        return sentCount > 0;
    }
    bool ok = m_socket.Send(pkt.data(), pkt.size()) > 0;
    if (ok) {
        m_accumBytesSent += pkt.size();
        m_replicatedPacketsSentCount++;
        if (verboseLog || (m_replicatedPacketsSentCount == 1) || (m_replicationTick % 150 == 0)) {
            AddLog("[Client] Replicating live state (tick #" + std::to_string(m_replicationTick) + ") to server.");
        }
    }
    return ok;
}

void NetworkManager::RegisterRPC(const std::string& rpcName, RPCHandler handler) {
    m_rpcHandlers[rpcName] = std::move(handler);
}

std::vector<uint8_t> NetworkManager::BuildRPCPacket(const std::string& rpcName,
                                                    uint32_t targetNetworkId,
                                                    const std::vector<std::string>& args) const {
    std::vector<uint8_t> pkt;
    pkt.push_back(static_cast<uint8_t>(PacketType::RPCInvoke));
    WriteString(pkt, rpcName);
    WriteU32(pkt, targetNetworkId);
    WriteU32(pkt, static_cast<uint32_t>(args.size()));
    for (const auto& a : args) {
        WriteString(pkt, a);
    }
    return pkt;
}

bool NetworkManager::SendRPC(const std::string& rpcName,
                             uint32_t targetNetworkId,
                             const std::vector<std::string>& args,
                             bool executeLocal) {
    if (executeLocal) {
        auto it = m_rpcHandlers.find(rpcName);
        if (it != m_rpcHandlers.end() && it->second) {
            it->second(targetNetworkId, args);
        }
    }
    auto pkt = BuildRPCPacket(rpcName, targetNetworkId, args);
    if (m_isServer && !m_connectedClients.empty()) {
        int sent = 0;
        for (const auto& c : m_connectedClients) {
            if (m_socket.SendTo(pkt.data(), pkt.size(), c.address, c.port) > 0) ++sent;
        }
        AddLog("[Server] Broadcast RPC '" + rpcName + "' to " + std::to_string(sent) + " client(s).");
        return sent > 0;
    }
    bool ok = m_socket.Send(pkt.data(), pkt.size()) > 0;
    if (ok) {
        AddLog("Sent RPC '" + rpcName + "' (targetId=" + std::to_string(targetNetworkId) + ").");
    }
    return ok;
}

bool NetworkManager::ProcessPacket(Scene& scene, const uint8_t* data, size_t size, float interpolationAlpha) {
    if (!data || size == 0) return false;
    PacketType type = static_cast<PacketType>(data[0]);

    if (type == PacketType::StateReplication) {
        return ApplyStateReplicationPacket(scene, data, size, interpolationAlpha);
    } else if (type == PacketType::DeclarativePropertyReplication) {
        return ApplyDeclarativeReplicationPacket(scene, data, size);
    } else if (type == PacketType::RPCInvoke) {
        size_t offset = 1;
        std::string rpcName;
        uint32_t targetNetId = 0;
        uint32_t argCount = 0;
        if (!ReadString(data, size, offset, rpcName)) return false;
        if (!ReadU32(data, size, offset, targetNetId)) return false;
        if (!ReadU32(data, size, offset, argCount)) return false;

        std::vector<std::string> args(argCount);
        for (uint32_t i = 0; i < argCount; ++i) {
            if (!ReadString(data, size, offset, args[i])) return false;
        }

        AddLog("Received RPC '" + rpcName + "' (targetId=" + std::to_string(targetNetId) + ").");
        if (rpcName == "SetAuthority" && !args.empty()) {
            try {
                uint32_t newAuth = static_cast<uint32_t>(std::stoul(args[0]));
                if (auto node = scene.GetNodeById(targetNetId)) {
                    node->SetMultiplayerAuthority(newAuth);
                    AddLog("Node #" + std::to_string(targetNetId) + " Multiplayer Authority set to Peer " + std::to_string(newAuth));
                }
            } catch (...) {}
        }
        auto it = m_rpcHandlers.find(rpcName);
        if (it != m_rpcHandlers.end() && it->second) {
            it->second(targetNetId, args);
            return true;
        }
        return true;
    }
    return false;
}

size_t NetworkManager::PollIncomingPackets(Scene& scene, float interpolationAlpha) {
    size_t processed = 0;
    std::vector<uint8_t> buf;
    while (m_socket.Receive(buf) > 0) {
        if (ProcessPacket(scene, buf.data(), buf.size(), interpolationAlpha)) {
            ++processed;
        }
    }
    return processed;
}

std::vector<uint8_t> NetworkManager::BuildDeclarativeReplicationPacket(Scene& scene, float currentTime) {
    struct NodeReplicationEntry {
        uint32_t networkId;
        std::vector<std::pair<std::string, std::string>> dirtyProps;
    };

    std::vector<NodeReplicationEntry> entries;

    for (auto& node : scene.GetNodes()) {
        if (!node) continue;
        if (!node->replicationConfig.enabled && node->replicationConfig.rules.empty()) continue;

        bool hasAuth = (node->multiplayerAuthority == 0)
                       ? node->IsLocallyActive()
                       : (node->multiplayerAuthority == m_localPeerId);
        if (!hasAuth) continue;

        NodeReplicationEntry entry;
        entry.networkId = (node->networkId != 0) ? node->networkId : node->id;

        for (auto& rule : node->replicationConfig.rules) {
            if (rule.mode == ReplicationMode::None) continue;
            float interval = (rule.syncRateHz > 0.0f) ? (1.0f / rule.syncRateHz) : 0.05f;
            if (currentTime - rule.lastSyncTime < interval) continue;

            std::string valStr = "";
            const std::string& p = rule.propertyName;
            if (p == "position") {
                valStr = std::to_string(node->position.x) + "," + std::to_string(node->position.y) + "," + std::to_string(node->position.z);
            } else if (p == "rotation") {
                valStr = std::to_string(node->rotation.x) + "," + std::to_string(node->rotation.y) + "," + std::to_string(node->rotation.z);
            } else if (p == "scale") {
                valStr = std::to_string(node->scale.x) + "," + std::to_string(node->scale.y) + "," + std::to_string(node->scale.z);
            } else if (p == "linearVelocity") {
                valStr = std::to_string(node->linearVelocity.x) + "," + std::to_string(node->linearVelocity.y) + "," + std::to_string(node->linearVelocity.z);
            } else if (p == "color") {
                valStr = std::to_string(node->color.r) + "," + std::to_string(node->color.g) + "," + std::to_string(node->color.b) + "," + std::to_string(node->color.a);
            } else if (p == "animState" || p == "currentAnimState") {
                valStr = node->currentAnimState;
            } else if (p == "uiProgress") {
                valStr = std::to_string(node->uiProgress);
            } else if (p == "uiValue") {
                valStr = std::to_string(node->uiValue);
            } else if (p == "uiText") {
                valStr = node->uiText;
            }

            if (valStr.empty()) continue;

            if (rule.mode == ReplicationMode::OnChange && valStr == rule.lastSerializedValue) {
                continue;
            }

            rule.lastSyncTime = currentTime;
            rule.lastSerializedValue = valStr;
            entry.dirtyProps.emplace_back(p, valStr);
        }

        if (!entry.dirtyProps.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    if (entries.empty()) return {};

    std::vector<uint8_t> pkt;
    pkt.push_back(static_cast<uint8_t>(PacketType::DeclarativePropertyReplication));
    WriteU32(pkt, static_cast<uint32_t>(entries.size()));

    for (const auto& entry : entries) {
        WriteU32(pkt, entry.networkId);
        WriteU32(pkt, static_cast<uint32_t>(entry.dirtyProps.size()));
        for (const auto& kv : entry.dirtyProps) {
            WriteString(pkt, kv.first);
            WriteString(pkt, kv.second);
        }
    }

    return pkt;
}

bool NetworkManager::ApplyDeclarativeReplicationPacket(Scene& scene, const uint8_t* data, size_t size) {
    if (!data || size < 5) return false;
    size_t offset = 1;
    uint32_t nodeCount = 0;
    if (!ReadU32(data, size, offset, nodeCount)) return false;

    for (uint32_t i = 0; i < nodeCount; ++i) {
        uint32_t netId = 0;
        uint32_t propCount = 0;
        if (!ReadU32(data, size, offset, netId)) return false;
        if (!ReadU32(data, size, offset, propCount)) return false;

        std::shared_ptr<Node> targetNode = nullptr;
        for (const auto& node : scene.GetNodes()) {
            if (node && (node->networkId == netId || node->id == netId)) {
                targetNode = node;
                break;
            }
        }

        bool hasAuth = false;
        if (targetNode) {
            hasAuth = (targetNode->multiplayerAuthority == 0)
                      ? targetNode->IsLocallyActive()
                      : (targetNode->multiplayerAuthority == m_localPeerId);
        }

        for (uint32_t p = 0; p < propCount; ++p) {
            std::string propName;
            std::string propVal;
            if (!ReadString(data, size, offset, propName)) return false;
            if (!ReadString(data, size, offset, propVal)) return false;

            if (!targetNode || hasAuth) {
                continue;
            }

            if (propName == "position") {
                float x=0, y=0, z=0;
                if (sscanf(propVal.c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
                    targetNode->position = glm::vec3(x, y, z);
                    if (auto* physics = Scripting::LuaScriptSystem::Get().GetPhysicsWorld()) {
                        if (physics->HasBodyForNode(targetNode->id)) {
                            physics->SetPosition(targetNode->id, targetNode->position);
                        }
                    }
                }
            } else if (propName == "rotation") {
                float rx=0, ry=0, rz=0;
                if (sscanf(propVal.c_str(), "%f,%f,%f", &rx, &ry, &rz) == 3) {
                    targetNode->rotation = glm::vec3(rx, ry, rz);
                }
            } else if (propName == "scale") {
                float sx=1, sy=1, sz=1;
                if (sscanf(propVal.c_str(), "%f,%f,%f", &sx, &sy, &sz) == 3) {
                    targetNode->scale = glm::vec3(sx, sy, sz);
                }
            } else if (propName == "linearVelocity") {
                float vx=0, vy=0, vz=0;
                if (sscanf(propVal.c_str(), "%f,%f,%f", &vx, &vy, &vz) == 3) {
                    targetNode->linearVelocity = glm::vec3(vx, vy, vz);
                    if (auto* physics = Scripting::LuaScriptSystem::Get().GetPhysicsWorld()) {
                        if (physics->HasBodyForNode(targetNode->id)) {
                            physics->SetLinearVelocity(targetNode->id, targetNode->linearVelocity);
                        }
                    }
                }
            } else if (propName == "color") {
                float r=1, g=1, b=1, a=1;
                if (sscanf(propVal.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a) >= 3) {
                    targetNode->color = glm::vec4(r, g, b, a);
                }
            } else if (propName == "animState" || propName == "currentAnimState") {
                targetNode->currentAnimState = propVal;
            } else if (propName == "uiProgress") {
                try { targetNode->uiProgress = std::stof(propVal); } catch (...) {}
            } else if (propName == "uiValue") {
                try { targetNode->uiValue = std::stof(propVal); } catch (...) {}
            } else if (propName == "uiText") {
                targetNode->uiText = propVal;
            }
        }
    }

    return true;
}

bool NetworkManager::SyncDeclarativeProperties(Scene& scene, float currentTime) {
    auto pkt = BuildDeclarativeReplicationPacket(scene, currentTime);
    if (pkt.empty()) return false;

    if (m_isServer && !m_connectedClients.empty()) {
        int sent = 0;
        for (const auto& c : m_connectedClients) {
            if (m_socket.SendTo(pkt.data(), pkt.size(), c.address, c.port) > 0) ++sent;
        }
        return sent > 0;
    }

    return m_socket.Send(pkt.data(), pkt.size()) > 0;
}

} // namespace Networking
