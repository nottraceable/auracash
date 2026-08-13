/**
 * @file Consensus.cpp
 * @brief Core validation rules implementation.
 */

#include "Consensus.h"
#include "AuraHash.h"
#include "EmissionModel.h"
#include "UTXOEngine.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace auracash {

ConsensusEngine::ConsensusEngine(UTXOSet& utxoSet) : m_utxos(utxoSet) {}

ValidationResult ConsensusEngine::ValidateBlock(const Block& block, const Block* parentBlock) {
    if (!parentBlock) {
        // Genesis block: no parent, so we skip parent hash and PoW checks.
        // But we still validate transactions, block size, etc.
    } else {
        if (!CheckParentHash(block, parentBlock)) {
            return ValidationResult::Fail("Invalid parent hash link");
        }
        if (!CheckTimestamp(block, parentBlock)) {
            return ValidationResult::Fail("Invalid timestamp");
        }
        if (!CheckProofOfWork(block)) {
            return ValidationResult::Fail("Proof of work failed");
        }
    }
    if (!CheckBlockSize(block)) {
        return ValidationResult::Fail("Block size exceeds 2 MB limit");
    }
    // Validate UTXO transactions sequentially
    for (const auto& tx : block.transactions) {
        if (!m_utxos.ValidateAndApplyInputs(tx)) {
            return ValidationResult::Fail("Invalid UTXO spend or double-spend detected");
        }
    }
    // Apply UTXO changes
    m_utxos.CommitBlock();

    return ValidationResult::Ok();
}

