#pragma once
#include <stdint.h>
#include <stddef.h>

// Hardware Abstraction Layer — platform (UUV) side.
//
// Implement for:
//   Simulator  → SimPlatformHAL   (platform_sim/sim_platform_hal.h)
//   Teensy 4.x → TeensyPlatformHAL (sketch/teensy_platform_hal.h)
//
// All methods may be called from the protocol tick() or from the UDP
// receive callback.  Implementations must be thread/ISR-safe where noted.

class PlatformHAL {
public:
    virtual ~PlatformHAL() = default;

    // ── Time ─────────────────────────────────────────────────────────────────
    // Monotonic millisecond counter.
    // Arduino: millis()
    virtual uint32_t nowMs() = 0;

    // ── Entropy ──────────────────────────────────────────────────────────────
    // Return a 32-bit value from the hardware TRNG / CSPRNG.
    // Must not block indefinitely; caller retries on nonce collision.
    // Arduino: read TRNG_ENT0..ENT15 registers (Teensy 4.x i.MX RT1062).
    //          Poll TRNG_MCTL_ENT_VAL before first read.
    //          Use a ChaCha20/AES-CTR-DRBG seeded from TRNG for speed.
    // ISR-safe: yes (reads from seeded DRBG state).
    virtual uint32_t randomU32() = 0;

    // ── Relay ─────────────────────────────────────────────────────────────────
    // Drive the relay enable line.
    //   closed = true  → 24 V bus active, motors may run
    //   closed = false → 24 V bus open,   safe state
    // Arduino: GPIO → optocoupler → relay coil.
    //          Must default to false (safe) on power-on and brownout.
    // ISR-safe: yes (single GPIO write).
    virtual void relaySet(bool closed) = 0;

    // Read back actual relay contact state via force-guided auxiliary contact.
    // Used by BIT and ongoing fault monitoring.
    // Returns true if relay is confirmed closed.
    // Arduino: digitalRead(RELAY_READBACK_PIN)
    virtual bool relayReadback() = 0;

    // ── Watchdog ──────────────────────────────────────────────────────────────
    // Arm the external watchdog with the given lease window.
    // Called once at the start of each lease.  The watchdog opens the relay
    // autonomously if watchdogKick() is not called within windowMs.
    // Arduino: configure TPL5010 delay pin / DS1374 alarm / MAX6369 timeout.
    //          Separate power domain from MCU so MCU hang cannot prevent trip.
    virtual void watchdogArm(uint32_t windowMs) = 0;

    // Kick the watchdog — proves firmware is alive and authorized.
    // Must be called before each watchdog window expires while ARMED.
    // Kick ordering: nonce consumed from buffer BEFORE this call (see spec).
    // Arduino: pulse TPL5010 DONE pin; or send correct Q&A byte to DS1374.
    virtual void watchdogKick() = 0;

    // ── Built-In Test ─────────────────────────────────────────────────────────
    // Mandatory self-test executed on every arm attempt before relays close.
    // Sequence:
    //   1. relaySet(true)               — close relay under test
    //   2. watchdogArm(BIT_WINDOW_MS)  — arm with a short test window
    //   3. Do NOT kick watchdog         — let it trip
    //   4. Wait BIT_WINDOW_MS + margin
    //   5. Check relayReadback() == false → watchdog opened relay correctly
    //   6. relaySet(false)              — ensure relay is open before returning
    // Returns true if relay opened as expected; false on failure (FAULT).
    // This call is BLOCKING (up to ~BIT_WINDOW_MS + margin).
    // Arduino: safe to call from loop() during arming; AsyncUDP buffers any
    //          incoming packets during the brief block.
    virtual bool performBIT() = 0;

    // ── UDP send ──────────────────────────────────────────────────────────────
    // Transmit a packet to the home unit.
    // The destination address is configured at construction time (home IP/port).
    // Arduino: udp.writeTo(buf, len, homeIp, homePort)
    // ISR-safe: implementation must be (AsyncUDP writeTo is not ISR-safe on
    //           Teensy; queue the send and flush from loop() if needed).
    virtual void sendPacket(const uint8_t* buf, size_t len) = 0;
};
