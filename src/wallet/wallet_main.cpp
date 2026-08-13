#include "wallet/Wallet.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

// Minimal hex decode for private key input
static auracash::uint256 hex_to_uint256(const std::string& hex) {
    auracash::uint256 out;
    std::memset(out.data, 0, 32);
    for (size_t i = 0; i + 1 < hex.size() && i / 2 < 32; i += 2) {
        unsigned char hi = hex[i], lo = hex[i+1];
        unsigned char hv = 0, lv = 0;
        if (hi >= '0' && hi <= '9') hv = hi - '0';
        else if (hi >= 'a' && hi <= 'f') hv = hi - 'a' + 10;
        else if (hi >= 'A' && hi <= 'F') hv = hi - 'A' + 10;
        if (lo >= '0' && lo <= '9') lv = lo - '0';
        else if (lo >= 'a' && lo <= 'f') lv = lo - 'a' + 10;
        else if (lo >= 'A' && lo <= 'F') lv = lo - 'A' + 10;
        out.data[i/2] = (hv << 4) | lv;
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: auracash-wallet <command> ...\n";
        std::cerr << "Commands:\n";
        std::cerr << "  generatekey                     - Generate new keypair\n";
        std::cerr << "  getbalance <address>            - Get balance for address\n";
        std::cerr << "  listunspent <address>           - List unspent outputs for address\n";
        std::cerr << "  createtx <fromAddr> <toAddr> <amount> [fee] [privkeyhex]\n";
        std::cerr << "  broadcasttx <hex>               - Broadcast raw hex transaction\n";
        return 1;
    }

    std::string host = "127.0.0.1";
    uint16_t rpcPort = 28432;
    auracash::wallet::Wallet wallet(host, rpcPort);

    std::string cmd = argv[1];
    if (cmd == "generatekey") {
        auto kp = wallet.generate_keypair();
        if (!kp) {
            std::cerr << "Failed to generate keypair\n";
            return 1;
        }
        std::cout << "Private Key: " << kp->priv.to_hex() << "\n";
        std::cout << "Public Key:  " << kp->pub.to_hex() << "\n";
        std::cout << "Address:     " << kp->address << "\n";
    } else if (cmd == "getbalance") {
        if (argc < 3) {
            std::cerr << "Usage: getbalance <address>\n";
            return 1;
        }
        uint64_t balance = 0;
        if (!wallet.get_balance(argv[2], balance)) {
            std::cerr << "Failed to get balance\n";
            return 1;
        }
        std::cout << "Balance: " << balance << " XAC\n";
    } else if (cmd == "listunspent") {
        if (argc < 3) {
            std::cerr << "Usage: listunspent <address>\n";
            return 1;
        }
        std::vector<auracash::wallet::Utxo> utxos;
        if (!wallet.fetch_utxos(argv[2], utxos)) {
            std::cerr << "Failed to list UTXOs\n";
            return 1;
        }
        std::cout << "Unspent Outputs:\n";
        for (const auto& u : utxos) {
            std::cout << "  txid: " << auracash::to_hex(u.txid) << "\n";
            std::cout << "  vout: " << u.vout << "\n";
            std::cout << "  value: " << u.value << "\n";
        }
    } else if (cmd == "createtx") {
        if (argc < 5) {
            std::cerr << "Usage: createtx <fromAddr> <toAddr> <amount> [fee] [privkeyhex]\n";
            return 1;
        }
        std::string fromAddr = argv[2];
        std::string toAddr = argv[3];
        uint64_t amount = std::stoull(argv[4]);
        uint64_t fee = 1000;
        auracash::uint256 privKey;
        if (argc >= 6) {
            // If provided priv key hex looks valid (>= 32 chars), use it; otherwise treat as fee
            std::string arg5 = argv[5];
            if (arg5.size() >= 64) {
                privKey = hex_to_uint256(arg5);
                if (argc >= 7) fee = std::stoull(argv[6]);
            } else {
                fee = std::stoull(arg5);
                if (argc >= 7) privKey = hex_to_uint256(argv[6]);
            }
        }
        // If no private key provided, try to use the fromAddr string bytes as a fallback key
        if (std::memcmp(privKey.data, 0, 32) == 0) {
            std::memset(privKey.data, 0xAB, 32);
            for (size_t i = 0; i < std::min(fromAddr.size(), static_cast<size_t>(32)); ++i) {
                privKey.data[i] ^= static_cast<uint8_t>(fromAddr[i]);
            }
        }
        auto txResult = wallet.create_transaction(fromAddr, toAddr, amount, privKey, fee);
        if (!txResult) {
            std::cerr << "Failed to create transaction\n";
            return 1;
        }
        std::cout << *txResult << "\n";
    } else if (cmd == "broadcasttx") {
        if (argc < 3) {
            std::cerr << "Usage: broadcasttx <hex>\n";
            return 1;
        }
        if (!wallet.broadcast_transaction(argv[2])) {
            std::cerr << "Failed to broadcast transaction\n";
            return 1;
        }
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        return 1;
    }

    return 0;
}
