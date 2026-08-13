#ifndef AURACASH_CRYPTO_SECP256K1_H
#define AURACASH_CRYPTO_SECP256K1_H
#include <secp256k1.h>

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

namespace auracash {

class uint256 {
public:
    uint8_t data[32];
    uint256() { memset(data, 0, 32); }
    explicit uint256(const std::string &hex);
    uint256(const uint8_t* p, size_t size);
    std::string to_hex() const;
    bool operator==(const uint256 &other) const { return memcmp(data, other.data, 32) == 0; }
    bool operator!=(const uint256 &other) const { return !(*this == other); }
    uint256 &operator^=(const uint256 &other) {
        for (int i = 0; i < 32; ++i) data[i] ^= other.data[i];
        return *this;
    }
    uint256 operator^(const uint256 &other) const {
        uint256 result; result ^= other; return result;
    }
};

class Signature {
public:
    uint8_t data[64];
    Signature() { memset(data, 0, 64); }
    explicit Signature(const uint8_t *begin, size_t size) { memset(data, 0, 64); memcpy(data, begin, std::min<size_t>(size, 64)); }
    std::string to_hex() const;
};

class Secp256k1 {
public:
    ~Secp256k1();
    bool generate_keypair(uint256 &out_priv, uint256 &out_pub);
    bool sign(const uint256 &msg, const uint256 &priv, Signature &out_sig);
    bool verify(const uint256 &msg, const Signature &sig, const uint256 &pub);
    std::string public_key_to_address(const uint256 &pub);
    static std::string base58_encode(const std::vector<uint8_t> &data);
private:
    static secp256k1_context *get_context();
};

} // namespace auracash

#endif // AURACASH_CRYPTO_SECP256K1_H