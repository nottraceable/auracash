#pragma once
#include "CoreTypes.h"
#include "mempool/Mempool.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace auracash {

class NetManager {
public:
    NetManager(uint16_t listenPort, const std::string& network = "mainnet");
    ~NetManager();

    // Start listening for inbound connections
    bool startListening();
    void stopListening();

    // Connect to a peer
    bool connectToPeer(const std::string& host, uint16_t port);

    // Send a block to all connected peers
    void broadcastBlock(const Block& block);

    // Send a transaction to all connected peers
    void broadcastTransaction(const Transaction& tx);

    // Request blocks from a peer
    void requestBlocks(const std::string& peer, uint32_t fromHeight);

    // Request transactions
    void requestTransaction(const Hash256& txid, const std::string& peer);

    // Get number of connected peers
    size_t getPeerCount() const;

    // Accessors for testing
    bool isListening() const { return m_listening.load(); }
    bool isConnectedTo(const std::string& host) const;

private:
    struct Peer {
        int socket = -1;
        std::string host;
        uint16_t port;
        std::thread workerThread;
        std::atomic<bool> running{false};
    };

    std::vector<std::unique_ptr<Peer>> m_peers;
    uint16_t m_listenPort;
    std::atomic<bool> m_listening{false};
    mutable std::mutex m_peerMutex;
    mutable std::unordered_map<std::string, Peer*> m_peerMap;

    // Worker function for each peer connection
    void peerWorker(Peer* peer);
    // Accept loop for listening socket
    void acceptLoop();
    // Helper to send raw data over socket
    bool sendData(int sock, const void* data, size_t len);
    // Send a framed message
    void sendMessage(int sock, uint8_t type, const std::vector<uint8_t>& payload);
    // Process received message
    void processMessage(Peer* peer, uint8_t type, const std::vector<uint8_t>& msg);
    // Send version message
    void sendVersion(int sock, uint32_t protocolVersion);
    // Send verack
    void sendVerack(int sock);
    // Serialize/serialize block and transaction
    std::vector<uint8_t> serializeBlock(const Block& block);
    std::vector<uint8_t> serializeTransaction(const Transaction& tx);
    // Handle block message
    void handleBlock(Peer* peer, const Block& block);
    // Handle transaction message
    void handleTransaction(Peer* peer, const Transaction& tx);
    // Handle inv message
    void handleInv(Peer* peer, const std::vector<Hash256>& hashes, bool blocks);
    // Handle getdata
    void handleGetData(Peer* peer, const std::vector<Hash256>& hashes);
    // Handle getblocks
    void handleGetBlocks(Peer* peer, uint32_t fromHeight);
};

} // namespace auracash