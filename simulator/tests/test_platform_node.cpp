#include "test_util.h"
#include "platform_node.h"
#include "packet.h"
#include "mac.h"
#include <vector>
#include <cstring>

static const uint8_t KEY[MAC_KEY_LEN] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};

// ── Mock HAL ─────────────────────────────────────────────────────────────────

struct MockPlatformHAL : public PlatformHAL {
    uint32_t now_ms       = 0;
    bool     relay_closed = false;
    bool     bit_result   = true;
    bool     wd_kicked    = false;
    uint32_t wd_window_ms = 0;
    uint32_t wd_kick_count = 0;
    bool     relay_fault  = false;   // when true, readback disagrees with set

    std::vector<std::vector<uint8_t>> sent;

    uint32_t nowMs()                           override { return now_ms; }
    uint32_t randomU32()                       override { return (_rng++ * 0x1337u + 1u) & 0xFFFFFu; }
    void     relaySet(bool c)                  override { relay_closed = c; }
    bool     relayReadback()                   override { return relay_fault ? !relay_closed : relay_closed; }
    void     watchdogArm(uint32_t w)           override { wd_window_ms = w; }
    void     watchdogKick()                    override { wd_kicked = true; wd_kick_count++; }
    bool     performBIT()                      override { return bit_result; }
    void     sendPacket(const uint8_t* b, size_t n) override {
        sent.push_back(std::vector<uint8_t>(b, b + n));
    }

private:
    uint32_t _rng = 42;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static uint32_t nonce_from_pkt(const std::vector<uint8_t>& pkt) {
    uint8_t s; uint32_t nonce;
    packet_decode_challenge(pkt.data(), s, nonce);
    return nonce;
}

// Build a valid response packet for the nonce the platform last sent
static void valid_response(uint32_t nonce, uint8_t n,
                           uint8_t out[PACKET_LEN])
{
    packet_encode_response(n, mac_compute(KEY, nonce, n), out);
}

// Arm the node: tick to emit challenge, feed valid response, tick through BIT.
// Needs three tick() calls:
//   tick 1: SAFE → emits challenge
//   onPacket: sets _pendingArm
//   tick 2: SAFE → ARMING (one cycle so display can update before blocking BIT)
//   tick 3: ARMING → ARMED (runs performBIT)
static void arm_node(PlatformNode& node, MockPlatformHAL& hal, uint8_t n = 9) {
    hal.sent.clear();
    node.tick();                                       // emits challenge
    uint32_t nonce = nonce_from_pkt(hal.sent.back());
    uint8_t resp[PACKET_LEN];
    valid_response(nonce, n, resp);
    node.onPacket(resp, PACKET_LEN);                  // valid grant → _pendingArm
    node.tick();                                       // SAFE → ARMING
    node.tick();                                       // ARMING → ARMED (runs BIT)
}

// ── Tests ─────────────────────────────────────────────────────────────────────

int main()
{
    SUITE("platform_node: initial state");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
        CHECK(!node.relayClosed());
    }

