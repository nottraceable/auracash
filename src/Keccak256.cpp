#include "keccak256.h"
#include <cstring>

// Keccak-256 implementation (SHA-3 family)

static const uint32_t keccakf_rndc[24][2] = {
    {0x00000001,0}, {0x00008082,0}, {0x0000808a,0}, {0x80008000,1},
    {0x0000808b,0}, {0x80000001,0}, {0x8000808b,1}, {0x80008088,1},
    {0x00008089,0}, {0x80008003,0}, {0x80008002,0}, {0x00008082,0},
    {0x0000000b,0}, {0x0000808b,0}, {0x8000000b,0}, {0x8000001a,1},
    {0x80008082,0}, {0x00008010,0}, {0x80000028,1}, {0x80000008,0},
    {0x0000808a,0}, {0x00000003,0}, {0x8000808a,0}, {0x80000088,1}
};

static const int keccakf_rotc[24] = {
    1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
    27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
};

static const int keccakf_piln[24] = {
    10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
};

typedef uint64_t st_t[25];

static void keccakf(st_t st) {
    int i, j, r;
    uint64_t t;
    uint64_t bc[5];

    for (r = 0; r < 24; ++r) {
        // Theta
        for (i = 0; i < 5; ++i) {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (i = 0; i < 5; ++i) {
            t = bc[(i + 4) % 5] ^ ((bc[(i + 1) % 5] << 1) | (bc[(i + 1) % 5] >> 63));
            for (j = 0; j < 25; j += 5) {
                st[j + i] ^= t;
            }
        }

        // Rho Pi
        t = st[1];
        for (i = 0; i < 24; ++i) {
            j = keccakf_piln[i];
            bc[0] = st[j];
            st[j] = (t << keccakf_rotc[i]) | (t >> (64 - keccakf_rotc[i]));
            t = bc[0];
        }

        // Chi
        for (j = 0; j < 25; j += 5) {
            for (i = 0; i < 5; ++i) {
                bc[i] = st[j + i];
            }
            for (i = 0; i < 5; ++i) {
                st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
            }
        }

        // Iota
        st[0] ^= (uint64_t(keccakf_rndc[r][0]) | (uint64_t(keccakf_rndc[r][1]) << 32));
    }
}

static int lsbhash(const unsigned char *in, int inlen, unsigned char *md, int mdlen)
{
    st_t st;
    unsigned char temp[200];
    unsigned char *padded = NULL;
    int padded_len;
    int rate = 200 - 2 * mdlen;
    int capacity = 2 * mdlen;
    int absorbRate = rate;
    int i, j;

    memset(st, 0, sizeof(st));

    // Absorb
    while (inlen >= absorbRate) {
        for (i = 0; i < absorbRate / 8; ++i) {
            for (j = 0; j < 8; ++j) {
                st[i] ^= ((uint64_t)in[i * 8 + j]) << (8 * j);
            }
        }
        keccakf(st);
        in += absorbRate;
        inlen -= absorbRate;
    }

    // Final block with padding
    padded_len = absorbRate;
    padded = new unsigned char[padded_len];
    memcpy(padded, in, inlen);
    padded[inlen] = 0x01;
    memset(padded + inlen + 1, 0, padded_len - inlen - 1);
    padded[padded_len - 1] ^= 0x80;

    for (i = 0; i < absorbRate / 8; ++i) {
        for (j = 0; j < 8; ++j) {
            st[i] ^= ((uint64_t)padded[i * 8 + j]) << (8 * j);
        }
    }
    keccakf(st);

    // Squeeze
    int outlen = mdlen;
    unsigned char *out = md;
    while (outlen > 0) {
        int blockSize = outlen < absorbRate ? outlen : absorbRate;
        for (i = 0; i < (blockSize + 7) / 8; ++i) {
            for (j = 0; j < 8 && i * 8 + j < blockSize; ++j) {
                out[i * 8 + j] = (st[i] >> (8 * j)) & 0xff;
            }
        }
        out += blockSize;
        outlen -= blockSize;
    }

    delete[] padded;
    return 0;
}

std::vector<uint8_t> Keccak256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> md(32);
    lsbhash(data.data(), data.size(), md.data(), 32);
    return md;
}

std::vector<uint8_t> Keccak256(const uint8_t* data, size_t len) {
    std::vector<uint8_t> md(32);
    lsbhash(data, len, md.data(), 32);
    return md;
}
