#include "test_util.h"
#include "mac.h"

static const uint8_t KEY_A[MAC_KEY_LEN] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};
static const uint8_t KEY_B[MAC_KEY_LEN] = {
    0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
    0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00
};

int main()
{
    SUITE("mac: output range");
    // All outputs must fit in 20 bits
    for (uint32_t nonce = 0; nonce < 32; nonce++) {
        for (uint8_t n = 0; n <= 16; n++) {
            uint32_t R = mac_compute(KEY_A, nonce, n);
            CHECK((R & 0xFFF00000u) == 0u);
        }
    }

    SUITE("mac: determinism");
    uint32_t r1 = mac_compute(KEY_A, 0x7CAFE, 9);
    uint32_t r2 = mac_compute(KEY_A, 0x7CAFE, 9);
    CHECK_EQ(r1, r2);

    SUITE("mac: different nonce → different R");
    {
        uint32_t ra = mac_compute(KEY_A, 0x00001, 9);
        uint32_t rb = mac_compute(KEY_A, 0x00002, 9);
        CHECK(ra != rb);
        uint32_t rc = mac_compute(KEY_A, 0xFFFFF, 9);
        CHECK(ra != rc);
    }

    SUITE("mac: n is bound into the MAC (same nonce, different n → different R)");
    {
        uint32_t r_n0 = mac_compute(KEY_A, 0xABCDE, 0);
        uint32_t r_n9 = mac_compute(KEY_A, 0xABCDE, 9);
        uint32_t r_nF = mac_compute(KEY_A, 0xABCDE, 0xF);
        // A captured short-lease response cannot be presented as a long-lease one
        CHECK(r_n0 != r_n9);
        CHECK(r_n9 != r_nF);
        CHECK(r_n0 != r_nF);
    }

    SUITE("mac: different key → different R");
    {
        uint32_t ra = mac_compute(KEY_A, 0x12345, 5);
        uint32_t rb = mac_compute(KEY_B, 0x12345, 5);
        CHECK(ra != rb);
    }

    SUITE("mac: zero key and zero input don't crash");
    {
        uint8_t zero_key[MAC_KEY_LEN] = {};
        uint32_t r = mac_compute(zero_key, 0, 0);
        CHECK((r & 0xFFF00000u) == 0u);  // still a valid 20-bit output
    }

    SUITE("mac: adjacent n values produce different outputs (20-bit space, spot check)");
    {
        // A 20-bit PRF output collides in ~17 values with p ≈ 0.013% — don't assert
        // global uniqueness across all 17 values. Instead verify that no two
        // *adjacent* n values collide (the most security-relevant case: a
        // captured n=k response must not accidentally match n=k+1).
        for (uint8_t n = 0; n < 16; n++) {
            uint32_t r0 = mac_compute(KEY_A, 0x7CAFE, n);
            uint32_t r1 = mac_compute(KEY_A, 0x7CAFE, n + 1);
            CHECK(r0 != r1);
        }
    }

    RESULTS();
}
