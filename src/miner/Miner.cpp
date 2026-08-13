#include "CoreTypes.h"
#include "crypto/Secp256k1.h"
#include "rpc/RpcClient.h"
#include "AuraHash.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <csignal>
#include <atomic>
#include <thread>

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    (void)signal;
    g_running = false;
    std::cerr << "\n[miner] Stopping miner...\n";
}

namespace auracash {

struct BlockTemplate {
    Hash256 prevBlockHash;
    uint32_t targetBits;
    Hash256 merkleRoot;
    uint64_t reward;
};

std::vector<uint8_t> decode_base58(const std::string& str) {
    static const std::string pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t zeroes = 0;
    while (zeroes < str.size() && str[zeroes] == '1') ++zeroes;
    std::vector<uint8_t> b256((str.size() - zeroes) * 733 / 1000 + 1, 0);
    for (size_t i = zeroes; i < str.size(); ++i) {
        auto pos = pszBase58.find(str[i]);
        if (pos == std::string::npos) return {};
        int carry = static_cast<int>(pos);
        for (auto it = b256.rbegin(); it != b256.rend(); ++it) {
            carry += 58 * (*it);
            *it = carry % 256;
            carry /= 256;
        }
    }
    auto it = b256.begin();
    while (it != b256.end() && *it == 0) ++it;
    std::vector<uint8_t> result(zeroes, 0x00);
    result.insert(result.end(), it, b256.end());
    return result;
}

std::vector<uint8_t> parse_address_pkhash(const std::string& addr) {
    std::vector<uint8_t> pkHash(20, 0);
    if (addr.empty()) return pkHash;

    auto decoded = decode_base58(addr);
    if (decoded.size() >= 21) {
        if (decoded.size() >= 25) {
            std::copy(decoded.begin() + 1, decoded.begin() + 21, pkHash.begin());
        } else {
            size_t copyLen = std::min(static_cast<size_t>(20), decoded.size());
            std::copy(decoded.begin(), decoded.begin() + copyLen, pkHash.begin());
        }
        return pkHash;
    }

    if (addr.size() >= 40) {
        for (size_t i = 0; i < 40; i += 2) {
            try {
                pkHash[i / 2] = static_cast<uint8_t>(std::stoul(addr.substr(i, 2), nullptr, 16));
            } catch (...) {
                pkHash[i / 2] = 0;
            }
        }
        return pkHash;
    }

    for (size_t i = 0; i < std::min(addr.size(), static_cast<size_t>(20)); ++i) {
        pkHash[i] = static_cast<uint8_t>(addr[i]);
    }
    return pkHash;
}

bool get_block_template(rpc::RpcClient& client, BlockTemplate& bt) {
    auracash::rpc::JsonArray params;
    params.push_back(auracash::rpc::JsonValue("rules"));
    params.push_back(auracash::rpc::JsonValue("mainnet"));
    auracash::rpc::JsonValue result;
    if (!client.call("getblocktemplate", params, result)) {
        return false;
    }
    if (!result.is_object()) {
        return false;
    }
    const auto& obj = result.as_object();
    std::string prevHex = obj.at("previousblockhash").as_string();
    auracash::uint256 tmp(prevHex);
    std::memcpy(bt.prevBlockHash.data(), tmp.data, 32);
    std::string mrHex = obj.at("merkleroot").as_string();
    auracash::uint256 tmp2(mrHex);
    std::memcpy(bt.merkleRoot.data(), tmp2.data, 32);
    bt.reward = static_cast<uint64_t>(obj.at("coinbasevalue").as_uint64());
    // bits may be returned as uint64, cast safely to uint32_t
    bt.targetBits = static_cast<uint32_t>(obj.at("bits").as_uint64());
    return true;
}

std::vector<uint8_t> serialize_transaction(const Transaction& tx) {
    std::vector<uint8_t> data;
    auto push4 = [&](uint32_t v) {
        data.push_back(v & 0xFF);
        data.push_back((v >> 8) & 0xFF);
        data.push_back((v >> 16) & 0xFF);
        data.push_back((v >> 24) & 0xFF);
    };
    auto push8 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            data.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
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
    for (const auto& o : tx.outputs) {
        push8(o.value);
        push4(static_cast<uint32_t>(o.scriptPubKey.size()));
        data.insert(data.end(), o.scriptPubKey.begin(), o.scriptPubKey.end());
    }
    push4(tx.lockTime);
    return data;
}

std::vector<uint8_t> serialize_block(const Block& block) {
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
        auto tx_data = serialize_transaction(tx);
        data.insert(data.end(), tx_data.begin(), tx_data.end());
    }
    return data;
}

} // namespace auracash

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string host = "127.0.0.1";
    uint16_t port = 8332;
    std::string payoutAddress;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) {
            if (arg == "--node" && i + 1 < argc) {
                std::string url = argv[++i];
                auto pos = url.find("://");
                if (pos != std::string::npos) url = url.substr(pos + 3);
                auto colon = url.find(":");
                if (colon != std::string::npos) {
                    host = url.substr(0, colon);
                    try { port = static_cast<uint16_t>(std::stoul(url.substr(colon + 1))); } catch (...) {}
                } else {
                    host = url;
                }
            } else if (arg == "--address" && i + 1 < argc) {
                payoutAddress = argv[++i];
            }
        } else {
            if (positional == 0) host = arg;
            else if (positional == 1) {
                try { port = static_cast<uint16_t>(std::stoul(arg)); } catch (...) {}
            } else if (positional == 2) payoutAddress = arg;
            ++positional;
        }
    }

    std::cerr << "==================================================\n";
    std::cerr << "[miner] Starting Continuous AuraCash Miner\n";
    std::cerr << "[miner] Node: " << host << ":" << port << "\n";
    std::cerr << "[miner] Payout Address: " << (payoutAddress.empty() ? "(default null)" : payoutAddress) << "\n";
    std::cerr << "==================================================\n";

    auracash::rpc::RpcClient client(host, port);
    uint64_t totalBlocksMined = 0;

    while (g_running) {
        auracash::BlockTemplate bt;
        if (!auracash::get_block_template(client, bt)) {
            std::cerr << "[miner] Waiting for node / getblocktemplate failed. Retrying in 3s...\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cerr << "\n[miner] --- Mining Block #" << (totalBlocksMined + 1) << " ---\n";
        std::cerr << "[miner] PrevBlockHash: " << auracash::to_hex(bt.prevBlockHash) << "\n";
        std::cerr << "[miner] Target Bits:   0x" << std::hex << bt.targetBits << std::dec << "\n";

        auracash::Transaction coinbase_tx;
        coinbase_tx.version = 1;
        auracash::TxIn coinbase_in;
        std::memset(&coinbase_in.prevTxHash, 0, 32);
        coinbase_in.prevVout = 0xffffffff;
        coinbase_in.scriptSig = {};
        coinbase_in.sequence = 0;
        coinbase_tx.inputs.push_back(coinbase_in);
        auracash::TxOut coinbase_out;
        coinbase_out.value = bt.reward;
        if (!payoutAddress.empty()) {
            std::vector<uint8_t> pkHash = auracash::parse_address_pkhash(payoutAddress);
            coinbase_out.scriptPubKey = {0x76, 0xA9, 0x14};
            coinbase_out.scriptPubKey.insert(coinbase_out.scriptPubKey.end(), pkHash.begin(), pkHash.end());
            coinbase_out.scriptPubKey.insert(coinbase_out.scriptPubKey.end(), {0x88, 0xAC});
        } else {
            coinbase_out.scriptPubKey = {0x76, 0xA9, 0x14, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0x88, 0xAC};
        }
        coinbase_tx.outputs.push_back(coinbase_out);
        coinbase_tx.lockTime = 0;

        auracash::BlockHeader header;
        header.version = 1;
        std::memcpy(header.prevBlockHash.data(), bt.prevBlockHash.data(), 32);
        std::memcpy(header.merkleRoot.data(), bt.merkleRoot.data(), 32);
        header.timestamp = static_cast<uint32_t>(std::time(nullptr));
        header.targetBits = bt.targetBits;

        auracash::Hash256 target;
        target.fill(0xFF);
        if (bt.targetBits != 0) {
            uint32_t exponent = (bt.targetBits >> 24) & 0xFF;
            uint32_t mantissa = bt.targetBits & 0x007FFFFF;
            target.fill(0);
            if (exponent <= 3) {
                for (int i = 0; i < 3; ++i) {
                    target[31 - i] = static_cast<uint8_t>((mantissa >> (8 * i)) & 0xFF);
                }
            } else {
                int shift = exponent - 3;
                for (int i = 0; i < 32; ++i) {
                    int idx = 31 - shift + i;
                    if (idx >= 0 && idx < 32) {
                        uint32_t byte = (mantissa >> (8 * i)) & 0xFF;
                        target[idx] = static_cast<uint8_t>(byte);
                    }
                }
            }
        }

        uint32_t nonce = auracash::AuraHashMine(header, target);
        if (!g_running) break;

        header.nonce = nonce;

        auracash::Block block;
        block.header = header;
        block.transactions.push_back(coinbase_tx);

        std::vector<uint8_t> block_raw = auracash::serialize_block(block);
        std::string block_hex;
        for (uint8_t b : block_raw) {
            char hb[3];
            std::snprintf(hb, sizeof(hb), "%02x", b);
            block_hex += hb;
        }

        auracash::rpc::JsonArray submit_params;
        submit_params.push_back(auracash::rpc::JsonValue(block_hex));
        auracash::rpc::JsonValue result;
        if (client.call("submitblock", submit_params, result)) {
            totalBlocksMined++;
            std::cerr << "[miner] SUCCESS: Block mined & accepted! (Total mined: " << totalBlocksMined << ")\n";
        } else {
            std::cerr << "[miner] ERROR: Block rejected.\n";
        }
    }

    std::cerr << "[miner] Miner stopped cleanly. Session total: " << totalBlocksMined << " blocks.\n";
    return 0;
}
