#pragma once
#include <Arduino.h>

// One independent, timer-backed safety output. Currently backed by IntervalTimer/PIT (reverted
// from GPT1+GPT2, which hung the board twice with the root cause not yet isolated -- see
// 01-platform-hardware/failsafe-design-options.md). The pin stays in plain GPIO the whole time;
// the ISR's only job is one digitalWrite to force the pin to its safe level.
//
// Supports up to 2 instances (index 0/1) -- IntervalTimer needs a plain function pointer per
// instance, so the ISR trampolines are fixed at compile time. Extend isr0/isr1/_instances if a
// third channel is ever needed.
class SafetyChannel {
public:
    // index: 0 or 1 (selects which ISR trampoline this instance owns).
    // pin: the digital pin this channel drives.
    // safeLevel: HIGH or LOW -- the level that means safe for this channel.
    void begin(uint8_t index, uint8_t pin, bool safeLevel);

    // (Re)starts the countdown fresh for leaseMs, asserting enable immediately.
    void arm(uint32_t leaseMs);

    // Cancels any running countdown and forces the pin safe immediately.
    void stop();

    // Forces the pin safe without touching the timer -- used by the supervisor to redundantly
    // re-assert safe every tick while latched.
    void forceSafe();

    // Does the pin currently match what it should be, given whether the lease should be
    // considered expired yet?
    bool matchesExpected(bool expectSafe) const;

private:
    void onExpire();
    static void isr0();
    static void isr1();

    uint8_t _index = 0;
    uint8_t _pin = 0;
    bool _safeLevel = HIGH;
    IntervalTimer _timer;

    static SafetyChannel *_instances[2];
};
