#include "CoreTypes.h"
#include "keccak256.h"

namespace auracash {

static void push4(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
}

static void push8(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

Hash256 Transaction::GetHash() const {
    std::vector<uint8_t> data;
    push4(data, version);
    uint32_t inCount = static_cast<uint32_t>(inputs.size());
    push4(data, inCount);
    for (const auto& in : inputs) {
        data.insert(data.end(), in.prevTxHash.begin(), in.prevTxHash.end());
        push4(data, in.prevVout);
        uint32_t sigSize = static_cast<uint32_t>(in.scriptSig.size());
        push4(data, sigSize);
        data.insert(data.end(), in.scriptSig.begin(), in.scriptSig.end());
        push4(data, in.sequence);
    }
    uint32_t outCount = static_cast<uint32_t>(outputs.size());
    push4(data, outCount);
    for (const auto& out : outputs) {
        push8(data, out.value);
        uint32_t pkSize = static_cast<uint32_t>(out.scriptPubKey.size());
        push4(data, pkSize);
        data.insert(data.end(), out.scriptPubKey.begin(), out.scriptPubKey.end());
    }
    push4(data, lockTime);
    return ToHash256(Keccak256(data));
}

uint64_t Transaction::GetTotalOutput() const {
    uint64_t total = 0;
    for (const auto& out : outputs) total += out.value;
    return total;
}

Hash256 Block::GetHash() const {
    return ToHash256(Keccak256(header.ToBytes()));
}

size_t Block::GetSize() const {
    size_t total = header.ToBytes().size();
    for (const auto& tx : transactions) {
        std::vector<uint8_t> data;
        push4(data, tx.version);
        push4(data, static_cast<uint32_t>(tx.inputs.size()));
        for (const auto& in : tx.inputs) {
            data.insert(data.end(), in.prevTxHash.begin(), in.prevTxHash.end());
            push4(data, in.prevVout);
            push4(data, static_cast<uint32_t>(in.scriptSig.size()));
            data.insert(data.end(), in.scriptSig.begin(), in.scriptSig.end());
            push4(data, in.sequence);
        }
        push4(data, static_cast<uint32_t>(tx.outputs.size()));
        for (const auto& out : tx.outputs) {
            push8(data, out.value);
            push4(data, static_cast<uint32_t>(out.scriptPubKey.size()));
            data.insert(data.end(), out.scriptPubKey.begin(), out.scriptPubKey.end());
        }
        push4(data, tx.lockTime);
        total += data.size();
    }
    return total;
}

} // namespace auracash
