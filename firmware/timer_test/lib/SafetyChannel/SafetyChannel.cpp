#include "SafetyChannel.h"

SafetyChannel *SafetyChannel::_instances[2] = { nullptr, nullptr };

void SafetyChannel::begin(uint8_t index, uint8_t pin, bool safeLevel) {
    _index = index;
    _pin = pin;
    _safeLevel = safeLevel;
    pinMode(_pin, OUTPUT);
    digitalWriteFast(_pin, _safeLevel);
    _instances[index] = this;
}

void SafetyChannel::arm(uint32_t leaseMs) {
    digitalWriteFast(_pin, !_safeLevel);   // assert enable
    _timer.end();                          // stop any previous countdown cleanly
    _timer.begin(_index == 0 ? isr0 : isr1, (uint64_t)leaseMs * 1000ULL);
}

void SafetyChannel::stop() {
    _timer.end();
    digitalWriteFast(_pin, _safeLevel);
}

void SafetyChannel::forceSafe() {
    digitalWriteFast(_pin, _safeLevel);
}

bool SafetyChannel::matchesExpected(bool expectSafe) const {
    bool expected = expectSafe ? _safeLevel : !_safeLevel;
    return digitalRead(_pin) == expected;
}

void SafetyChannel::onExpire() {
    digitalWriteFast(_pin, _safeLevel);
}

void SafetyChannel::isr0() { if (_instances[0]) _instances[0]->onExpire(); }
void SafetyChannel::isr1() { if (_instances[1]) _instances[1]->onExpire(); }