bool ConsensusEngine::CheckProofOfWork(const Block& block) const {
    // Get target from block header (compact form)
    uint32_t targetCompact = block.header.targetBits;
    // Convert to uint256 target
    // For simplicity, we assume targetCompact is a compact representation as in Bitcoin.
    // We'll implement a basic conversion: target = (targetCompact & 0x007FFFFF) * 2^(8*((targetCompact>>24)-3))
    // This is a simplified version and may not be exactly as in Bitcoin but sufficient for demonstration.
    int32_t exponent = (targetCompact >> 24) & 0xFF;
    uint32_t mantissa = targetCompact & 0x007FFFFF;
    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
    } else {
        mantissa <<= 8 * (exponent - 3);
    }
    // Now we have mantissa as the target in the least significant 256 bits? Actually, it's the target with the given exponent.
    // We'll just compare the hash to a target derived from mantissa shifted by (exponent*8) bits? 
    // Instead, let's use a simpler approach: we require that the hash is less than a target where the target is 
    //   target = mantissa * 2^(8*(exponent-3))
    // and we compare the hash as a 256-bit number.
    // We'll convert the hash to a uint256 (using two 128-bit parts) and compare.
    // For brevity, we'll just check that the hash has a certain number of leading zero bytes based on the target.
    // Since we don't have a full uint256, we'll do a simple check: if the first 4 bytes of the hash are zero, then accept? Not accurate.
    // Given the complexity, we'll assume that the target is already set to a difficulty that we can check by comparing the hash to a threshold.
    // We'll use the following: the hash must be less than (targetCompact) when interpreted as a 256-bit number in little-endian? Not exactly.
    // Let's do a simple check: require that the hash is less than or equal to the target where target is represented by the compact form.
    // We'll convert the compact target to a 256-bit integer and compare with the hash (also as 256-bit integer).
    // We'll implement a simple uint256 class for comparison, but to avoid bloating, we'll use the existing Hash256 and compare lexicographically.
    // Note: In Bitcoin, the target is a 256-bit number and the hash must be less than or equal to it.
    // We'll implement a comparison function for Hash256 as an unsigned 256-bit integer in big-endian (the hash is stored as is, and we compare as big-endian numbers).
    // However, the compact target is usually interpreted as a mantissa and exponent, and the target is mantissa * 2^(8*(exponent-3)).
    // We'll compute the target as a Hash256 in big-endian.

    // Convert compact target to a 256-bit target (big-endian)
    Hash256 target{};
    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
    } else {
        mantissa <<= 8 * (exponent - 3);
    }
    // Now mantissa is the target with the least significant bits set, but we need to place it in the 256-bit number.
    // The target is mantissa * 2^(8*(exponent-3)), so we shift mantissa left by (exponent-3)*8 bytes? Actually, the formula in Bitcoin:
    // target = mantissa * 2^(8*(exponent - 3))
    // where the target is a 256-bit number.
    // We'll represent the target as a big-endian array of 32 bytes.
    // We'll put the mantissa in the least significant bytes and shift left by (exponent-3)*8 bytes (i.e., add zeros to the left).
    // But note: the mantissa is 24 bits (3 bytes). So we have:
    //   target[0..29] = 0
    //   target[30] = (mantissa >> 16) & 0xFF
    //   target[31] = (mantissa >> 8) & 0xFF
    //   target[32] = mantissa & 0xFF   -> but we only have 32 bytes, index 0-31.
    // Actually, we want the mantissa to be in the least significant 3 bytes, and then shift left by (exponent-3)*8 bytes.
    // So the mantissa starts at byte position (exponent-3) from the left? Let's think in terms of big-endian: the most significant byte is at index 0.
    // If we want to shift left by n*8 bytes, we add n zero bytes at the most significant side.
    // So the mantissa will be placed at the least significant side, and we add (exponent-3) zero bytes at the most significant side.
    // But note: the total size is 32 bytes. So we can only shift up to 29 bytes (leaving 3 bytes for mantissa).
    // We'll do:
    int shiftBytes = exponent - 3;
    if (shiftBytes < 0) shiftBytes = 0;
    if (shiftBytes > 29) shiftBytes = 29; // mantissa occupies 3 bytes
    // Clear target
    std::fill(target.begin(), target.end(), 0);
    // Put mantissa in the last 3 bytes, shifted left by shiftBytes (i.e., starting at index shiftBytes from the left?).
    // Actually, we want the mantissa to be in the least significant part, so we put it at the end and then shift left by adding zeros in front.
    // In big-endian, the most significant byte is at index 0. Shifting left by n bytes means moving the mantissa to higher addresses (toward index 0) and filling the lower addresses with zeros? 
    // Example: if we have mantissa 0x123456 and we shift left by 4 bytes (32 bits), we get 0x12345600000000 in a 64-bit number.
    // In big-endian bytes, the number 0x12345600000000 is stored as: [0x12,0x34,0x56,0x00,0x00,0x00,0x00,0x00]
    // So the mantissa bytes are at the front, and zeros at the back.
    // Therefore, to represent mantissa * 256^shiftBytes, we put the mantissa in the first 3 bytes and then shiftBytes zeros after? 
    // Actually, no: shifting left by n bytes in a big-endian representation means moving the mantissa toward the most significant side and adding zeros at the least significant side.
    // Let's do: target[0] = (mantissa >> 16) & 0xFF; target[1] = (mantissa >> 8) & 0xFF; target[2] = mantissa & 0xFF; and then the rest (from index 3 to 31) are zeros? 
    // But then we haven't shifted by exponent-3. We need to shift left by (exponent-3)*8 bytes, which means we add (exponent-3) zero bytes at the least significant side.
    // So the mantissa should be at the most significant side, and then we add zeros at the least significant side.
    // Therefore, we set:
    //   for i = 0 to 2: target[i] = (mantissa >> (8*(2-i))) & 0xFF;
    //   and then we leave the next (exponent-3) bytes as zero? Actually, no: we want to shift left by (exponent-3)*8 bytes, which means we multiply by 256^(exponent-3).
    //   In big-endian, multiplying by 256^n shifts the number to the left by n bytes, adding n zero bytes at the least significant end.
    //   So the mantissa will occupy the most significant 3 bytes, and then we have (exponent-3) zero bytes, and then the rest zeros to fill 32 bytes.
    //   But note: if exponent-3 is large, we may exceed 32 bytes.
    //   We'll limit the shift so that the mantissa plus shiftBytes does not exceed 32.
    //   We want to represent the target as a 32-byte big-endian number.
    //   Let available = 32 - 3; // bytes left for shift and padding
    //   shiftBytes = std::min(exponent - 3, available);
    //   Then we set the first 3 bytes to mantissa, and then shiftBytes zeros, and then the rest zeros (but actually we already set to zero).
    //   However, note that the target is mantissa * 256^(exponent-3). If exponent-3 is negative, we divide.
    //   We already handled exponent<=3 by shifting the mantissa right.
    //   So for exponent>3, we shift left.
    //   Let's do:
    if (exponent > 3) {
        int shiftBytes = exponent - 3;
        if (shiftBytes > 29) shiftBytes = 29; // so that mantissa (3 bytes) + shiftBytes <= 32
        // mantissa goes to the most significant 3 bytes
        target[0] = (mantissa >> 16) & 0xFF;
        target[1] = (mantissa >> 8) & 0xFF;
        target[2] = mantissa & 0xFF;
        // the next shiftBytes bytes are already zero (from initialization)
        // the rest are zero.
    } else {
        // exponent <= 3: we already shifted mantissa right by 8*(3-exponent) above.
        // Now we put the mantissa in the least significant 3 bytes.
        target[29] = (mantissa >> 16) & 0xFF;
        target[30] = (mantissa >> 8) & 0xFF;
        target[31] = mantissa & 0xFF;
    }

    // Now compare the block hash (as big-endian) with the target.
    // We require hash <= target.
    Hash256 hash = block.GetHash();
    for (int i = 0; i < 32; ++i) {
        if (hash[i] < target[i]) return true;   // hash is less than target
        if (hash[i] > target[i]) return false;  // hash is greater than target
    }
    return true; // equal
}

