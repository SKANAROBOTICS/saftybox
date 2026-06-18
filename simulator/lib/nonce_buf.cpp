#include "nonce_buf.h"
#include "mac.h"
#include <string.h>

NonceBuf::NonceBuf() : _head(0)
{
    memset(_buf, 0, sizeof(_buf));
}

bool NonceBuf::add(uint32_t nonce, uint32_t now_ms, uint32_t max_age_ms)
{
    if (contains(nonce)) return false;

    _buf[_head] = { nonce, now_ms + max_age_ms, true };
    _head = (_head + 1) % NONCE_BUF_K;
    return true;
}

bool NonceBuf::consume(const uint8_t key[MAC_KEY_LEN], uint8_t n, uint32_t R,
                       uint32_t now_ms)
{
    prune(now_ms);
    // Search newest-first (most recently added slot = _head - 1, wrapping)
    for (uint8_t i = 0; i < NONCE_BUF_K; i++) {
        uint8_t idx = (_head + NONCE_BUF_K - 1 - i) % NONCE_BUF_K;
        NonceEntry& e = _buf[idx];
        if (!e.valid) continue;

        if (mac_compute(key, e.nonce, n) == R) {
            e.valid = false;    // consume — must happen before watchdog kick
            return true;
        }
    }
    return false;
}

void NonceBuf::prune(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NONCE_BUF_K; i++) {
        if (_buf[i].valid && now_ms >= _buf[i].expires_at_ms)
            _buf[i].valid = false;
    }
}

bool NonceBuf::contains(uint32_t nonce) const
{
    for (uint8_t i = 0; i < NONCE_BUF_K; i++) {
        if (_buf[i].valid && _buf[i].nonce == nonce) return true;
    }
    return false;
}

uint8_t NonceBuf::count() const
{
    uint8_t c = 0;
    for (uint8_t i = 0; i < NONCE_BUF_K; i++)
        if (_buf[i].valid) c++;
    return c;
}
