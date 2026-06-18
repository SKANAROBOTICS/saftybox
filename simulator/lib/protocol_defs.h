#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Packet ───────────────────────────────────────────────────────────────────
static constexpr size_t   PACKET_LEN      = 3;   // both directions, bytes

// ── Protocol timing (tune to acoustic link characterisation) ─────────────────
static constexpr uint32_t T_CHALLENGE_MS   = 250;  // interval between challenges
static constexpr uint32_t NONCE_MAX_AGE_MS = 5000; // nonce validity window
static constexpr uint8_t  NONCE_BUF_K      = 8;    // circular buffer depth

// ── Lease exponent range ──────────────────────────────────────────────────────
static constexpr uint8_t N_MIN  = 0;
static constexpr uint8_t N_MAX  = 16;  // hard ceiling; personality plug may lower
static constexpr uint8_t N_AUTO = 2;   // AUTO mode: 2^2 = 4 s lease, ~1 s kick

// ── Status nibble (robot → home, free-riding in challenge packet) ─────────────
static constexpr uint8_t STATUS_RELAY_CLOSED = 0x01;
static constexpr uint8_t STATUS_BIT_OK       = 0x02;
static constexpr uint8_t STATUS_FAULT        = 0x04;
// 0x08: reserved

// ── MAC ──────────────────────────────────────────────────────────────────────
static constexpr size_t MAC_KEY_LEN = 16;  // SipHash-2-4 key = 128 bits

// ── Network defaults ─────────────────────────────────────────────────────────
static constexpr uint16_t PORT_PLATFORM = 5760;
static constexpr uint16_t PORT_HOME     = 5761;
