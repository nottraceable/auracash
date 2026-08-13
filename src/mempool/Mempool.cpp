#include "Mempool.h"
#include "UTXOEngine.h"
#include <openssl/sha.h>

namespace auracash {

bool Mempool::add_transaction(const Transaction& tx, const UTXOSet& utxoSet, Secp256k1& crypto) {
    // Compute transaction hash
    Hash256 txid = tx.GetHash();
    if (m_txs.find(txid) != m_txs.end()) return false; // duplicate

    // Coinbase not allowed in mempool
    if (tx.IsCoinbase()) return false;

    // Verify each input exists in UTXO and not already spent in mempool
    for (const auto& in : tx.inputs) {
        OutPoint op{in.prevTxHash, in.prevVout};
        // Check if already spent in another pending tx
        if (m_spentOutputs.find(op) != m_spentOutputs.end()) return false;
        // Check UTXO existence
        if (!utxoSet.HasCoin(op)) return false;
    }

    // All checks passed, add to mempool
    TxEntry entry{txid, &tx, {}};
    for (const auto& in : tx.inputs) {
        OutPoint op{in.prevTxHash, in.prevVout};
        entry.spentOutputs.insert(op);
        m_spentOutputs[op] = txid;
    }
    m_txs[txid] = entry;
    return true;
}

bool Mempool::remove_transaction(const Hash256& txid) {
    auto it = m_txs.find(txid);
    if (it == m_txs.end()) return false;
    for (const auto& op : it->second.spentOutputs) {
        m_spentOutputs.erase(op);
    }
    m_txs.erase(it);
    return true;
}

bool Mempool::has_transaction(const Hash256& txid) const {
    return m_txs.find(txid) != m_txs.end();
}

std::vector<const Transaction*> Mempool::get_transactions() const {
    std::vector<const Transaction*> txs;
    for (const auto& kv : m_txs) {
        txs.push_back(kv.second.tx);
    }
    return txs;
}

} // namespace auracash