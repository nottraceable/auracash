#include "NetManager.h"
#include <cstring>
#include <iostream>
#include <ctime>
#include <chrono>
#include <thread>
#include <sstream>

namespace auracash {

NetManager::NetManager(uint16_t listenPort, const std::string& network)
    : m_listenPort(listenPort) {}

NetManager::~NetManager() {
    stopListening();
    for (auto& peer : m_peers) {
        peer->running.store(false);
        if (peer->socket >= 0) close(peer->socket);
        if (peer->workerThread.joinable()) peer->workerThread.join();
        peer->workerThread = std::thread();
    }
    m_peers.clear();
    m_peerMap.clear();
}

bool NetManager::isConnectedTo(const std::string& host) const {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_peerMap.find(host) != m_peerMap.end();
}

size_t NetManager::getPeerCount() const {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_peers.size();
}

bool NetManager::startListening() {
    if (m_listening.load()) return true;
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        std::cerr << "[net] Failed to create listening socket\n";
        return false;
    }
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_listenPort);
    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[net] Failed to bind on port " << m_listenPort << "\n";
        close(listenSock);
        return false;
    }
    if (listen(listenSock, 32) < 0) {
        std::cerr << "[net] Failed to listen on port " << m_listenPort << "\n";
        close(listenSock);
        return false;
    }
    m_listening.store(true);
    std::thread acceptThread([this, listenSock]() {
        std::cerr << "[net] Listening on port " << m_listenPort << "\n";
        while (m_listening.load()) {
            int clientSock;
            sockaddr_in clientAddr{};
            socklen_t len = sizeof(clientAddr);
            clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &len);
            if (clientSock < 0) {
                if (!m_listening.load()) break;
                continue;
            }
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            auto peer = std::make_unique<Peer>();
            peer->socket = clientSock;
            peer->host = ip;
            peer->port = ntohs(clientAddr.sin_port);
            peer->running.store(true);
            Peer* raw = peer.get();
            peer->workerThread = std::thread([this, raw]() { peerWorker(raw); });
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                m_peers.push_back(std::move(peer));
                m_peerMap[raw->host] = raw;
            }
        }
        close(listenSock);
    });
    acceptThread.detach();
    return true;
}

void NetManager::stopListening() {
    m_listening.store(false);
}

bool NetManager::connectToPeer(const std::string& host, uint16_t port) {
    if (isConnectedTo(host)) return false;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }
    auto peer = std::make_unique<Peer>();
    peer->socket = sock;
    peer->host = host;
    peer->port = port;
    peer->running.store(true);
    Peer* raw = peer.get();
    peer->workerThread = std::thread([this, raw]() { peerWorker(raw); });
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        m_peers.push_back(std::move(peer));
        m_peerMap[host] = raw;
    }
    std::cerr << "[net] Connected to " << host << ":" << port << "\n";
    return true;
}

void NetManager::peerWorker(Peer* peer) {
    // Perform handshake: send our version, read version, send verack
    sendVersion(peer->socket, 70015);
    while (peer->running.load()) {
        uint8_t type;
        ssize_t n = recv(peer->socket, &type, 1, 0);
        if (n <= 0) break;
        uint8_t lenBuf[4];
        n = recv(peer->socket, (void*)lenBuf, 4, MSG_WAITALL);
        if (n <= 0) break;
        uint32_t len = lenBuf[0] | (lenBuf[1] << 8) | (lenBuf[2] << 16) | (lenBuf[3] << 24);
        if (len > 4 * 1024 * 1024) {
            std::cerr << "[net] Oversized message (" << len << ") from " << peer->host << "\n";
            break;
        }
        std::vector<uint8_t> payload(len);
        if (len > 0) {
            n = recv(peer->socket, payload.data(), len, MSG_WAITALL);
            if (n <= 0) break;
        }
        processMessage(peer, type, payload);
    }
    peer->running.store(false);
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        m_peerMap.erase(peer->host);
    }
    if (peer->socket >= 0) close(peer->socket);
}

