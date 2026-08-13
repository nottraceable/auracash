#include "wallet/Wallet.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: auracash-wallet <command> ...\n";
        std::cerr << "Commands:\n";
        std::cerr << "  generatekey       - Generate new keypair\n";
        std::cerr << "  getbalance <addr> - Get balance for address\n";
        std::cerr << "  listunspent <addr>- List unspent outputs for address\n";
        std::cerr << "  createtx fromAddr toAddr amount [fee]\n";
        std::cerr << "  broadcasttx hex   - Broadcast raw hex transaction\n";
        return 1;
    }

    std::string host = "127.0.0.1";
    uint16_t rpcPort = 8332;
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
        if (argc < 6) {
            std::cerr << "Usage: createtx fromAddr toAddr amount [fee]\n";
            return 1;
        }
        uint64_t amount = std::stoull(argv[4]);
        uint64_t fee = 1000;
        if (argc >= 7) {
            fee = std::stoull(argv[6]);
        }
        // Generate keypair for sender (or accept private key via argument)
        auto senderKp = wallet.generate_keypair();
        if (!senderKp) {
            std::cerr << "Failed to generate sender keypair\n";
            return 1;
        }
        std::string txHex;
        auto txResult = wallet.create_transaction(
            senderKp->address, argv[3], amount, senderKp->priv, fee);
        if (!txResult) {
            std::cerr << "Failed to create transaction\n";
            return 1;
        }
        txHex = *txResult;
        std::cout << "Raw Transaction Hex:\n";
        std::cout << txHex << "\n";
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