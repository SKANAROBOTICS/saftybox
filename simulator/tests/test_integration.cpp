// End-to-end integration tests: real platform_node + real home_node,
// connected by calling onPacket() directly with the packets each side sends.
// No network, no threads — deterministic and fast.

#include "test_util.h"
#include "platform_node.h"
#include "home_node.h"
#include "packet.h"
#include "mac.h"
#include <vector>
#include <cstring>

static const uint8_t KEY[MAC_KEY_LEN] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};
static const uint8_t WRONG_KEY[MAC_KEY_LEN] = {
    0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF,
    0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF
};

// ── Mock HALs ─────────────────────────────────────────────────────────────────

struct IntegPlatHAL : public PlatformHAL {
    uint32_t now_ms    = 0;
    bool relay_closed  = false;
    bool bit_result    = true;
    std::vector<std::vector<uint8_t>> sent;

    uint32_t nowMs()                              override { return now_ms; }
    uint32_t randomU32()                          override { return (_rng++ * 0x9E3779B9u) & 0xFFFFFu; }
    void     relaySet(bool c)                     override { relay_closed = c; }
    bool     relayReadback()                      override { return relay_closed; }
    void     watchdogArm(uint32_t)                override {}
    void     watchdogKick()                       override {}
    bool     performBIT()                         override { return bit_result; }
    void     sendPacket(const uint8_t* b, size_t n) override {
        sent.push_back(std::vector<uint8_t>(b, b + n));
    }
private:
    uint32_t _rng = 1;
};

struct IntegHomeHAL : public HomeHAL {
    uint32_t now_ms  = 0;
    bool     link_up = true;
    std::vector<std::vector<uint8_t>> sent;

    uint32_t nowMs()    override { return now_ms; }
    bool     linkLive() override { return link_up; }
    void sendPacket(const uint8_t* b, size_t n) override {
        sent.push_back(std::vector<uint8_t>(b, b + n));
    }
};

// ── Exchange helpers ──────────────────────────────────────────────────────────

// Deliver all pending packets from platform → home and home → platform.
// Returns number of successful challenge-response exchanges.
static int exchange(PlatformNode& plat, IntegPlatHAL& platHAL,
                    HomeNode& home, IntegHomeHAL& homeHAL)
{
    int exchanges = 0;
    // Platform → Home
    for (auto& pkt : platHAL.sent)
        home.onPacket(pkt.data(), pkt.size());
    platHAL.sent.clear();

    // Home → Platform
    for (auto& pkt : homeHAL.sent) {
        plat.onPacket(pkt.data(), pkt.size());
        exchanges++;
    }
    homeHAL.sent.clear();
    return exchanges;
}

// One full tick cycle: tick both nodes, then exchange packets
static int tick_and_exchange(PlatformNode& plat, IntegPlatHAL& platHAL,
                              HomeNode& home,  IntegHomeHAL& homeHAL)
{
    platHAL.now_ms += 10;
    homeHAL.now_ms  = platHAL.now_ms;

    plat.tick();
    home.tick();
    return exchange(plat, platHAL, home, homeHAL);
}