bool NetManager::sendData(int sock, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void NetManager::sendMessage(int sock, uint8_t type, const std::vector<uint8_t>& payload) {
    if (!sendData(sock, &type, 1)) return;
    uint8_t lenBuf[4];
    uint32_t len = static_cast<uint32_t>(payload.size());
    lenBuf[0] = len & 0xFF;
    lenBuf[1] = (len >> 8) & 0xFF;
    lenBuf[2] = (len >> 16) & 0xFF;
    lenBuf[3] = (len >> 24) & 0xFF;
    if (!sendData(sock, lenBuf, 4)) return;
    if (!payload.empty()) sendData(sock, payload.data(), payload.size());
}

void NetManager::sendVersion(int sock, uint32_t protocolVersion) {
    std::vector<uint8_t> payload;
    // protocol version (4 bytes LE)
    payload.push_back(protocolVersion & 0xFF);
    payload.push_back((protocolVersion >> 8) & 0xFF);
    payload.push_back((protocolVersion >> 16) & 0xFF);
    payload.push_back((protocolVersion >> 24) & 0xFF);
    // services (8 bytes LE) set to 1 (NODE_NETWORK)
    uint64_t services = 1;
    for (int i = 0; i < 8; ++i) payload.push_back(static_cast<uint8_t>((services >> (8*i)) & 0xFF));
    // timestamp (8 bytes LE)
    uint64_t ts = static_cast<uint64_t>(std::time(nullptr));
    for (int i = 0; i < 8; ++i) payload.push_back(static_cast<uint8_t>((ts >> (8*i)) & 0xFF));
    // addr_recv_flags (1 byte)
    payload.push_back(1);
    // node id (4 bytes LE)
    payload.push_back(0); payload.push_back(0); payload.push_back(0); payload.push_back(1);
    // user agent (string: len byte + bytes)
    const char* ua = "AuraCash v3.0.0";
    size_t uaLen = strlen(ua);
    payload.push_back(static_cast<uint8_t>(uaLen));
    for (size_t i = 0; i < uaLen; ++i) payload.push_back(ua[i]);
    // start height (4 bytes LE)
    payload.push_back(0); payload.push_back(0); payload.push_back(0); payload.push_back(0);
    sendMessage(sock, 0, payload); // type 0 = version
}

void NetManager::sendVerack(int sock) {
    std::vector<uint8_t> empty;
    sendMessage(sock, 1, empty);
}

void NetManager::processMessage(Peer* peer, uint8_t type, const std::vector<uint8_t>& msg) {
    switch (type) {
        case 0: // version -> reply with verack
            sendVerack(peer->socket);
            break;
        case 4: { // block
            // Deserialize block placeholder
            break;
        }
        case 5: { // tx
            break;
        }
        case 6: // getblocks
            break;
        case 3: // getdata
            break;
        default:
            break;
    }
}

void NetManager::broadcastBlock(const Block& block) {
    std::vector<uint8_t> payload = serializeBlock(block);
    std::lock_guard<std::mutex> lock(m_peerMutex);
    for (auto& peer : m_peers) {
        if (peer->socket >= 0) sendMessage(peer->socket, 4, payload);
    }
}

void NetManager::broadcastTransaction(const Transaction& tx) {
    std::vector<uint8_t> payload = serializeTransaction(tx);
    std::lock_guard<std::mutex> lock(m_peerMutex);
    for (auto& peer : m_peers) {
        if (peer->socket >= 0) sendMessage(peer->socket, 5, payload);
    }
}

void NetManager::requestBlocks(const std::string& peerName, uint32_t fromHeight) {
    std::vector<uint8_t> payload;
    payload.push_back(fromHeight & 0xFF);
    payload.push_back((fromHeight >> 8) & 0xFF);
    payload.push_back((fromHeight >> 16) & 0xFF);
    payload.push_back((fromHeight >> 24) & 0xFF);
    std::lock_guard<std::mutex> lock(m_peerMutex);
    auto it = m_peerMap.find(peerName);
    if (it != m_peerMap.end() && it->second->socket >= 0) {
        sendMessage(it->second->socket, 6, payload);
    }
}

void NetManager::requestTransaction(const Hash256& txid, const std::string& peerName) {
    std::vector<uint8_t> payload(txid.begin(), txid.end());
    std::lock_guard<std::mutex> lock(m_peerMutex);
    auto it = m_peerMap.find(peerName);
    if (it != m_peerMap.end() && it->second->socket >= 0) {
        sendMessage(it->second->socket, 3, payload);
    }
}

std::vector<uint8_t> NetManager::serializeBlock(const Block& block) {
    std::vector<uint8_t> data;
    auto push4 = [&](uint32_t v) {
        data.push_back(v & 0xFF);
        data.push_back((v >> 8) & 0xFF);
        data.push_back((v >> 16) & 0xFF);
        data.push_back((v >> 24) & 0xFF);
    };
    push4(block.header.version);
    data.insert(data.end(), block.header.prevBlockHash.begin(), block.header.prevBlockHash.end());
    data.insert(data.end(), block.header.merkleRoot.begin(), block.header.merkleRoot.end());
    push4(block.header.timestamp);
    push4(block.header.targetBits);
    push4(block.header.nonce);
    push4(static_cast<uint32_t>(block.transactions.size()));
    for (const auto& tx : block.transactions) {
        auto txData = serializeTransaction(tx);
        data.insert(data.end(), txData.begin(), txData.end());
    }
    return data;
}

std::vector<uint8_t> NetManager::serializeTransaction(const Transaction& tx) {
    std::vector<uint8_t> data;
    auto push4 = [&](uint32_t v) {
        data.push_back(v & 0xFF);
        data.push_back((v >> 8) & 0xFF);
        data.push_back((v >> 16) & 0xFF);
        data.push_back((v >> 24) & 0xFF);
    };
    auto push8 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) data.push_back(static_cast<uint8_t>((v >> (8*i)) & 0xFF));
    };
    push4(tx.version);
    push4(static_cast<uint32_t>(tx.inputs.size()));
    for (const auto& in : tx.inputs) {
        data.insert(data.end(), in.prevTxHash.begin(), in.prevTxHash.end());
        push4(in.prevVout);
        push4(static_cast<uint32_t>(in.scriptSig.size()));
        data.insert(data.end(), in.scriptSig.begin(), in.scriptSig.end());
        push4(in.sequence);
    }
    push4(static_cast<uint32_t>(tx.outputs.size()));
    for (const auto& out : tx.outputs) {
        push8(out.value);
        push4(static_cast<uint32_t>(out.scriptPubKey.size()));
        data.insert(data.end(), out.scriptPubKey.begin(), out.scriptPubKey.end());
    }
    push4(tx.lockTime);
    return data;
}

void NetManager::handleBlock(Peer* peer, const Block& block) {}
void NetManager::handleTransaction(Peer* peer, const Transaction& tx) {}
void NetManager::handleInv(Peer* peer, const std::vector<Hash256>& hashes, bool blocks) {}
void NetManager::handleGetData(Peer* peer, const std::vector<Hash256>& hashes) {}
void NetManager::handleGetBlocks(Peer* peer, uint32_t fromHeight) {}

} // namespace auracash