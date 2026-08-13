#include "Secp256k1.h"
#include <secp256k1.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace auracash {

Secp256k1::~Secp256k1() {}

bool Secp256k1::generate_keypair(uint256 &out_priv, uint256 &out_pub) {
    secp256k1_context *ctx = get_context();
    secp256k1_pubkey pubkey;
    // Generate random private key
    if (RAND_bytes(out_priv.data, 32) != 1) return false;
    int ret = secp256k1_ec_pubkey_create(ctx, &pubkey, out_priv.data);
    if (!ret) return false;
    unsigned char pub_serialized[65];
    size_t pub_len = 65;
    ret = secp256k1_ec_pubkey_serialize(ctx, pub_serialized, &pub_len, &pubkey, SECP256K1_EC_UNCOMPRESSED);
    if (!ret) return false;
    out_pub = uint256(&pub_serialized[1], 64);
    return true;
}

bool Secp256k1::sign(const uint256 &msg, const uint256 &priv, Signature &out_sig) {
    secp256k1_context *ctx = get_context();
    secp256k1_ecdsa_signature sig;
    int ret = secp256k1_ecdsa_sign(ctx, &sig, msg.data, priv.data, nullptr, nullptr);
    if (!ret) return false;
    unsigned char sig_serialized[64];
    secp256k1_ecdsa_signature_serialize_compact(ctx, sig_serialized, &sig);
    out_sig = Signature(sig_serialized, 64);
    return true;
}

bool Secp256k1::verify(const uint256 &msg, const Signature &sig, const uint256 &pub) {
    secp256k1_context *ctx = get_context();
    secp256k1_ecdsa_signature parsed_sig;
    int ret = secp256k1_ecdsa_signature_parse_compact(ctx, &parsed_sig, sig.data);
    if (!ret) return false;
    secp256k1_pubkey pubkey;
    ret = secp256k1_ec_pubkey_parse(ctx, &pubkey, pub.data, 64);
    if (!ret) return false;
    ret = secp256k1_ecdsa_verify(ctx, &parsed_sig, msg.data, &pubkey);
    return ret != 0;
}

std::string Secp256k1::public_key_to_address(const uint256 &pub) {
    unsigned char pub_serialized[65];
    pub_serialized[0] = 0x04;
    std::memcpy(pub_serialized + 1, pub.data, 64);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, pub_serialized, 65);
    SHA256_Final(hash, &sha256);
    unsigned char checksum_hash[SHA256_DIGEST_LENGTH];
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, hash, SHA256_DIGEST_LENGTH);
    SHA256_Final(checksum_hash, &sha256);
    std::vector<unsigned char> payload;
    payload.push_back(0); // mainnet version
    payload.insert(payload.end(), hash, hash + SHA256_DIGEST_LENGTH);
    payload.insert(payload.end(), checksum_hash, checksum_hash + 4);
    return base58_encode(payload);
}

std::string Secp256k1::base58_encode(const std::vector<unsigned char> &data) {
    const char *base58_chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint64_t base = 58;
    std::string encoded;
    uint64_t value = 0;
    int leading_zeros = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        value = value * 256 + data[i];
        if (value == 0) {
            leading_zeros++;
        } else {
            while (value >= base) {
                encoded.push_back(base58_chars[value % base]);
                value /= base;
            }
        }
    }
    for (int i = 0; i < leading_zeros; ++i) {
        encoded.push_back(base58_chars[0]);
    }
    std::reverse(encoded.begin(), encoded.end());
    return encoded;
}

secp256k1_context *Secp256k1::get_context() {
    static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    static bool randomized = false;
    if (!randomized) {
        unsigned char seed[32];
        if (RAND_bytes(seed, 32) == 1) {
            secp256k1_context_randomize(ctx, seed);
            randomized = true;
        }
    }
    return ctx;
}

} // namespace auracash

namespace auracash {


uint256::uint256(const uint8_t* p, size_t size) {
    memset(data, 0, 32);
    size_t n = std::min<size_t>(size, 32);
    memcpy(data, p, n);
}

uint256::uint256(const std::string &hex) {
    memset(data, 0, 32);
    size_t len = hex.size();
    for (size_t i = 0; i + 1 < len && i / 2 < 32; i += 2) {
        unsigned char hi = hex[i];
        unsigned char lo = hex[i + 1];
        unsigned char hv = 0, lv = 0;
        if (hi >= '0' && hi <= '9') hv = hi - '0';
        else if (hi >= 'a' && hi <= 'f') hv = hi - 'a' + 10;
        else if (hi >= 'A' && hi <= 'F') hv = hi - 'A' + 10;
        if (lo >= '0' && lo <= '9') lv = lo - '0';
        else if (lo >= 'a' && lo <= 'f') lv = lo - 'a' + 10;
        else if (lo >= 'A' && lo <= 'F') lv = lo - 'A' + 10;
        data[i / 2] = (hv << 4) | lv;
    }
}

std::string uint256::to_hex() const {
    std::string hex;
    for (size_t i = 0; i < 32; ++i) {
        hex += "0123456789abcdef"[(data[i] >> 4) & 0xF];
        hex += "0123456789abcdef"[data[i] & 0xF];
    }
    return hex;
}

std::string Signature::to_hex() const {
    std::string hex;
    for (size_t i = 0; i < 64; ++i) {
        hex += "0123456789abcdef"[(data[i] >> 4) & 0xF];
        hex += "0123456789abcdef"[data[i] & 0xF];
    }
    return hex;
}

} // namespace auracash
