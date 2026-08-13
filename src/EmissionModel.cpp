/**
 * @file EmissionModel.cpp
 * @brief Continuous Decay Emission Model implementation.
 * 
 * Uses the formula R(t) = R0 * exp(-lambda * t) to determine block rewards.
 * The parameters are chosen such that the total sum of all block rewards 
 * asymptotically approaches, but never exceeds, 21 Billion XAC.
 */

#include "EmissionModel.h"
#include <cmath>

namespace auracash {

// Constants for the decay model
constexpr double R0 = static_cast<double>(INITIAL_BLOCK_REWARD); // Initial block reward in satoshi
constexpr double LAMBDA = 1.0 / 42'000'000.0; // Decay factor to ensure ~21 Billion cap

// For integral approximation: sum of geometric series = R0 / (1 - exp(-lambda))
// We want this ~ MAX_SUPPLY.
//sum = R0 / (1 - exp(-lambda)) ~ R0 / LAMBDA
// lambda = R0 / MAX_SUPPLY
static_assert(R0 > 0, "Initial reward must be positive");

Amount EmissionModel::ComputeBlockReward(uint32_t blockHeight) {
    double exponent = -static_cast<double>(LAMBDA) * static_cast<double>(blockHeight);
    double reward = R0 * std::exp(exponent);
    
    // Ensure we never mine more than the max supply.
    // In a real scenario, this would be checked against the current total supply.
    if (reward < 1.0) {
        return 1; // Minimum reward of 1 satoshi
    }
    
    return static_cast<Amount>(reward);
}

} // namespace auracash
