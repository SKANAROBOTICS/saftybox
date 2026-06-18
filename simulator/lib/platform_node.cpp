#include "platform_node.h"
#include "packet.h"
#include "mac.h"
#include <string.h>

PlatformNode::PlatformNode(PlatformHAL& hal,
                           const uint8_t key[MAC_KEY_LEN],
                           uint8_t nMax)
    : _hal(hal), _nMax(nMax),
      _state(PlatformState::SAFE), _relayClosed(false),
      _lastN(0), _leaseExpiresMs(0), _nextChallengeMs(0),
      _pendingArm(false), _pendingN(0), _pendingDisarm(false)
{
    memcpy(_key, key, MAC_KEY_LEN);
}

// ── onPacket (callback context) ──────────────────────────────────────────────

void PlatformNode::onPacket(const uint8_t* buf, size_t len)
{
    if (len != PACKET_LEN) return;
    if (_state != PlatformState::SAFE && _state != PlatformState::ARMED) return;

    uint8_t n; uint32_t R;
    if (!packet_decode_response(buf, n, R)) return;

    // n=0: unauthenticated revoke — no MAC check; safe-only, cannot be misused to arm
    if (n == 0) {
        _pendingDisarm = true;
        return;
    }

    if (n > _nMax) return;

    // prune + consume inside a single call
    if (_nonces.consume(_key, n, R, _hal.nowMs())) {
        // Valid grant — signal tick() to arm (or refresh lease)
        _pendingN   = n;
        _pendingArm = true;
        // Kick happens in tick() after optional BIT
    }
}

// ── Private helpers ──────────────────────────────────────────────────────────

void PlatformNode::_enterSafe()
{
    _state        = PlatformState::SAFE;
    _relayClosed  = false;
    _hal.relaySet(false);
    _nonces       = NonceBuf();
    _pendingArm   = false;
    _pendingDisarm= false;
}

void PlatformNode::_runArming()
{
    // BIT is a blocking HAL call.  Arduino: runs synchronously in loop();
    // AsyncUDP will buffer incoming packets during the brief block.
    if (_hal.performBIT()) {
        _enterArmed(_pendingN);
    } else {
        _state       = PlatformState::TRIPPED;
        _relayClosed = false;
        _hal.relaySet(false);
    }
    _pendingArm = false;
}

void PlatformNode::_enterArmed(uint8_t n)
{
    uint32_t windowMs   = (1UL << n) * 1000UL;
    _lastN              = n;
    _leaseExpiresMs     = _hal.nowMs() + windowMs;
    _state              = PlatformState::ARMED;
    _relayClosed        = true;
    _hal.watchdogArm(windowMs);
    _hal.relaySet(true);
    _hal.watchdogKick();     // first kick immediately after arming
}

void PlatformNode::_sendChallenge()
{
    uint32_t nonce;
    uint8_t  attempts = 0;
    do {
        nonce = _hal.randomU32() & 0x000FFFFF;
        if (++attempts > 10) return;   // TRNG trouble; skip this interval
    } while (_nonces.contains(nonce));

    if (!_nonces.add(nonce, _hal.nowMs(), NONCE_MAX_AGE_MS)) return;

    uint8_t buf[PACKET_LEN];
    packet_encode_challenge(statusNibble(), nonce, buf);
    _hal.sendPacket(buf, PACKET_LEN);
    _nextChallengeMs = _hal.nowMs() + T_CHALLENGE_MS;
}

// ── tick (main loop) ─────────────────────────────────────────────────────────

void PlatformNode::tick()
{
    uint32_t now = _hal.nowMs();

    if (_pendingDisarm) {
        _pendingDisarm = false;
        _pendingArm    = false;
        _enterSafe();
        return;
    }

    switch (_state) {

    case PlatformState::SAFE:
        if (now >= _nextChallengeMs) _sendChallenge();
        if (_pendingArm) {
            _state = PlatformState::ARMING;
            // _runArming() will be called next iteration so display can update
        }
        break;

    case PlatformState::ARMING:
        _runArming();   // blocking; returns ARMED or TRIPPED
        break;

    case PlatformState::ARMED:
        // Heartbeat challenges
        if (now >= _nextChallengeMs) _sendChallenge();

        // Lease refresh from incoming grant
        if (_pendingArm) {
            _pendingArm = false;
            _enterArmed(_pendingN);     // resets lease timer + kicks watchdog
            break;
        }

        // Belt-and-suspenders: software lease expiry check
        // (watchdog is the primary cutoff authority)
        if (now >= _leaseExpiresMs) {
            _enterSafe();
            break;
        }

        // Relay readback fault detection
        if (_hal.relayReadback() != _relayClosed) {
            // Unexpected state — relay welded or opened unexpectedly
            _state       = PlatformState::TRIPPED;
            _relayClosed = false;
            _hal.relaySet(false);
        }
        break;

    case PlatformState::TRIPPED:
        _hal.relaySet(false);   // keep asserting safe in case of transient
        // Stays latched until requestDisarm()
        break;
    }
}

// ── Commands ─────────────────────────────────────────────────────────────────

void PlatformNode::requestDisarm()
{
    _pendingDisarm = true;
}

// ── Queries ──────────────────────────────────────────────────────────────────

uint32_t PlatformNode::leaseRemainingMs(uint32_t now_ms) const
{
    if (_state != PlatformState::ARMED) return 0;
    if (now_ms >= _leaseExpiresMs)      return 0;
    return _leaseExpiresMs - now_ms;
}

uint8_t PlatformNode::statusNibble() const
{
    // Carries last accepted n (current lease exponent), 0 if no active lease.
    if (_state == PlatformState::ARMED) return _lastN & 0x0F;
    return 0;
}
