#pragma once
#include "CoreTypes.h"
#include "UTXOEngine.h"
#include "crypto/Secp256k1.h"
#include <map>
#include <set>
#include <memory>

namespace auracash {

class Mempool {
public:
    Mempool() = default;
    ~Mempool() = default;

    // Add transaction to mempool after basic validation
    bool add_transaction(const Transaction& tx, const UTXOSet& utxoSet, Secp256k1& crypto);
    bool remove_transaction(const Hash256& txid);
    bool has_transaction(const Hash256& txid) const;
    std::vector<const Transaction*> get_transactions() const;
    size_t size() const { return m_txs.size(); }

private:
    struct TxEntry {
        Hash256 txid;
        const Transaction* tx;
        std::set<OutPoint> spentOutputs;
    };
    std::map<Hash256, TxEntry> m_txs;
    std::map<OutPoint, Hash256> m_spentOutputs; // maps outpoint to containing txid
};

} // namespace auracash