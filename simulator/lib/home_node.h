#pragma once
#include "protocol_defs.h"
#include "hal/home_hal.h"

enum class HomeMode : uint8_t {
    HOLD,    // mode key at HOLD — not sending new grants; existing lease runs out naturally
    SINGLE,  // send grants on every challenge while key is held; springs back to HOLD on release
    AUTO,    // send grants on every challenge; stays latched until key moved to HOLD
};

struct HomeStatus {
    bool     linkLive;      // received a challenge within LINK_TIMEOUT_MS
    uint8_t  echoedN;       // last n the robot confirmed (status nibble from challenge)
    uint32_t lastRxMs;      // timestamp of last received challenge
    uint8_t  lastGrantedN;  // n value of last non-zero response sent (not updated on revoke)
    uint32_t lastGrantMs;   // timestamp of last non-zero response sent (for REMAINING countdown)
};

class HomeNode {
public:
    HomeNode(HomeHAL& hal, const uint8_t key[MAC_KEY_LEN]);

    // ── UDP receive callback ──────────────────────────────────────────────────
    // Call from AsyncUDP.onPacket().
    void onPacket(const uint8_t* buf, size_t len);

    // ── Main loop tick ────────────────────────────────────────────────────────
    void tick();

    // ── UI commands (called from panel controls) ──────────────────────────────
    void setMode(HomeMode mode);
    void setN(uint8_t n);          // program key UP/DOWN; clamped to [N_MIN, N_MAX]
    void mushroom();               // UN-ARM latch — sends n=0 on EVERY challenge until released
    void releaseMushroom();        // release the UN-ARM latch; mode key resumes

    // ── State queries ─────────────────────────────────────────────────────────
    HomeMode       mode()          const { return _mode; }
    uint8_t        n()             const { return _n; }
    HomeStatus     status()        const { return _status; }
    bool           mushroomActive()const { return _mushroomActive; }
    bool           linkLive()            { return _hal.linkLive(); }

private:
    static constexpr uint32_t LINK_TIMEOUT_MS = 2000;

    void _respondTo(uint32_t nonce);
    void _sendRevoke();         // sends n=0, R=0 (unauthenticated revoke)

    HomeHAL& _hal;
    uint8_t  _key[MAC_KEY_LEN];

    HomeMode   _mode;
    uint8_t    _n;
    HomeStatus _status;
    bool       _mushroomActive; // true while UN-ARM is latched; overrides mode on every challenge
};
