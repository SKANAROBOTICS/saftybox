#pragma once
#include "protocol_defs.h"
#include "nonce_buf.h"
#include "hal/platform_hal.h"

enum class PlatformState : uint8_t {
    SAFE,         // relays open, sending challenges, awaiting valid grant
    ARMING,       // BIT in progress (blocking in HAL)
    ARMED,        // relays closed, lease active, refreshing via challenges
    TRIPPED,      // watchdog fired or BIT failed; latched — requires requestDisarm()
};

class PlatformNode {
public:
    PlatformNode(PlatformHAL& hal,
                 const uint8_t key[MAC_KEY_LEN],
                 uint8_t nMax = N_MAX);

    // ── UDP receive callback ──────────────────────────────────────────────────
    // Call from AsyncUDP.onPacket() (may be interrupt/callback context).
    // ISR-safe: uses only volatile flags and lock-free buffer ops.
    void onPacket(const uint8_t* buf, size_t len);

    // ── Main loop tick ────────────────────────────────────────────────────────
    // Call from loop() every iteration (or on a 10 ms timer).
    // Drives the state machine, sends challenges, monitors lease expiry.
    void tick();

    // ── State queries (read by display / diagnostics) ────────────────────────
    PlatformState state()       const { return _state; }
    bool          relayClosed() const { return _relayClosed; }
    uint8_t       lastN()       const { return _lastN; }
    uint8_t       nonceCount()  const { return _nonces.count(); }
    uint32_t      leaseRemainingMs(uint32_t now_ms) const;

    // 4-bit status nibble carried in outgoing challenge packets
    uint8_t statusNibble() const;

    // ── Commands ──────────────────────────────────────────────────────────────
    // Request transition to SAFE (+ relay open). Clears TRIPPED latch.
    // Set from any context; consumed on next tick().
    void requestDisarm();

private:
    void _enterSafe();
    void _runArming();          // called from tick() while in ARMING state
    void _enterArmed(uint8_t n);
    void _sendChallenge();

    PlatformHAL& _hal;
    uint8_t      _key[MAC_KEY_LEN];
    uint8_t      _nMax;

    PlatformState _state;
    bool          _relayClosed;
    uint8_t       _lastN;
    uint32_t      _leaseExpiresMs;
    uint32_t      _nextChallengeMs;
    NonceBuf      _nonces;

    // Pending arm parameters set by onPacket(), consumed by tick()
    volatile bool    _pendingArm;
    volatile uint8_t _pendingN;
    volatile bool    _pendingDisarm;
};