    SUITE("platform_node: sends challenges in SAFE state");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        node.tick();
        CHECK(hal.sent.size() >= 1u);
        CHECK_EQ(hal.sent[0].size(), PACKET_LEN);
    }

    SUITE("platform_node: challenge nonces are unique (no duplicate in buffer)");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        // Advance time to trigger multiple challenges
        for (int i = 0; i < (int)NONCE_BUF_K + 2; i++) {
            hal.now_ms += T_CHALLENGE_MS + 1;
            node.tick();
        }
        // All sent nonces must be distinct
        bool unique = true;
        for (size_t i = 0; i < hal.sent.size(); i++) {
            for (size_t j = i + 1; j < hal.sent.size(); j++) {
                uint8_t si; uint32_t ni;
                uint8_t sj; uint32_t nj;
                packet_decode_challenge(hal.sent[i].data(), si, ni);
                packet_decode_challenge(hal.sent[j].data(), sj, nj);
                if (ni == nj) unique = false;
            }
        }
        CHECK(unique);
    }

    SUITE("platform_node: valid response → ARMING → ARMED");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        arm_node(node, hal, 9);
        CHECK_EQ((int)node.state(), (int)PlatformState::ARMED);
        CHECK(node.relayClosed());
        CHECK(hal.relay_closed);
        CHECK(hal.wd_kicked);
        CHECK_EQ(hal.wd_window_ms, (1u << 9) * 1000u);   // 512 s window
    }

    SUITE("platform_node: invalid R → stays SAFE");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        node.tick();
        uint32_t nonce = nonce_from_pkt(hal.sent.back());
        uint8_t resp[PACKET_LEN];
        // Corrupt R by flipping one bit
        packet_encode_response(9, mac_compute(KEY, nonce, 9) ^ 1, resp);
        node.onPacket(resp, PACKET_LEN);
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
        CHECK(!node.relayClosed());
    }

    SUITE("platform_node: wrong key rejected");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        node.tick();
        uint32_t nonce = nonce_from_pkt(hal.sent.back());
        uint8_t bad_key[MAC_KEY_LEN] = {};
        uint8_t resp[PACKET_LEN];
        packet_encode_response(9, mac_compute(bad_key, nonce, 9), resp);
        node.onPacket(resp, PACKET_LEN);
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
    }

    SUITE("platform_node: n > nMax rejected");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY, /*nMax=*/5);
        node.tick();
        uint32_t nonce = nonce_from_pkt(hal.sent.back());
        uint8_t resp[PACKET_LEN];
        valid_response(nonce, 9, resp);   // n=9 > nMax=5
        node.onPacket(resp, PACKET_LEN);
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
    }

    SUITE("platform_node: short packet ignored");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        uint8_t short_pkt[2] = {0x01, 0x02};
        node.onPacket(short_pkt, 2);
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
    }

    SUITE("platform_node: BIT failure → TRIPPED");
    {
        MockPlatformHAL hal;
        hal.bit_result = false;
        PlatformNode node(hal, KEY);
        arm_node(node, hal);                           // BIT will fail
        CHECK_EQ((int)node.state(), (int)PlatformState::TRIPPED);
        CHECK(!node.relayClosed());
        CHECK(!hal.relay_closed);
    }

    SUITE("platform_node: software lease expiry → SAFE");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        arm_node(node, hal, 1);                        // 2-second lease
        CHECK_EQ((int)node.state(), (int)PlatformState::ARMED);
        hal.now_ms += (1u << 1) * 1000u + 100u;       // past lease
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
        CHECK(!node.relayClosed());
    }

    SUITE("platform_node: relay readback fault → TRIPPED");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        arm_node(node, hal);
        CHECK_EQ((int)node.state(), (int)PlatformState::ARMED);
        hal.relay_fault = true;      // readback now disagrees
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::TRIPPED);
    }

    SUITE("platform_node: requestDisarm from ARMED → SAFE");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        arm_node(node, hal);
        node.requestDisarm();
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
        CHECK(!node.relayClosed());
        CHECK(!hal.relay_closed);
    }

    SUITE("platform_node: requestDisarm clears TRIPPED latch");
    {
        MockPlatformHAL hal;
        hal.bit_result = false;
        PlatformNode node(hal, KEY);
        arm_node(node, hal);
        CHECK_EQ((int)node.state(), (int)PlatformState::TRIPPED);
        hal.bit_result = true;
        node.requestDisarm();
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::SAFE);
    }

    SUITE("platform_node: lease refresh in ARMED extends window");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        arm_node(node, hal, 2);                         // 4 s lease
        CHECK_EQ((int)node.state(), (int)PlatformState::ARMED);
        hal.now_ms += 3500;                             // 3.5 s in (would expire at 4 s)
        // Deliver a fresh heartbeat
        hal.sent.clear();
        node.tick();                                    // sends new challenge
        uint32_t nonce2 = nonce_from_pkt(hal.sent.back());
        uint8_t resp[PACKET_LEN];
        valid_response(nonce2, 2, resp);
        node.onPacket(resp, PACKET_LEN);
        node.tick();                                    // refresh
        // Advance to original expiry + 1 s
        hal.now_ms += 1500;
        node.tick();
        CHECK_EQ((int)node.state(), (int)PlatformState::ARMED);  // still alive
    }

    SUITE("platform_node: leaseRemainingMs");
    {
        MockPlatformHAL hal;
        PlatformNode node(hal, KEY);
        CHECK_EQ(node.leaseRemainingMs(0), 0u);         // not armed
        arm_node(node, hal, 1);                         // 2 s lease
        hal.now_ms += 500;
        uint32_t rem = node.leaseRemainingMs(hal.now_ms);
        CHECK(rem > 1400u && rem <= 2000u);
    }

    RESULTS();
}
