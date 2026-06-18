#include "sim_platform_hal.h"
#include <chrono>
#include <random>
#include <cstdio>
#include <unistd.h>

// ── Monotonic clock origin (set at first call) ────────────────────────────────
static uint64_t s_epoch_us = 0;

static uint64_t monotonic_us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

SimPlatformHAL::SimPlatformHAL(int sock, const sockaddr_in& homeAddr)
    : _sock(sock), _homeAddr(homeAddr),
      _simRelayClosed(false),
      _wdWindowMs(0), _wdArmMs(0), _wdLastKickMs(0)
{
    s_epoch_us = monotonic_us();
}

// ── Time ──────────────────────────────────────────────────────────────────────

uint32_t SimPlatformHAL::nowMs()
{
    return (uint32_t)((monotonic_us() - s_epoch_us) / 1000ULL);
}

// ── Entropy ───────────────────────────────────────────────────────────────────
// Placeholder: uses std::mt19937 seeded from /dev/urandom.
// On Teensy: replace with TRNG_ENT register reads.

uint32_t SimPlatformHAL::randomU32()
{
    static std::mt19937 rng = [](){
        std::mt19937 r;
        uint32_t seed;
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) { fread(&seed, sizeof(seed), 1, f); fclose(f); }
        r.seed(seed);
        return r;
    }();
    return rng();
}

// ── Relay ─────────────────────────────────────────────────────────────────────
// Placeholder: set/read an in-memory flag.
// On Teensy: GPIO write to optocoupler enable; read force-guided aux contact.

void SimPlatformHAL::relaySet(bool closed)
{
    _simRelayClosed = closed;
    // HAL_RELAY_SET placeholder — on Teensy: digitalWrite(RELAY_PIN, closed ? HIGH : LOW);
}

bool SimPlatformHAL::relayReadback()
{
    // HAL_RELAY_READBACK placeholder — on Teensy: digitalRead(RELAY_FB_PIN)
    return _simRelayClosed;
}

// ── Watchdog ──────────────────────────────────────────────────────────────────
// Placeholder: record arm parameters and kick time for display.
// On Teensy: configure TPL5010/DS1374/MAX6369 via GPIO or I²C.

void SimPlatformHAL::watchdogArm(uint32_t windowMs)
{
    // HAL_WATCHDOG_ARM placeholder
    // On Teensy: program external WD IC timeout register / delay resistor value
    _wdWindowMs = windowMs;
    _wdArmMs    = nowMs();
}

void SimPlatformHAL::watchdogKick()
{
    // HAL_WATCHDOG_KICK placeholder
    // On Teensy (TPL5010): pulse DONE pin low for ≥ 100 µs, then high
    //   digitalWrite(WD_DONE_PIN, LOW);
    //   delayMicroseconds(200);
    //   digitalWrite(WD_DONE_PIN, HIGH);
    //
    // On Teensy (DS1374 Q&A): write correct response byte over I²C
    //
    // On Teensy (windowed WD): kick must fall within [T_OPEN, T_CLOSE] window
    _wdLastKickMs = nowMs();
}

// ── Built-In Test ─────────────────────────────────────────────────────────────
// Placeholder: simulate a passing BIT instantly.
// On Teensy:
//   1. relaySet(true)
//   2. watchdogArm(BIT_WINDOW_MS)     — e.g. 200 ms
//   3. delay(BIT_WINDOW_MS + 50)      — do NOT kick; let watchdog trip
//   4. result = (relayReadback() == false)
//   5. relaySet(false)
//   6. return result
//
// If relay did not open: watchdog or relay is faulty — refuse to arm.

bool SimPlatformHAL::performBIT()
{
    // HAL_PERFORM_BIT placeholder
    // Simulator: briefly simulate relay closed → watchdog trip → relay open
    relaySet(true);
    // [On real HW: block here for BIT_WINDOW_MS + margin and verify readback]
    relaySet(false);
    return true;   // stub: always passes
}

// ── UDP send ──────────────────────────────────────────────────────────────────

void SimPlatformHAL::sendPacket(const uint8_t* buf, size_t len)
{
    sendto(_sock, buf, len, 0,
           (const sockaddr*)&_homeAddr, sizeof(_homeAddr));
}
