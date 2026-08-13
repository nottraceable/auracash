#pragma once
#include "CoreTypes.h"
#include <unordered_map>
#include <set>
#include <string>
#include <cstdint>

namespace auracash {

struct OutPoint {
    Hash256 txHash;
    uint32_t vout;
    bool operator==(const OutPoint& other) const {
        return txHash == other.txHash && vout == other.vout;
    }
    bool operator<(const OutPoint& other) const {
        int cmp = memcmp(txHash.data(), other.txHash.data(), txHash.size());
        if (cmp != 0) return cmp < 0;
        return vout < other.vout;
    }
};

} // namespace auracash

namespace std {
template<>
struct hash<auracash::OutPoint> {
    size_t operator()(const auracash::OutPoint& o) const {
        size_t h1 = std::hash<std::string>{}(std::string(reinterpret_cast<const char*>(o.txHash.data()), o.txHash.size()));
        size_t h2 = std::hash<uint32_t>{}(o.vout);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std

namespace auracash {

class UTXOSet {
public:
    UTXOSet();

    bool AddCoin(const OutPoint& out, const TxOut& txo);
    bool RemoveCoin(const OutPoint& out, TxOut& txo);
    bool HasCoin(const OutPoint& out) const;
    bool ValidateAndApplyInputs(const Transaction& tx);
    void CommitBlock();
    void RevertBlock();
    uint64_t GetTotalSupply() const;
    void Clear();

    // New getter to expose current UTXOs
    const std::unordered_map<OutPoint, TxOut>& GetUTXOs() const { return m_utxos; }

private:
    std::unordered_map<OutPoint, TxOut> m_utxos;
    std::set<OutPoint> m_spentThisBlock;
};

} // namespace auracash
