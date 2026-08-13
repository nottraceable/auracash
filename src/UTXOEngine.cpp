/**
 * @file UTXOEngine.cpp
 * @brief Standard UTXO state validation engine.
 */

#include "UTXOEngine.h"
#include <stdexcept>

namespace auracash {

UTXOSet::UTXOSet() {}

bool UTXOSet::AddCoin(const OutPoint& out, const TxOut& txo) {
    if (m_utxos.find(out) != m_utxos.end()) return false;
    m_utxos[out] = txo;
    return true;
}

bool UTXOSet::RemoveCoin(const OutPoint& out, TxOut& txo) {
    auto it = m_utxos.find(out);
    if (it == m_utxos.end()) return false;
    txo = it->second;
    m_utxos.erase(it);
    return true;
}

bool UTXOSet::HasCoin(const OutPoint& out) const {
    return m_utxos.find(out) != m_utxos.end();
}

bool UTXOSet::ValidateAndApplyInputs(const Transaction& tx) {
    if (tx.inputs.empty()) {
        return false; // Empty inputs are not allowed (except coinbase which is handled externally)
    }

    // Coinbase tx handling (single input, prev_vout = 0xffffffff, prev_hash zero):
    if (tx.IsCoinbase()) {
        for (const auto& out : tx.outputs) {
            if (!out.IsValid()) return false;
        }
        return true;
    }

    // Standard UTXO: each input must reference an existing UTXO not yet spent in this block.
    uint64_t totalIn = 0;
    for (const auto& in : tx.inputs) {
        OutPoint op{in.prevTxHash, in.prevVout};
        if (m_spentThisBlock.count(op) > 0) {
            return false; // double-spend within the block
        }
        auto it = m_utxos.find(op);
        if (it == m_utxos.end()) {
            return false; // unknown/unspent input
        }
        totalIn += it->second.value;
        // For each input, the UTXO is consumed, but actual erase happens in Commit
        m_spentThisBlock.insert(op);
    }

    uint64_t totalOut = tx.GetTotalOutput();
    if (totalOut > totalIn) {
        return false; // inflation: spent more than input
    }

    return true;
}

void UTXOSet::CommitBlock() {
    // The simplest approach: real validation engine separates spent tracking from actual
    // removal. Since we are doing single-tx validation in the loop, by the time CommitBlock
    // is called, the m_utxos still has the consumed entries; we must remove them now.
    // However, ValidateAndApplyInputs does NOT erase m_utxos — caller must erase.
    // To make this clean: real engine should remove inside ValidateAndApplyInputs (per block-level tx).
    // We'll leave m_spentThisBlock set populated from validation; remove those now.
    for (const auto& op : m_spentThisBlock) {
        m_utxos.erase(op);
    }
    m_spentThisBlock.clear();
}

void UTXOSet::RevertBlock() {
    m_spentThisBlock.clear();
}

uint64_t UTXOSet::GetTotalSupply() const {
    uint64_t total = 0;
    for (const auto& kv : m_utxos) total += kv.second.value;
    return total;
}

void UTXOSet::Clear() {
    m_utxos.clear();
    m_spentThisBlock.clear();
}

} // namespace auracash
