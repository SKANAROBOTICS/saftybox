#include "home_node.h"
#include "packet.h"
#include "mac.h"
#include <string.h>

HomeNode::HomeNode(HomeHAL& hal, const uint8_t key[MAC_KEY_LEN])
    : _hal(hal), _mode(HomeMode::HOLD), _n(N_MIN), _mushroomActive(true)
{
    memcpy(_key, key, MAC_KEY_LEN);
    memset(&_status, 0, sizeof(_status));
}

// ── onPacket (callback context) ──────────────────────────────────────────────

void HomeNode::onPacket(const uint8_t* buf, size_t len)
{
    if (len != PACKET_LEN) return;

    uint8_t  echoedN; uint32_t nonce;
    packet_decode_challenge(buf, echoedN, nonce);

    _status.lastRxMs = _hal.nowMs();
    _status.echoedN  = echoedN;

    // Mushroom latch overrides mode — send n=0 on every challenge while latched.
    if (_mushroomActive) {
        _sendRevoke();
        return;
    }

    // Decide whether to respond
    switch (_mode) {
    case HomeMode::HOLD:
        break;                      // no response

    case HomeMode::SINGLE:
    case HomeMode::AUTO:
        _respondTo(nonce);          // spring-back for SINGLE is handled by the UI/key
        break;
    }
}

// ── tick (main loop) ─────────────────────────────────────────────────────────

void HomeNode::tick() { /* no periodic work currently needed */ }

// ── UI commands ──────────────────────────────────────────────────────────────

void HomeNode::setMode(HomeMode mode)
{
    _mode = mode;
}

void HomeNode::setN(uint8_t n)
{
    if (n < N_MIN) n = N_MIN;
    if (n > N_MAX) n = N_MAX;
    _n = n;
}

void HomeNode::mushroom()
{
    _mushroomActive = true;
    _mode = HomeMode::HOLD;     // suggested default; mode key can pre-set for when released
}

void HomeNode::releaseMushroom()
{
    _mushroomActive = false;    // mode key now takes effect again
}

// ── Private ──────────────────────────────────────────────────────────────────

void HomeNode::_respondTo(uint32_t nonce)
{
    uint8_t effectiveN = (_mode == HomeMode::AUTO) ? N_AUTO : _n;
    uint32_t R = mac_compute(_key, nonce, effectiveN);

    uint8_t buf[PACKET_LEN];
    packet_encode_response(effectiveN, R, buf);
    _hal.sendPacket(buf, PACKET_LEN);

    _status.lastGrantedN = effectiveN;
    _status.lastGrantMs  = _hal.nowMs();
}

void HomeNode::_sendRevoke()
{
    // Unauthenticated revoke: n=0, R=0 — platform accepts without MAC check.
    // lastGrantedN / lastGrantMs intentionally NOT updated: the panel countdown
    // continues so the operator can see when the robot's old lease expires.
    uint8_t buf[PACKET_LEN];
    packet_encode_response(0, 0, buf);
    _hal.sendPacket(buf, PACKET_LEN);
}
