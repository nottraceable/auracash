#pragma once
#include "CoreTypes.h"
#include <vector>
#include <cstdint>

namespace auracash {

std::vector<uint8_t> GenerateSeed(const BlockHeader& header, uint32_t nonce);
std::vector<uint8_t> AuraHash(const BlockHeader& header, uint32_t nonce);
bool VerifyAuraHash(const BlockHeader& header, uint32_t nonce, const Hash256& target);
uint32_t AuraHashMine(const BlockHeader& header, const Hash256& target);

} // namespace auracash
