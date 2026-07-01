#include "FailsafeSupervisor.h"

void FailsafeSupervisor::begin(SafetyChannel *channels, uint8_t count) {
    _channels = channels;
    _count = count;
}

bool FailsafeSupervisor::armWith(uint32_t baseLeaseMs, int32_t offset1Ms, int32_t offset2Ms) {
    if (!_seenRevoke) return false;

    int32_t lease1 = (int32_t)baseLeaseMs + offset1Ms;
    int32_t lease2 = (int32_t)baseLeaseMs + offset2Ms;
    if (lease1 < 1) lease1 = 1;
    if (lease2 < 1) lease2 = 1;
    _channels[0].arm((uint32_t)lease1);
    _channels[1].arm((uint32_t)lease2);
    _leaseMs = baseLeaseMs;   // supervisor expects the BASE lease -- any offset is the fault
    _lastArmMs = millis();
    _running = true;
    _latched = false;
    _mismatching = false;
    return true;
}

void FailsafeSupervisor::stopAll() {
    for (uint8_t i = 0; i < _count; i++) _channels[i].stop();
    _running = false;
    _latched = false;
    _mismatching = false;
    _seenRevoke = true;   // explicit revoke seen -- unlocks the next armWith()
}

bool FailsafeSupervisor::update() {
    if (!_running) return false;

    uint32_t elapsed = millis() - _lastArmMs;
    bool expectSafe = elapsed >= _leaseMs;

    bool mismatch = false;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_channels[i].matchesExpected(expectSafe)) mismatch = true;
    }

    if (mismatch) {
        if (!_mismatching) {
            _mismatching = true;
            _mismatchSinceMs = millis();
        } else if (!_latched && millis() - _mismatchSinceMs >= toleranceMs()) {
            _latched = true;
            _seenRevoke = false;  // re-lock: a fresh explicit stop is required before the
                                   // next arm can succeed, same gate as after a fresh boot
        }
    } else {
        _mismatching = false;
    }

    if (_latched) {
        for (uint8_t i = 0; i < _count; i++) _channels[i].forceSafe();  // redundant, every tick
    }

    return _latched;
}

uint32_t FailsafeSupervisor::toleranceMs() const {
    const uint32_t FLOOR_MS = 5;
    const uint32_t DRIFT_PPM = 100;
    uint32_t scaled = (uint32_t)(((uint64_t)_leaseMs * DRIFT_PPM) / 1000000UL);
    return scaled > FLOOR_MS ? scaled : FLOOR_MS;
}
