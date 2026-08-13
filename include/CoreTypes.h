#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <cstring>

namespace auracash {

// 256-bit hash as 32 bytes
using Hash256 = std::array<uint8_t, 32>;

inline Hash256 ToHash256(const std::vector<uint8_t>& v) {
    Hash256 h;
    if (v.size() >= 32) std::memcpy(h.data(), v.data(), 32);
    else {
        std::memcpy(h.data(), v.data(), v.size());
        for (size_t i = v.size(); i < 32; ++i) h[i] = 0;
    }
    return h;
}

inline std::string to_hex(const Hash256& h) {
    std::string hex;
    for (uint8_t b : h) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
    }
    return hex;
}

inline bool IsZeroHash(const Hash256& h) {
    for (uint8_t b : h) if (b != 0) return false;
    return true;
}

struct BlockHeader {
    uint32_t version = 1;
    Hash256 prevBlockHash;
    Hash256 merkleRoot;
    uint32_t timestamp = 0;
    uint32_t targetBits = 0; // compact target
    uint32_t nonce = 0;

    std::vector<uint8_t> ToBytes() const {
        std::vector<uint8_t> out(80); // 4 + 32 + 32 + 4 + 4 + 4 = 80 bytes
        size_t off = 0;
        auto push4 = [&](uint32_t v) {
            out[off++] = v & 0xFF;
            out[off++] = (v >> 8) & 0xFF;
            out[off++] = (v >> 16) & 0xFF;
            out[off++] = (v >> 24) & 0xFF;
        };
        push4(version);
        std::memcpy(&out[off], prevBlockHash.data(), 32); off += 32;
        std::memcpy(&out[off], merkleRoot.data(), 32); off += 32;
        push4(timestamp);
        push4(targetBits);
        push4(nonce);
        return out;
    }
};

struct TxOut {
    uint64_t value;
    std::vector<uint8_t> scriptPubKey;
    bool IsValid() const { return true; }
};

struct TxIn {
    Hash256 prevTxHash;
    uint32_t prevVout;
    std::vector<uint8_t> scriptSig;
    uint32_t sequence = 0;
};

struct Transaction {
    uint32_t version = 1;
    std::vector<TxIn> inputs;
    std::vector<TxOut> outputs;
    uint32_t lockTime = 0;

    Hash256 GetHash() const;
    uint64_t GetTotalOutput() const;
    bool IsCoinbase() const { return inputs.size() == 1 && IsZeroHash(inputs[0].prevTxHash) && inputs[0].prevVout == 0xffffffff; }
};

struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;

    Hash256 GetHash() const;
    size_t GetSize() const; // serialized size estimate
};

} // namespace auracash