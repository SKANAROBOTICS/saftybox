#pragma once
#include "protocol_defs.h"
#include "hal/home_hal.h"

enum class HomeMode : uint8_t {
    HOLD,    // mode key at HOLD — not sending new grants; existing lease runs out naturally
    SINGLE,  // send grants on every challenge while key is held; springs back to HOLD on release
    AUTO,    // send grants on every challenge; stays latched until key moved to HOLD
};

struct HomeStatus {
    uint8_t  echoedN;       // last n echoed by robot in challenge (its current lease exponent; 0=none)
    uint32_t lastRxMs;      // timestamp of last challenge received (0 = never)
    uint8_t  lastGrantedN;  // last non-zero n sent in a response (not updated on revoke)
    uint32_t lastGrantMs;   // timestamp of last non-zero response (for REMAINING countdown)
};
// Panel colour interpretation of echoedN vs lastGrantedN:
//   echoedN == 0            → ROBOT LED green  (robot safe/stopped)
//   echoedN >  0            → ROBOT LED red    (robot holds active lease)

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

    void _respondTo(uint32_t nonce);
    void _sendRevoke();         // sends n=0, R=0 (unauthenticated revoke)

    HomeHAL& _hal;
    uint8_t  _key[MAC_KEY_LEN];

    HomeMode   _mode;
    uint8_t    _n;
    HomeStatus _status;
    bool       _mushroomActive; // true while UN-ARM is latched; overrides mode on every challenge
};
