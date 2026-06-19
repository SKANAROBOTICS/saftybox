#pragma once
#include "protocol_defs.h"

struct NonceEntry {
    uint32_t nonce;
    uint32_t expires_at_ms;
    bool     valid;
};

// Circular buffer of outstanding nonces (robot side).
// K slots; oldest displaced on overflow.
class NonceBuf {
public:
    NonceBuf();

    // Add nonce to buffer.  Returns false if nonce already present (caller
    // must regenerate).  Displaces oldest slot if buffer is full.
    bool add(uint32_t nonce, uint32_t now_ms, uint32_t max_age_ms);

    // Search the buffer (newest first) for a slot whose computed MAC matches R.
    // On first match: consume (remove) the slot and return true.
    // Prunes expired entries before searching.
    bool consume(const uint8_t key[MAC_KEY_LEN], uint8_t n, uint32_t R,
                 uint32_t now_ms);

    // True if nonce is already present in a valid, non-expired slot.
    bool contains(uint32_t nonce) const;

    uint8_t count() const;

private:
    void prune(uint32_t now_ms);   // called by consume(); removes expired entries
    NonceEntry _buf[NONCE_BUF_K];
    uint8_t    _head;   // next write index (circular)
};
