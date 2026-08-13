#pragma once
#include "CoreTypes.h"
#include "UTXOEngine.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdint>

namespace auracash {

// Block size cap = 2 MB (consensus rule)
constexpr size_t MAX_BLOCK_SIZE = 2 * 1024 * 1024;
// 1-minute block interval
constexpr uint32_t TARGET_BLOCK_TIME_SEC = 60;

// Validation result
struct ValidationResult {
    bool ok;
    std::string reason;

    static ValidationResult Ok() { return {true, "OK"}; }
    static ValidationResult Fail(const std::string& r) { return {false, r}; }
};

// Core block validation engine
class ConsensusEngine {
public:
    ConsensusEngine(UTXOSet& utxoSet);

    ValidationResult ValidateBlock(const Block& block, const Block* parentBlock);
    bool CheckProofOfWork(const Block& block) const;
    bool CheckBlockSize(const Block& block) const;
    bool CheckTimestamp(const Block& block, const Block* parentBlock) const;
    bool CheckParentHash(const Block& block, const Block* parentBlock) const;

    // Genesis block factory
    static Block GetGenesisBlock();

private:
    UTXOSet& m_utxos;
    std::unordered_map<std::string, Block> m_blockIndex;
};

} // namespace auracash
