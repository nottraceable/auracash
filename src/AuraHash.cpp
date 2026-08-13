#include "AuraHash.h"
#include "keccak256.h"
#include "CoreTypes.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace auracash {

static_assert(sizeof(double) == 8, "Double must be 8 bytes");

static uint64_t DoubleToU64(double v) {
    uint64_t u;
    static_assert(sizeof(v) == sizeof(u));
    std::memcpy(&u, &v, sizeof(u));
    return u;
}

static double U64ToDouble(uint64_t u) {
    double v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

std::vector<uint8_t> GenerateSeed(const BlockHeader& header, uint32_t nonce) {
    std::vector<uint8_t> data = header.ToBytes();
    data.push_back(static_cast<uint8_t>(nonce & 0xFF));
    data.push_back(static_cast<uint8_t>((nonce >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((nonce >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((nonce >> 24) & 0xFF));
    return Keccak256(data);
}

void SeedToMatrix(const std::vector<uint8_t>& seed, double* outMatrixM) {
    // Use the 32-byte seed to deterministically populate a 64x64 double matrix.
    // Simple linear congruential generator per-element wrapped around the seed values.
    const int N = 64;
    const uint64_t A = 6364136223846793005ULL;
    const uint64_t C = 1442695040888963407ULL;
    uint64_t state = 0;
    for (uint8_t s : seed) {
        state = state * 31 + s;
    }
    for (int i = 0; i < N * N; ++i) {
        state = A * state + C;
        // produce a double in a reasonable range
        int64_t exponent = static_cast<int64_t>(state & 0x7FF);
        if ((exponent < 0x5A0) || (exponent > 0x7FE)) exponent = 0x3FF; // keep in small range around 1
        uint64_t mantissa = state >> 11;
        uint64_t bits = (exponent << 52) | (mantissa & 0xFFFFFFFFFFFFF);
        double v = U64ToDouble(bits);
        // clamp to avoid infinities
        if (std::isnan(v) || std::isinf(v)) v = 1.0;
        outMatrixM[i] = v;
    }
}

void MultiplyMatrixVector(const double* M, const double* V, double* Out) {
    const int N = 64;
    for (int i = 0; i < N; ++i) {
        double sum = 0.0;
        for (int j = 0; j < N; ++j) {
            sum += M[i * N + j] * V[j];
        }
        Out[i] = sum;
    }
}

std::vector<uint8_t> AuraHash(const BlockHeader& header, uint32_t nonce) {
    auto seed = GenerateSeed(header, nonce);
    double M[64 * 64];
    SeedToMatrix(seed, M);

    double V[64];
    // Map 32-byte seed also to a 64-element vector by expanding bytes and dithering.
    uint64_t state = 0;
    for (uint8_t s : seed) {
        state = state * 31 + s;
    }
    for (int i = 0; i < 64; ++i) {
        uint64_t A = 6364136223846793005ULL;
        uint64_t C = 1442695040888963407ULL;
        state = A * state + C;
        int64_t exponent = static_cast<int64_t>(state & 0x7FF);
        if ((exponent < 0x5A0) || (exponent > 0x7FE)) exponent = 0x3FF;
        uint64_t mantissa = state >> 11;
        uint64_t bits = (exponent << 52) | (mantissa & 0xFFFFFFFFFFFFF);
        double v = U64ToDouble(bits);
        if (std::isnan(v) || std::isinf(v)) v = 1.0;
        V[i] = v;
    }

    double Out[64];
    MultiplyMatrixVector(M, V, Out);

    // Convert Out vector to bytes for the final hash
    std::vector<uint8_t> outBytes(64 * 8);
    for (int i = 0; i < 64; ++i) {
        uint64_t bits = DoubleToU64(Out[i]);
        // Write little-endian
        for (int b = 0; b < 8; ++b) {
            outBytes[i * 8 + b] = static_cast<uint8_t>((bits >> (8 * b)) & 0xFF);
        }
    }

    return Keccak256(outBytes);
}

bool VerifyAuraHash(const BlockHeader& header, uint32_t nonce, const Hash256& target) {
    std::vector<uint8_t> hashVec = AuraHash(header, nonce);
    Hash256 h = ToHash256(hashVec);
    // Compare h and target as unsigned 256-bit integers (big-endian)
    // Return true if h <= target
    return memcmp(h.data(), target.data(), 32) <= 0;
}

uint32_t AuraHashMine(const BlockHeader& header, const Hash256& target) {
    uint32_t nonce = 0;
    while (true) {
        std::vector<uint8_t> hashVec = AuraHash(header, nonce);
        Hash256 h = ToHash256(hashVec);
        if (memcmp(h.data(), target.data(), 32) <= 0) {
            return nonce;
        }
        if (++nonce == 0) break; // overflow
    }
    return 0;
}

} // namespace auracash
