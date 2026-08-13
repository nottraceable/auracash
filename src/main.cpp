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

using namespace auracash;

int main(int argc, char* argv[]) {
    std::cout << "AuraCash (XAC) v3.0 Node" << std::endl;
    std::cout << "Starting..." << std::endl;

    // Core components
    UTXOSet utxoSet;
    ConsensusEngine consensus(utxoSet);
    Mempool mempool;
    NetManager netManager(8333);
    rpc::RpcServer rpcServer(8332);

    // Simple in‑memory blockchain (vector of blocks)
    static std::vector<Block> blockchain;
    // Load genesis block and initialise UTXO set with its coinbase output
    blockchain.push_back(ConsensusEngine::GetGenesisBlock());
    const Block &gen = blockchain.back();
    if (!gen.transactions.empty()) {
        const Transaction &cb = gen.transactions[0];
        OutPoint out{cb.GetHash(), 0};
        utxoSet.AddCoin(out, cb.outputs[0]);
    }

    // -------------------------- RPC METHODS --------------------------
    // getblockchaininfo – returns basic chain status
    rpcServer.register_method("getblockchaininfo", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        rpc::JsonObject obj;
        obj["chain"] = rpc::JsonValue("mainnet");
        obj["blocks"] = rpc::JsonValue(static_cast<int64_t>(blockchain.size() - 1));
        obj["difficulty"] = rpc::JsonValue(static_cast<int64_t>(blockchain.back().header.targetBits));
        obj["bestblockhash"] = rpc::JsonValue(to_hex(blockchain.back().GetHash()));
        obj["peers"] = rpc::JsonValue(static_cast<int64_t>(netManager.getPeerCount()));
        return rpc::JsonValue(std::move(obj));
    }, false);

    // getblocktemplate – provide data for miners.
    rpcServer.register_method("getblocktemplate", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        const Block &tip = blockchain.back();
        uint32_t nextHeight = static_cast<uint32_t>(blockchain.size());
        rpc::JsonObject obj;
        obj["previousblockhash"] = rpc::JsonValue(to_hex(tip.GetHash()));
        // Dummy merkle root for demo (empty keccak)
        obj["merkleroot"] = rpc::JsonValue(to_hex(ToHash256(Keccak256(std::vector<uint8_t>{}))));
        obj["height"] = rpc::JsonValue(static_cast<int64_t>(nextHeight));
        obj["target"] = rpc::JsonValue(to_hex(tip.header.merkleRoot)); // basic target (can be refined)
        uint64_t coinbaseValue = EmissionModel::ComputeBlockReward(nextHeight);
        obj["coinbasevalue"] = rpc::JsonValue(static_cast<uint64_t>(coinbaseValue));
        obj["bits"] = rpc::JsonValue(static_cast<uint32_t>(tip.header.targetBits));
        // Reserve coinbaseScript for future completeness
        obj["coinbasescript"] = rpc::JsonValue(std::string());
        return rpc::JsonValue(std::move(obj));
    }, false);

    // sendrawtransaction – accept a raw tx hex, return its txid (no validation for demo).
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

    // getbalance – return total UTXO value (ignores address for demo).
    rpcServer.register_method("getbalance", [&](const rpc::JsonArray &params) -> rpc::JsonValue {
        uint64_t total = utxoSet.GetTotalSupply();
        return rpc::JsonValue(static_cast<uint64_t>(total));
    }, false);

    // submitblock – deserialize, validate, apply to chain and UTXO set.
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
        // Header
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
        // Apply UTXO changes
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
        return rpc::JsonValue(); // empty result per spec
    }, false);

    // -----------------------------------------------------------------
    netManager.startListening();
    // Auto‑connect to seed nodes (hard‑coded list)
    std::vector<std::pair<std::string, uint16_t>> seeds = {{"seed1.auracash.org", 8333}, {"seed2.auracash.org", 8333}};
    for (auto &s : seeds) {
        netManager.connectToPeer(s.first, s.second);
    }

    if (!rpcServer.start()) {
        std::cerr << "Failed to start RPC server" << std::endl;
        return 1;
    }

    std::cout << "Node running. P2P on port 8333, RPC on port 8332" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    std::atomic<bool> running{false};
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}