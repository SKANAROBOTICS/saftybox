#include "home_node.h"
#include "packet.h"
#include "mac.h"
#include <string.h>

HomeNode::HomeNode(HomeHAL& hal, const uint8_t key[MAC_KEY_LEN])
    : _hal(hal), _mode(HomeMode::SAFE), _n(N_AUTO), _singleFired(false),
      _sprayCount(0)
{
    memcpy(_key, key, MAC_KEY_LEN);
    memset(&_status, 0, sizeof(_status));
}

// ── onPacket (callback context) ──────────────────────────────────────────────

void HomeNode::onPacket(const uint8_t* buf, size_t len)
{
    if (len != PACKET_LEN) return;

    uint8_t  echoedN; uint32_t nonce;
    if (!packet_decode_challenge(buf, echoedN, nonce)) return;

    _status.lastRxMs = _hal.nowMs();
    _status.echoedN  = echoedN;

    // Revoke spray takes priority over mode (mushroom was pressed)
    if (_sprayCount > 0) {
        _sendRevoke();
        _sprayCount--;
        return;
    }

    // Decide whether to respond
    switch (_mode) {
    case HomeMode::SAFE:
        break;                      // no response

    case HomeMode::SINGLE:
        if (!_singleFired) {
            _respondTo(nonce);
            _singleFired = true;
            _mode        = HomeMode::SAFE;  // key springs back
        }
        break;

    case HomeMode::AUTO:
        _respondTo(nonce);
        break;
    }
}

// ── tick (main loop) ─────────────────────────────────────────────────────────

void HomeNode::tick()
{
    uint32_t now = _hal.nowMs();
    _status.linkLive = (now - _status.lastRxMs) < LINK_TIMEOUT_MS;
}

// ── UI commands ──────────────────────────────────────────────────────────────

void HomeNode::setMode(HomeMode mode)
{
    if (mode == HomeMode::SINGLE) _singleFired = false;
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
    _mode        = HomeMode::SAFE;
    _singleFired = false;
    _sprayCount  = 4;   // spray 4 × n=0 revoke packets ("sprays disable")
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
}

void HomeNode::_sendRevoke()
{
    // Unauthenticated revoke: n=0, R=0 — platform accepts without MAC check.
    uint8_t buf[PACKET_LEN];
    packet_encode_response(0, 0, buf);
    _hal.sendPacket(buf, PACKET_LEN);
    _status.lastGrantedN = 0;
}
