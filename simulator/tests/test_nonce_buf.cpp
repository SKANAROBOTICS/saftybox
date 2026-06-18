#include "test_util.h"
#include "nonce_buf.h"
#include "mac.h"

static const uint8_t KEY[MAC_KEY_LEN] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};

// Helper: build a correct R for a nonce
static uint32_t correct_R(uint32_t nonce, uint8_t n) {
    return mac_compute(KEY, nonce, n);
}

int main()
{
    SUITE("nonce_buf: add and count");
    {
        NonceBuf b;
        CHECK_EQ(b.count(), 0u);
        CHECK(b.add(0x11111, 0, 5000));
        CHECK_EQ(b.count(), 1u);
        CHECK(b.add(0x22222, 0, 5000));
        CHECK_EQ(b.count(), 2u);
    }

    SUITE("nonce_buf: duplicate rejected");
    {
        NonceBuf b;
        CHECK( b.add(0xAAAAA, 0, 5000));
        CHECK(!b.add(0xAAAAA, 0, 5000));  // same nonce
        CHECK_EQ(b.count(), 1u);
    }

    SUITE("nonce_buf: consume valid — returns true, entry removed");
    {
        NonceBuf b;
        b.add(0x7CAFE, 0, 5000);
        CHECK(b.consume(KEY, 9, correct_R(0x7CAFE, 9), 100));
        CHECK_EQ(b.count(), 0u);
    }

    SUITE("nonce_buf: consume wrong R — returns false, entry kept");
    {
        NonceBuf b;
        b.add(0x7CAFE, 0, 5000);
        CHECK(!b.consume(KEY, 9, correct_R(0x7CAFE, 9) ^ 1, 100));
        CHECK_EQ(b.count(), 1u);
    }

    SUITE("nonce_buf: no replay — consumed nonce cannot be re-consumed");
    {
        NonceBuf b;
        b.add(0x7CAFE, 0, 5000);
        uint32_t R = correct_R(0x7CAFE, 9);
        CHECK( b.consume(KEY, 9, R, 100));
        CHECK(!b.consume(KEY, 9, R, 100));
    }

    SUITE("nonce_buf: wrong key rejected");
    {
        NonceBuf b;
        b.add(0x7CAFE, 0, 5000);
        uint8_t bad_key[MAC_KEY_LEN] = {};
        uint32_t R_good = correct_R(0x7CAFE, 9);
        CHECK(!b.consume(bad_key, 9, R_good, 100));
        CHECK_EQ(b.count(), 1u);
    }

    SUITE("nonce_buf: n mismatch rejected (n is bound in MAC)");
    {
        NonceBuf b;
        b.add(0x7CAFE, 0, 5000);
        uint32_t R_n9 = correct_R(0x7CAFE, 9);
        // Present as n=5 (different lease) with R computed for n=9
        CHECK(!b.consume(KEY, 5, R_n9, 100));
    }

    SUITE("nonce_buf: expiry — expired nonce not consumed");
    {
        NonceBuf b;
        b.add(0xAAAAA, 0, 100);   // expires at t=100
        CHECK(!b.consume(KEY, 9, correct_R(0xAAAAA, 9), 200));  // t=200, expired
    }

    SUITE("nonce_buf: prune removes expired entries");
    {
        NonceBuf b;
        b.add(0xBBBBB, 0, 100);
        b.add(0xCCCCC, 0, 500);
        CHECK_EQ(b.count(), 2u);
        b.prune(150);           // t=150: 0xBBBBB expired, 0xCCCCC still valid
        CHECK_EQ(b.count(), 1u);
        b.prune(600);           // t=600: both expired
        CHECK_EQ(b.count(), 0u);
    }

    SUITE("nonce_buf: fresh nonce after expiry no longer blocks re-add");
    {
        NonceBuf b;
        b.add(0xDDDDD, 0, 100);
        b.prune(200);                   // expires
        CHECK(b.add(0xDDDDD, 200, 5000)); // same value, new slot
    }

    SUITE("nonce_buf: full buffer displaces oldest entry");
    {
        NonceBuf b;
        // Fill to capacity
        for (uint8_t i = 0; i < NONCE_BUF_K; i++)
            b.add((uint32_t)i + 1, 0, 5000);
        CHECK_EQ(b.count(), NONCE_BUF_K);
        // Adding one more should not fail (displaces oldest)
        b.add(NONCE_BUF_K + 1, 0, 5000);
        CHECK_EQ(b.count(), NONCE_BUF_K);  // still K entries
        // Oldest (nonce=1) should be gone
        CHECK(!b.contains(1));
        CHECK( b.contains(NONCE_BUF_K + 1));
    }

    SUITE("nonce_buf: newest-first search — most recent match preferred");
    {
        // Two entries with different nonces; verify newest is found first
        NonceBuf b;
        b.add(0x00001, 0, 5000);   // older
        b.add(0x00002, 0, 5000);   // newer
        // Consume newer one
        CHECK(b.consume(KEY, 3, correct_R(0x00002, 3), 100));
        CHECK(!b.contains(0x00002));
        CHECK( b.contains(0x00001));
    }

    RESULTS();
}
