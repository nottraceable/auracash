#include "Consensus.h"
#include "UTXOEngine.h"
#include "AuraHash.h"
#include "keccak256.h"
#include "EmissionModel.h"
#include "CoreTypes.h"
#include "mempool/Mempool.h"
#include "net/NetManager.h"
#include "rpc/RpcServer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace auracash;

static uint64_t parse_json_uint64(const rpc::JsonValue &val) {
    if (val.is_uint64()) return val.as_uint64();
    if (val.is_int64()) return static_cast<uint64_t>(val.as_int64());
    if (val.is_string()) {
        try { return std::stoull(val.as_string()); } catch (...) { return 0; }
    }
    return 0;
}

static std::vector<uint8_t> decode_base58(const std::string& str) {
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

static std::vector<uint8_t> parse_address_pkhash(const std::string& addr) {
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
            try { pkHash[i / 2] = static_cast<uint8_t>(std::stoul(addr.substr(i, 2), nullptr, 16)); }
            catch (...) { pkHash[i / 2] = 0; }
        }
        return pkHash;
    }
    for (size_t i = 0; i < std::min(addr.size(), static_cast<size_t>(20)); ++i) {
        pkHash[i] = static_cast<uint8_t>(addr[i]);
    }
    return pkHash;
}

static bool script_pub_key_matches_address(const std::vector<uint8_t>& script, const std::string& address) {
    if (script.size() < 23) return false;
    // Expect P2PKH: OP_DUP(0x76) OP_HASH160(0xA9) push20(0x14) [20 bytes] OP_EQUALVERIFY(0x88) OP_CHECKSIG(0xAC)
    if (script[0] != 0x76 || script[1] != 0xA9 || script[2] != 0x14 || script[23] != 0x88 || script[24] != 0xAC) return false;
    auto pkHash = parse_address_pkhash(address);
    return std::equal(script.begin() + 3, script.begin() + 23, pkHash.begin());
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    std::cout << "AuraCash (XAC) v3.0 Node" << std::endl;
    std::cout << "Starting..." << std::endl;

    UTXOSet utxoSet;
    ConsensusEngine consensus(utxoSet);
    Mempool mempool;
    const uint16_t p2pPort = 28433;
    const uint16_t rpcPort = 28432;
    NetManager netManager(p2pPort);
    rpc::RpcServer rpcServer(rpcPort);

    static std::vector<Block> blockchain;
    blockchain.push_back(ConsensusEngine::GetGenesisBlock());
    const Block &gen = blockchain.back();
    if (!gen.transactions.empty()) {
        const Transaction &cb = gen.transactions[0];
        OutPoint out{cb.GetHash(), 0};
        utxoSet.AddCoin(out, cb.outputs[0]);
    }

    rpcServer.register_method("getblockchaininfo", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        (void)params;
        rpc::JsonObject obj;
        obj["chain"] = rpc::JsonValue("mainnet");
        obj["blocks"] = rpc::JsonValue(static_cast<int64_t>(blockchain.size() - 1));
        obj["difficulty"] = rpc::JsonValue(static_cast<int64_t>(blockchain.back().header.targetBits));
        obj["bestblockhash"] = rpc::JsonValue(to_hex(blockchain.back().GetHash()));
        obj["peers"] = rpc::JsonValue(static_cast<int64_t>(netManager.getPeerCount()));
        return rpc::JsonValue(std::move(obj));
    }, false);

    rpcServer.register_method("getblocktemplate", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        (void)params;
        const Block &tip = blockchain.back();
        uint32_t nextHeight = static_cast<uint32_t>(blockchain.size());
        rpc::JsonObject obj;
        obj["previousblockhash"] = rpc::JsonValue(to_hex(tip.GetHash()));
        obj["merkleroot"] = rpc::JsonValue(to_hex(ToHash256(Keccak256(std::vector<uint8_t>{} ))));
        obj["height"] = rpc::JsonValue(static_cast<int64_t>(nextHeight));
        obj["target"] = rpc::JsonValue(to_hex(tip.header.merkleRoot));
        uint64_t coinbaseValue = EmissionModel::ComputeBlockReward(nextHeight);
        obj["coinbasevalue"] = rpc::JsonValue(static_cast<uint64_t>(coinbaseValue));
        obj["bits"] = rpc::JsonValue(static_cast<uint32_t>(tip.header.targetBits));
        obj["coinbasescript"] = rpc::JsonValue(std::string());
        return rpc::JsonValue(std::move(obj));
    }, false);

    rpcServer.register_method("sendrawtransaction", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        if (params.size() < 1) throw std::runtime_error("Missing raw transaction hex");
        std::string hex = params[0].as_string();
        std::vector<uint8_t> raw; raw.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            raw.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2), nullptr, 16)));
        }
        Hash256 txid = ToHash256(Keccak256(raw));
        return rpc::JsonValue(to_hex(txid));
    }, false);

    rpcServer.register_method("getbalance", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        std::string address;
        if (!params.empty() && params[0].is_string()) address = params[0].as_string();
        if (address.empty()) {
            return rpc::JsonValue(static_cast<uint64_t>(utxoSet.GetTotalSupply()));
        }
        uint64_t bal = 0;
        for (const auto &kv : utxoSet.GetUTXOs()) {
            const TxOut &out = kv.second;
            if (script_pub_key_matches_address(out.scriptPubKey, address)) bal += out.value;
        }
        return rpc::JsonValue(static_cast<uint64_t>(bal));
    }, false);

    rpcServer.register_method("listunspent", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        uint64_t minconf = 0, maxconf = 9999999;
        std::string address;
        if (params.size() > 0) minconf = parse_json_uint64(params[0]);
        if (params.size() > 1) maxconf = parse_json_uint64(params[1]);
        if (params.size() > 2 && params[2].is_string()) address = params[2].as_string();
        rpc::JsonArray result;
        for (const auto &kv : utxoSet.GetUTXOs()) {
            const OutPoint &op = kv.first;
            const TxOut &out = kv.second;
            if (!address.empty()) {
                if (!script_pub_key_matches_address(out.scriptPubKey, address)) continue;
            }
            rpc::JsonObject obj;
            obj["txid"] = rpc::JsonValue(to_hex(op.txHash));
            obj["vout"] = rpc::JsonValue(static_cast<int64_t>(op.vout));
            obj["amount"] = rpc::JsonValue(static_cast<uint64_t>(out.value));
            result.push_back(rpc::JsonValue(std::move(obj)));
        }
        return rpc::JsonValue(std::move(result));
    }, false);

    rpcServer.register_method("submitblock", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        if (params.size() < 1) throw std::runtime_error("Missing raw block hex");
        std::string hex = params[0].as_string();
        std::vector<uint8_t> raw; raw.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            raw.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2), nullptr, 16)));
        }
        size_t off = 0;
        auto read4 = [&](uint32_t &out) {
            out = raw[off] | (raw[off+1] << 8) | (raw[off+2] << 16) | (raw[off+3] << 24);
            off += 4;
        };
        Block block;
        read4(block.header.version);
        block.header.prevBlockHash = ToHash256(std::vector<uint8_t>(raw.begin()+off, raw.begin()+off+32)); off += 32;
        block.header.merkleRoot = ToHash256(std::vector<uint8_t>(raw.begin()+off, raw.begin()+off+32)); off += 32;
        read4(block.header.timestamp);
        read4(block.header.targetBits);
        read4(block.header.nonce);
        uint32_t txCount; read4(txCount);
        for (uint32_t i = 0; i < txCount; ++i) {
            Transaction tx;
            read4(tx.version);
            uint32_t inCount; read4(inCount);
            for (uint32_t j = 0; j < inCount; ++j) {
                TxIn in;
                in.prevTxHash = ToHash256(std::vector<uint8_t>(raw.begin()+off, raw.begin()+off+32)); off += 32;
                read4(in.prevVout);
                uint32_t sigSize; read4(sigSize);
                in.scriptSig.assign(raw.begin()+off, raw.begin()+off+sigSize);
                off += sigSize;
                read4(in.sequence);
                tx.inputs.push_back(in);
            }
            uint32_t outCount; read4(outCount);
            for (uint32_t j = 0; j < outCount; ++j) {
                TxOut out;
                uint64_t val = 0; for (int b = 0; b < 8; ++b) { val |= ((uint64_t)raw[off++]) << (8*b); }
                out.value = val;
                uint32_t pkSize; read4(pkSize);
                out.scriptPubKey.assign(raw.begin()+off, raw.begin()+off+pkSize);
                off += pkSize;
                tx.outputs.push_back(out);
            }
            read4(tx.lockTime);
            block.transactions.push_back(tx);
        }
        const Block *parent = blockchain.empty() ? nullptr : &blockchain.back();
        auto vr = consensus.ValidateBlock(block, parent);
        if (!vr.ok) throw std::runtime_error("Block validation failed: " + vr.reason);
        for (const auto &tx : block.transactions) {
            for (const auto &in : tx.inputs) {
                OutPoint op{in.prevTxHash, in.prevVout};
                TxOut dummy;
                utxoSet.RemoveCoin(op, dummy);
            }
            Hash256 txid = tx.GetHash();
            for (size_t idx = 0; idx < tx.outputs.size(); ++idx) {
                OutPoint op{txid, static_cast<uint32_t>(idx)};
                utxoSet.AddCoin(op, tx.outputs[idx]);
            }
        }
        blockchain.push_back(block);
        netManager.broadcastBlock(block);
        return rpc::JsonValue();
    }, false);

    netManager.startListening();
    std::vector<std::pair<std::string, uint16_t>> seeds = {{"seed1.auracash.org", p2pPort}, {"seed2.auracash.org", p2pPort}};
    for (auto &s : seeds) netManager.connectToPeer(s.first, s.second);

    if (!rpcServer.start()) {
        std::cerr << "Failed to start RPC server" << std::endl;
        return 1;
    }

    std::cout << "Node running. P2P on port " << p2pPort << ", RPC on port " << rpcPort << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;
    std::atomic<bool> running{true};
    while (running) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
