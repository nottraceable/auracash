#pragma once
#include "CoreTypes.h"
#include "crypto/Secp256k1.h"
#include "rpc/Json.h"
#include "rpc/RpcClient.h"
#include <string>
#include <vector>
#include <optional>

namespace auracash {
namespace wallet {

struct Utxo {
    Hash256 txid;
    uint32_t vout;
    uint64_t value;
    std::vector<uint8_t> scriptPubKey;
};

struct Keypair {
    uint256 priv;
    uint256 pub;
    std::string address;
};

class Wallet {
public:
    Wallet(const std::string& rpcHost, uint16_t rpcPort);
    ~Wallet();

    // Generate new keypair
    std::optional<Keypair> generate_keypair();

    // Get balance for address
    bool get_balance(const std::string& address, uint64_t& balance);

    // Create and sign transaction
    // Returns raw transaction hex
    std::optional<std::string> create_transaction(
        const std::string& fromAddress,
        const std::string& toAddress,
        uint64_t amount,
        const uint256& privKey,
        uint64_t fee = 1000);

    // Broadcast raw transaction hex
    bool broadcast_transaction(const std::string& rawTxHex);

    // Get UTXOs for address via RPC
    bool fetch_utxos(const std::string& address, std::vector<Utxo>& utxos);

private:
    std::string m_rpcHost;
    uint16_t m_rpcPort;
    rpc::RpcClient m_client;
    auracash::Secp256k1 m_crypto;
};

} // namespace wallet
} // namespace auracash