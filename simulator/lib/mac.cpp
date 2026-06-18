#include "mac.h"
#include <string.h>

// ── SipHash-2-4 ──────────────────────────────────────────────────────────────
// Reference: Aumasson & Bernstein, "SipHash: a fast short-input PRF" (2012)
// Key = 128-bit (16 bytes).  Output = 64-bit.

#define ROTL64(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64u - (b))))

#define SIPROUND \
    v0 += v1; v1 = ROTL64(v1,13); v1 ^= v0; v0 = ROTL64(v0,32); \
    v2 += v3; v3 = ROTL64(v3,16); v3 ^= v2;                      \
    v0 += v3; v3 = ROTL64(v3,21); v3 ^= v0;                      \
    v2 += v1; v1 = ROTL64(v1,17); v1 ^= v2; v2 = ROTL64(v2,32)

static uint64_t siphash24(const uint8_t k[16],
                          const uint8_t* msg, size_t len)
{
    // Load key
    uint64_t k0 = (uint64_t)k[0]       | ((uint64_t)k[1] <<  8)
                | ((uint64_t)k[2] << 16)| ((uint64_t)k[3] << 24)
                | ((uint64_t)k[4] << 32)| ((uint64_t)k[5] << 40)
                | ((uint64_t)k[6] << 48)| ((uint64_t)k[7] << 56);
    uint64_t k1 = (uint64_t)k[8]       | ((uint64_t)k[9] <<  8)
                | ((uint64_t)k[10]<< 16)| ((uint64_t)k[11]<< 24)
                | ((uint64_t)k[12]<< 32)| ((uint64_t)k[13]<< 40)
                | ((uint64_t)k[14]<< 48)| ((uint64_t)k[15]<< 56);

    uint64_t v0 = k0 ^ UINT64_C(0x736f6d6570736575);
    uint64_t v1 = k1 ^ UINT64_C(0x646f72616e646f6d);
    uint64_t v2 = k0 ^ UINT64_C(0x6c7967656e657261);
    uint64_t v3 = k1 ^ UINT64_C(0x7465646279746573);

    // Process 8-byte blocks
    size_t blocks = len / 8;
    for (size_t i = 0; i < blocks; i++) {
        uint64_t m = 0;
        const uint8_t* p = msg + i * 8;
        for (int j = 0; j < 8; j++) m |= ((uint64_t)p[j] << (j * 8));
        v3 ^= m;
        SIPROUND; SIPROUND;
        v0 ^= m;
    }

    // Last (possibly partial) block + length byte
    uint64_t last = (uint64_t)(len & 0xFF) << 56;
    const uint8_t* tail = msg + blocks * 8;
    size_t rem = len & 7;
    for (size_t i = 0; i < rem; i++) last |= ((uint64_t)tail[i] << (i * 8));

    v3 ^= last;
    SIPROUND; SIPROUND;
    v0 ^= last;

    // Finalization
    v2 ^= 0xFF;
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

// ── MAC entry point ──────────────────────────────────────────────────────────
// Input encoding: 3 bytes — nonce (20-bit, big-endian left-packed) | n (4-bit)
//   byte 0: nonce[19:12]
//   byte 1: nonce[11:4]
//   byte 2: nonce[3:0] << 4 | n[3:0]
// This uniquely binds the lease exponent n into the authenticated value.

uint32_t mac_compute(const uint8_t key[MAC_KEY_LEN], uint32_t nonce, uint8_t n)
{
    uint8_t msg[3];
    msg[0] = (nonce >> 12) & 0xFF;
    msg[1] = (nonce >>  4) & 0xFF;
    msg[2] = ((nonce & 0x0F) << 4) | (n & 0x0F);

    uint64_t h = siphash24(key, msg, sizeof(msg));
    return (uint32_t)(h & 0x000FFFFF);  // lower 20 bits
}