// Drive the exchange until the platform reaches the target state or timeout
static bool drive_to(PlatformNode& plat, IntegPlatHAL& platHAL,
                     HomeNode& home, IntegHomeHAL& homeHAL,
                     PlatformState target, int max_ticks = 200)
{
    for (int i = 0; i < max_ticks; i++) {
        platHAL.now_ms += T_CHALLENGE_MS + 1;
        homeHAL.now_ms  = platHAL.now_ms;
        plat.tick();
        home.tick();
        exchange(plat, platHAL, home, homeHAL);
        // Run the ARMING tick if needed
        plat.tick();
        if (plat.state() == target) return true;
    }
    return false;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

int main()
{
    SUITE("integration: AUTO mode — platform reaches ARMED");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        bool reached = drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);
        CHECK(reached);
        CHECK(platHAL.relay_closed);
        CHECK_EQ((int)plat.state(), (int)PlatformState::ARMED);
    }

    SUITE("integration: SINGLE mode — platform reaches ARMED once");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setN(2);               // 4-second lease
        home.setMode(HomeMode::SINGLE);
        bool reached = drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);
        CHECK(reached);
        CHECK_EQ((int)home.mode(), (int)HomeMode::SAFE);  // key sprung back
    }

    SUITE("integration: wrong key — platform never arms");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, WRONG_KEY);  // different key

        home.setMode(HomeMode::AUTO);
        bool reached = drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED, 50);
        CHECK(!reached);
        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);
        CHECK(!platHAL.relay_closed);
    }

    SUITE("integration: AUTO sustains lease across multiple heartbeats");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);

        // Keep driving for N_AUTO lease periods — relay must stay closed
        uint32_t one_lease_ms = (1u << N_AUTO) * 1000u;
        bool relay_stayed_closed = true;
        for (int i = 0; i < 50; i++) {
            platHAL.now_ms += one_lease_ms / 10;
            homeHAL.now_ms  = platHAL.now_ms;
            plat.tick();
            home.tick();
            exchange(plat, platHAL, home, homeHAL);
            plat.tick();   // handle ARMING if needed
            if (!platHAL.relay_closed) relay_stayed_closed = false;
        }
        CHECK(relay_stayed_closed);
        CHECK_EQ((int)plat.state(), (int)PlatformState::ARMED);
    }

    SUITE("integration: lease expires after home goes SAFE (no more heartbeats)");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setN(1);   // 2-second lease
        home.setMode(HomeMode::SINGLE);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);

        // Home is now SAFE; advance time past lease
        uint32_t lease_ms = (1u << 1) * 1000u;
        platHAL.now_ms += lease_ms + 500;
        homeHAL.now_ms  = platHAL.now_ms;
        plat.tick();

        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);
        CHECK(!platHAL.relay_closed);
    }

    SUITE("integration: mushroom aborts mid-AUTO");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);

        // Operator presses mushroom
        home.mushroom();
        CHECK_EQ((int)home.mode(), (int)HomeMode::SAFE);

        // Platform lease expires — no more heartbeats from home
        platHAL.now_ms += (1u << N_AUTO) * 1000u + 500;
        homeHAL.now_ms  = platHAL.now_ms;
        plat.tick();
        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);
    }

    SUITE("integration: requestDisarm immediately opens relay");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);

        plat.requestDisarm();
        plat.tick();
        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);
        CHECK(!platHAL.relay_closed);
    }

    SUITE("integration: BIT failure prevents arming");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        platHAL.bit_result = false;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED, 50);
        CHECK_EQ((int)plat.state(), (int)PlatformState::TRIPPED);
        CHECK(!platHAL.relay_closed);
    }

    SUITE("integration: re-arm after trip (disarm + re-arm cycle)");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        platHAL.bit_result = false;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED, 50);
        CHECK_EQ((int)plat.state(), (int)PlatformState::TRIPPED);

        // Operator disarms (key cycle on home, requestDisarm on platform)
        home.mushroom();
        plat.requestDisarm();
        plat.tick();
        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);

        // Re-arm with BIT now passing
        platHAL.bit_result = true;
        home.setMode(HomeMode::AUTO);
        bool reached = drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);
        CHECK(reached);
        CHECK(platHAL.relay_closed);
    }

    SUITE("integration: mushroom spray reaches platform, relay opens immediately");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);
        CHECK_EQ((int)plat.state(), (int)PlatformState::ARMED);

        // Operator presses mushroom — home sprays n=0
        home.mushroom();

        // Exchange a few packets so the spray reaches platform
        for (int i = 0; i < 6; i++) {
            platHAL.now_ms += T_CHALLENGE_MS + 1;
            homeHAL.now_ms  = platHAL.now_ms;
            plat.tick();
            home.tick();
            exchange(plat, platHAL, home, homeHAL);
            plat.tick();   // consume pending disarm from n=0
            if (plat.state() == PlatformState::SAFE) break;
        }
        CHECK_EQ((int)plat.state(), (int)PlatformState::SAFE);
        CHECK(!platHAL.relay_closed);
    }

    SUITE("integration: statusNibble echoed in challenge is echoedN on home");
    {
        IntegPlatHAL platHAL;
        IntegHomeHAL homeHAL;
        PlatformNode plat(platHAL, KEY);
        HomeNode     home(homeHAL, KEY);

        home.setMode(HomeMode::AUTO);
        drive_to(plat, platHAL, home, homeHAL, PlatformState::ARMED);

        // After arming statusNibble = N_AUTO, but the first post-arm challenge
        // hasn't been exchanged yet.  Run a few more cycles.
        for (int i = 0; i < 4; i++) {
            platHAL.now_ms += T_CHALLENGE_MS + 1;
            homeHAL.now_ms  = platHAL.now_ms;
            plat.tick();
            home.tick();
            exchange(plat, platHAL, home, homeHAL);
            plat.tick();
        }

        CHECK_EQ(plat.statusNibble(), N_AUTO);
        CHECK_EQ(home.status().echoedN, N_AUTO);
        CHECK_EQ(home.status().lastGrantedN, N_AUTO);
    }

    RESULTS();
}
