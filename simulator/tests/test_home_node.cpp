#include "test_util.h"
#include "home_node.h"
#include "packet.h"
#include "mac.h"
#include <vector>
#include <cstring>

static const uint8_t KEY[MAC_KEY_LEN] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};

struct MockHomeHAL : public HomeHAL {
    uint32_t now_ms = 0;
    std::vector<std::vector<uint8_t>> sent;

    uint32_t nowMs() override { return now_ms; }
    void sendPacket(const uint8_t* b, size_t n) override {
        sent.push_back(std::vector<uint8_t>(b, b + n));
    }
};

// Build a challenge packet (platform → home)
static void make_challenge(uint32_t nonce, uint8_t status,
                           uint8_t out[PACKET_LEN])
{
    packet_encode_challenge(status, nonce, out);
}

// Decode the response the home sent and return its n and R
static void decode_response(const std::vector<uint8_t>& pkt,
                            uint8_t& n, uint32_t& R)
{
    packet_decode_response(pkt.data(), n, R);
}

int main()
{
    SUITE("home_node: initial state is SAFE");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        CHECK_EQ((int)node.mode(), (int)HomeMode::SAFE);
    }

    SUITE("home_node: SAFE mode — challenge receives no response");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x11111, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        CHECK_EQ(hal.sent.size(), 0u);
    }

    SUITE("home_node: AUTO mode — responds to every challenge");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x11111, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        node.onPacket(chal, PACKET_LEN);   // same nonce for simplicity
        CHECK_EQ(hal.sent.size(), 2u);
    }

    SUITE("home_node: AUTO mode — response uses N_AUTO");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x22222, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        uint8_t n; uint32_t R;
        decode_response(hal.sent[0], n, R);
        CHECK_EQ(n, N_AUTO);
    }

    SUITE("home_node: AUTO mode — response MAC is correct");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x33333, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        uint8_t n; uint32_t R;
        decode_response(hal.sent[0], n, R);
        CHECK_EQ(R, mac_compute(KEY, 0x33333, N_AUTO));
    }

    SUITE("home_node: SINGLE mode — responds exactly once");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::SINGLE);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x44444, 0, chal);
        node.onPacket(chal, PACKET_LEN);        // first → responds
        node.onPacket(chal, PACKET_LEN);        // second → silent (key sprung back)
        node.onPacket(chal, PACKET_LEN);        // third → silent
        CHECK_EQ(hal.sent.size(), 1u);
    }

    SUITE("home_node: SINGLE mode — returns to SAFE after firing");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::SINGLE);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x55555, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        CHECK_EQ((int)node.mode(), (int)HomeMode::SAFE);
    }

    SUITE("home_node: SINGLE uses configured n");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setN(9);
        node.setMode(HomeMode::SINGLE);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x66666, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        uint8_t n; uint32_t R;
        decode_response(hal.sent[0], n, R);
        CHECK_EQ(n, 9u);
        CHECK_EQ(R, mac_compute(KEY, 0x66666, 9));
    }

    SUITE("home_node: setN clamped to [N_MIN, N_MAX]");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setN(200);
        CHECK_EQ(node.n(), N_MAX);
        node.setN(0);
        CHECK_EQ(node.n(), N_MIN);
        node.setN(9);
        CHECK_EQ(node.n(), 9u);
    }

    SUITE("home_node: mushroom forces SAFE and sprays n=0 (not normal grants)");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        node.mushroom();
        CHECK_EQ((int)node.mode(), (int)HomeMode::SAFE);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x77777, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        // Spray sends one n=0 revoke, not a normal grant
        CHECK_EQ(hal.sent.size(), 1u);
        uint8_t n; uint32_t R;
        decode_response(hal.sent[0], n, R);
        CHECK_EQ(n, 0u);    // revoke, not a grant
        CHECK_EQ(R, 0u);
    }

    SUITE("home_node: link-live flag set on receive, cleared after timeout");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x88888, 0, chal);
        node.onPacket(chal, PACKET_LEN);
        node.tick();
        CHECK(node.status().linkLive);

        hal.now_ms += 3000;    // past LINK_TIMEOUT_MS (2000)
        node.tick();
        CHECK(!node.status().linkLive);
    }

    SUITE("home_node: echoedN carries robot's last accepted n");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        uint8_t chal[PACKET_LEN];
        make_challenge(0x99999, 0x5, chal);     // status nibble = last n = 5
        node.onPacket(chal, PACKET_LEN);
        CHECK_EQ(node.status().echoedN, 0x5u);
    }

    SUITE("home_node: mushroom sprays 4 × n=0 revoke responses");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        node.mushroom();
        uint8_t chal[PACKET_LEN];
        make_challenge(0xAAAAA, 0, chal);
        // First 4 challenges → n=0 revoke (spray)
        for (int i = 0; i < 4; i++) {
            hal.sent.clear();
            node.onPacket(chal, PACKET_LEN);
            CHECK_EQ(hal.sent.size(), 1u);
            uint8_t n; uint32_t R;
            decode_response(hal.sent[0], n, R);
            CHECK_EQ(n, 0u);
            CHECK_EQ(R, 0u);
        }
        // 5th challenge → silent (spray exhausted, mode = SAFE)
        hal.sent.clear();
        node.onPacket(chal, PACKET_LEN);
        CHECK_EQ(hal.sent.size(), 0u);
    }

    SUITE("home_node: short packet ignored");
    {
        MockHomeHAL hal;
        HomeNode node(hal, KEY);
        node.setMode(HomeMode::AUTO);
        uint8_t short_pkt[2] = {0x01, 0x02};
        node.onPacket(short_pkt, 2);
        CHECK_EQ(hal.sent.size(), 0u);
    }

    RESULTS();
}
