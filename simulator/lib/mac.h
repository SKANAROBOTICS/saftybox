#pragma once
#include "protocol_defs.h"

// Compute R = truncate_20( SipHash-2-4_key( nonce || n ) )
//
// key:   MAC_KEY_LEN (16) bytes
// nonce: 20-bit value
// n:     4-bit lease exponent
// Returns a 20-bit value (upper 44 bits of SipHash output discarded).
//
// Arduino note: SipHash-2-4 is compact (~60 lines, no lookup tables),
// runs in <1 µs on Cortex-M7 — suitable for both home and platform MCUs.
uint32_t mac_compute(const uint8_t key[MAC_KEY_LEN], uint32_t nonce, uint8_t n);
