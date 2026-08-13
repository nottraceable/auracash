#include "Wallet.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <string>

namespace auracash {
namespace wallet {

namespace {

uint64_t parse_json_uint64(const auracash::rpc::JsonValue& val) {
    if (val.is_uint64()) return val.as_uint64();
    if (val.is_int64()) return static_cast<uint64_t>(val.as_int64());
    if (val.is_string()) {
        try { return std::stoull(val.as_string()); } catch (...) { return 0; }
    }
    return 0;
}

std::vector<uint8_t> build_p2pkh_script(const std::string& address) {
    std::vector<uint8_t> pkHash(20, 0);
    if (!address.empty()) {
        // Simple: treat raw address string bytes as script prefix with first 20 bytes as hash
        for (size_t i = 0; i < std::min(address.size(), static_cast<size_t>(20)); ++i) {
            pkHash[i] = static_cast<uint8_t>(address[i]);
        }
    }
    std::vector<uint8_t> script = {0x76, 0xA9, 0x14};
    script.insert(script.end(), pkHash.begin(), pkHash.end());
    script.push_back(0x88);
    script.push_back(0xAC);
    return script;
}

} // namespace

std::vector<uint8_t> serialize_transaction(const Transaction& tx);

Wallet::Wallet(const std::string& rpcHost, uint16_t rpcPort)
    : m_rpcHost(rpcHost), m_rpcPort(rpcPort), m_client(rpcHost, rpcPort) {}

Wallet::~Wallet() {}

std::optional<Keypair> Wallet::generate_keypair() {
    uint256 priv, pub;
    if (!m_crypto.generate_keypair(priv, pub)) {
        return std::nullopt;
    }
    std::string addr = m_crypto.public_key_to_address(pub);
    return Keypair{priv, pub, addr};
}

bool Wallet::get_balance(const std::string& address, uint64_t& balance) {
    auracash::rpc::JsonArray params;
    params.push_back(auracash::rpc::JsonValue(address));
    params.push_back(auracash::rpc::JsonValue(static_cast<uint64_t>(0)));
    
    auracash::rpc::JsonValue result;
    if (!m_client.call("getbalance", params, result)) {
        std::cerr << "[wallet] getbalance failed\n";
        return false;
    }
    
    balance = parse_json_uint64(result);
    return true;
}

bool Wallet::fetch_utxos(const std::string& address, std::vector<Utxo>& utxos) {
    auracash::rpc::JsonArray params;
    params.push_back(auracash::rpc::JsonValue(static_cast<uint64_t>(0)));
    params.push_back(auracash::rpc::JsonValue(static_cast<uint64_t>(999999)));
    params.push_back(auracash::rpc::JsonValue(address));
    
    auracash::rpc::JsonValue result;
    if (!m_client.call("listunspent", params, result)) {
        std::cerr << "[wallet] listunspent failed\n";
        return false;
    }
    if (!result.is_array()) {
        std::cerr << "[wallet] listunspent result not array\n";
        return false;
    }
    utxos.clear();
    for (const auto& item : result.as_array()) {
        if (!item.is_object()) continue;
        const auto& obj = item.as_object();
        if (!obj.contains("txid") || !obj.contains("vout")) continue;
        
        Hash256 txid;
        std::string txidHex = obj.at("txid").as_string();
        auracash::uint256 tmp(txidHex);
        std::memcpy(txid.data(), tmp.data, 32);
        
        Utxo u;
        u.txid = txid;
        u.vout = static_cast<uint32_t>(parse_json_uint64(obj.at("vout")));
        
        if (obj.contains("amount")) {
            u.value = parse_json_uint64(obj.at("amount"));
        } else if (obj.contains("value")) {
            u.value = parse_json_uint64(obj.at("value"));
        } else {
            u.value = 0;
        }
        
        utxos.push_back(u);
    }
    return true;
}

std::optional<std::string> Wallet::create_transaction(
    const std::string& fromAddress,
    const std::string& toAddress,
    uint64_t amount,
    const uint256& privKey,
    uint64_t fee) {

    std::vector<Utxo> utxos;
    if (!fetch_utxos(fromAddress, utxos)) {
        return std::nullopt;
    }
    if (utxos.empty()) {
        std::cerr << "[wallet] No UTXOs found\n";
        return std::nullopt;
    }
    std::vector<std::pair<Hash256, uint32_t>> inputs;
    uint64_t totalIn = 0;
    for (const auto& u : utxos) {
        inputs.emplace_back(u.txid, u.vout);
        totalIn += u.value;
        if (totalIn >= amount + fee) break;
    }
    if (totalIn < amount + fee) {
        std::cerr << "[wallet] Insufficient funds\n";
        return std::nullopt;
    }

    Transaction tx;
    tx.version = 1;
    tx.lockTime = 0;
    for (auto& [prevHash, vout] : inputs) {
        TxIn in;
        in.prevTxHash = prevHash;
        in.prevVout = vout;
        in.scriptSig = {};
        in.sequence = 0xffffffff;
        tx.inputs.push_back(in);
    }
    TxOut outTo;
    outTo.value = amount;
    outTo.scriptPubKey = build_p2pkh_script(toAddress);
    tx.outputs.push_back(outTo);
    uint64_t change = totalIn - amount - fee;
    if (change > 0) {
        TxOut outChange;
        outChange.value = change;
        outChange.scriptPubKey = build_p2pkh_script(fromAddress);
        tx.outputs.push_back(outChange);
    }

    Hash256 txHash = tx.GetHash();
    uint256 txHashU256(txHash.data(), 32);
    Signature sig;
    if (!m_crypto.sign(txHashU256, privKey, sig)) {
        return std::nullopt;
    }
    std::vector<uint8_t> sigSerialized(64);
    std::memcpy(sigSerialized.data(), sig.data, 64);
    for (auto& in : tx.inputs) {
        in.scriptSig = sigSerialized;
    }

    std::vector<uint8_t> raw = serialize_transaction(tx);
    std::string rawHex;
    for (uint8_t b : raw) {
        char hb[3];
        snprintf(hb, sizeof(hb), "%02x", b);
        rawHex += hb;
    }
    return rawHex;
}

bool Wallet::broadcast_transaction(const std::string& rawTxHex) {
    auracash::rpc::JsonArray params;
    params.push_back(auracash::rpc::JsonValue(rawTxHex));
    params.push_back(auracash::rpc::JsonValue(static_cast<uint64_t>(1)));
    
    auracash::rpc::JsonValue result;
    if (!m_client.call("sendrawtransaction", params, result)) {
        std::cerr << "[wallet] broadcast failed\n";
        return false;
    }
    std::string txid = result.is_string() ? result.as_string() : "unknown";
    std::cout << "[wallet] Transaction broadcast with id " << txid << "\n";
    return true;
}

std::vector<uint8_t> serialize_transaction(const Transaction& tx) {
    std::vector<uint8_t> out;
    auto push4 = [&](uint32_t v) {
        out.push_back(v & 0xFF);
        out.push_back((v >> 8) & 0xFF);
        out.push_back((v >> 16) & 0xFF);
        out.push_back((v >> 24) & 0xFF);
    };
    auto push8 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (8*i)) & 0xFF));
    };
    push4(tx.version);
    push4(static_cast<uint32_t>(tx.inputs.size()));
    for (const auto& in : tx.inputs) {
        out.insert(out.end(), in.prevTxHash.begin(), in.prevTxHash.end());
        push4(in.prevVout);
        push4(static_cast<uint32_t>(in.scriptSig.size()));
        out.insert(out.end(), in.scriptSig.begin(), in.scriptSig.end());
        push4(in.sequence);
    }
    push4(static_cast<uint32_t>(tx.outputs.size()));
    for (const auto& o : tx.outputs) {
        push8(o.value);
        push4(static_cast<uint32_t>(o.scriptPubKey.size()));
        out.insert(out.end(), o.scriptPubKey.begin(), o.scriptPubKey.end());
    }
    push4(tx.lockTime);
    return out;
}

} // namespace wallet
} // namespace auracash
