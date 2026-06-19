#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Packet ───────────────────────────────────────────────────────────────────
static constexpr size_t   PACKET_LEN      = 3;   // both directions, bytes

// ── Protocol timing (tune to acoustic link characterisation) ─────────────────
static constexpr uint32_t T_CHALLENGE_MS   = 500;  // interval between challenges
static constexpr uint32_t NONCE_MAX_AGE_MS = 5000; // nonce validity window
static constexpr uint8_t  NONCE_BUF_K      = 8;    // circular buffer depth

// ── Lease exponent range ──────────────────────────────────────────────────────
// n=0 is reserved as the unauthenticated revoke signal — not selectable by operator.
// Packet field n is 4 bits → ceiling is 15 (2^15 = 32 768 s ≈ 9 h).
static constexpr uint8_t N_MIN  = 1;
static constexpr uint8_t N_MAX  = 15;  // hard ceiling; personality plug may lower
static constexpr uint8_t N_AUTO = 2;   // AUTO mode: 2^2 = 4 s lease, 500 ms challenge interval

// ── Status nibble (robot → home, free-riding in challenge packet) ─────────────
// Carries the last n the robot successfully validated (its current lease exponent).
// 0 = no active lease (robot stopped, tripped, or revoke received).

// ── MAC ──────────────────────────────────────────────────────────────────────
static constexpr size_t MAC_KEY_LEN = 16;  // SipHash-2-4 key = 128 bits

// ── Network defaults ─────────────────────────────────────────────────────────
// Ports: change here to match deployment.
// IPs: passed as first CLI args to platform_sim and home_app (default 127.0.0.1).
//   platform_sim  key.bin  <home_ip>    <home_port>  <my_port>
//   home_app      key.bin  <platform_ip> <plat_port> <my_port>
static constexpr uint16_t PORT_PLATFORM = 5760;
static constexpr uint16_t PORT_HOME     = 5761;