bool ConsensusEngine::CheckBlockSize(const Block& block) const {
    return block.GetSize() <= MAX_BLOCK_SIZE;
}

bool ConsensusEngine::CheckTimestamp(const Block& block, const Block* parentBlock) const {
    if (!parentBlock) return true; // genesis has no parent
    // Simple rule: timestamp must be greater than parent's timestamp and less than now + 2 hours?
    // For simplicity, we just check that it's strictly greater than the parent's timestamp.
    return block.header.timestamp > parentBlock->header.timestamp;
}

bool ConsensusEngine::CheckParentHash(const Block& block, const Block* parentBlock) const {
    return block.header.prevBlockHash == parentBlock->GetHash();
}

// Genesis block factory
Block ConsensusEngine::GetGenesisBlock() {
    Block genesis;
    genesis.header.version = 1;
    genesis.header.prevBlockHash.fill(0);
    genesis.header.merkleRoot.fill(0);
    genesis.header.timestamp = 1609459200; // 2021-01-01 00:00:00 UTC
    genesis.header.targetBits = 0x207fffff; // example compact target
    genesis.header.nonce = 0;

    // Create a coinbase transaction for genesis
    Transaction coinbase;
    coinbase.version = 1;
    coinbase.inputs.resize(1);
    coinbase.inputs[0].prevTxHash.fill(0);
    coinbase.inputs[0].prevVout = 0xffffffff;
    coinbase.inputs[0].scriptSig = std::vector<uint8_t>{0x04, 0xff, 0xff, 0x00, 0x1d, 0x01, 0x04};
    coinbase.outputs.resize(1);
    coinbase.outputs[0].value = EmissionModel::ComputeBlockReward(0);
    coinbase.outputs[0].scriptPubKey = std::vector<uint8_t>{0x76, 0xa9, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0xac};
    coinbase.lockTime = 0;

    genesis.transactions.push_back(coinbase);
    // Merkle root is the hash of the coinbase transaction (since it's the only transaction)
    genesis.header.merkleRoot = coinbase.GetHash();

    return genesis;
}

} // namespace auracash
