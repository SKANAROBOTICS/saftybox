#pragma once
#include "../lib/hal/platform_hal.h"
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Simulator implementation of PlatformHAL.
// All hardware calls are stubbed; relay state is a boolean in memory.
// Thread-safe: sendPacket may be called from the UDP receive thread.

class SimPlatformHAL : public PlatformHAL {
public:
    SimPlatformHAL(int sock, const sockaddr_in& homeAddr);

    // ── PlatformHAL interface ─────────────────────────────────────────────────
    uint32_t nowMs()                               override;
    uint32_t randomU32()                           override;
    void     relaySet(bool closed)                 override;
    bool     relayReadback()                       override;
    void     watchdogArm(uint32_t windowMs)        override;
    void     watchdogKick()                        override;
    bool     performBIT()                          override;
    void     sendPacket(const uint8_t* buf,
                        size_t len)                override;

    // ── Sim-only diagnostics ──────────────────────────────────────────────────
    bool     simRelayState()      const { return _simRelayClosed; }
    uint32_t simWatchdogArmMs()   const { return _wdArmMs; }
    uint32_t simWatchdogKickMs()  const { return _wdLastKickMs; }
    uint32_t simWatchdogWindowMs()const { return _wdWindowMs; }

private:
    int           _sock;
    sockaddr_in   _homeAddr;

    bool          _simRelayClosed;
    uint32_t      _wdWindowMs;
    uint32_t      _wdArmMs;
    uint32_t      _wdLastKickMs;
};
