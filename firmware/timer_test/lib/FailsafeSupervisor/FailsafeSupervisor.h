#pragma once
#include <Arduino.h>
#include "SafetyChannel.h"

// Owns a set of SafetyChannels, tracks the current *intended* base lease, and runs the
// self-test: does elapsed time since the last arm match what the channels are actually doing?
// A sustained mismatch latches -- sticky, forces safe, stops accepting further arms until an
// explicit stop is seen (matches the existing TRIPPED-state philosophy elsewhere in this
// project: latched, requires an explicit disarm, not self-clearing).
//
// Also enforces the boot/post-fault revoke-gate: armWith() refuses to do anything until an
// explicit stopAll() (n=0) has been seen at least once, since boot or since the last latch.
// This is enforced HERE, in the timers layer, independent of whatever the layer above (manual
// serial input today; the real protocol later) thinks is a valid request.
class FailsafeSupervisor {
public:
    void begin(SafetyChannel *channels, uint8_t count);

    // Returns false (and does nothing) if the revoke-gate hasn't been satisfied yet.
    bool armWith(uint32_t baseLeaseMs, int32_t offset1Ms, int32_t offset2Ms);

    // Immediately forces all channels safe and cancels their timers. Satisfies the revoke-gate
    // for the next armWith().
    void stopAll();

    // Call every loop() iteration. Returns true if latched (caller should stop re-arming).
    bool update();

    bool running() const { return _running; }
    bool latched() const { return _latched; }
    bool seenRevoke() const { return _seenRevoke; }
    uint32_t leaseMs() const { return _leaseMs; }
    uint32_t elapsedMs() const { return millis() - _lastArmMs; }

private:
    uint32_t toleranceMs() const;

    SafetyChannel *_channels = nullptr;
    uint8_t _count = 0;
    uint32_t _leaseMs = 0;
    uint32_t _lastArmMs = 0;
    bool _running = false;
    bool _latched = false;
    bool _mismatching = false;
    uint32_t _mismatchSinceMs = 0;
    bool _seenRevoke = false;  // starts false at boot -- must see an explicit stopAll() before
                                // the first armWith() will ever succeed
};
