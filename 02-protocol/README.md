# Trusted Heartbeat — Authorization Protocol

> Pillar 2 of 3 · handle **Trust** · [↑ overview](../README.md)

## Purpose
Continuously establish and prove **authorization to run** (the *lease*) between the certified home
and the UUV, over an **extremely low-bandwidth** link.

## Main challenge
Motors enable **only** on a **genuine, fresh, intended** heartbeat — immune to **accidental**
triggering and **record/replay**. (Resistance to a *malicious* actor is explicitly **out of scope**.)

## Approach / key decisions
- **Challenge-response.** UUV emits a fresh random **nonce**; home returns
  `R = truncate₂( MAC_key( ver ‖ mode ‖ nonce ) )`; UUV recomputes and **constant-time** compares.
- **Freshness = replay immunity.** A recorded `(nonce, R)` pair is useless next cycle because the
  nonce changed. A 2-byte response gives ≈ 1/65536 odds against a *fresh* challenge → accidental
  success is effectively impossible.
- **Symmetric truncated MAC** (HMAC/CMAC). Shared key on both ends, **injected at flash time** so
  per-pair keys don't need per-pair firmware.
- **Mode bound into the MAC** — a captured short-lease response can't be replayed for a longer one.
- **Lease primitive.** Each valid heartbeat extends `authorized-until` by `lease(mode)`:
  - **AUTO** ≈ 3 s lease, renewed ~every 1 s (live-comms operation).
  - **SINGLE-SHOT** 2 / 5 / 10 min windows (link may go dark).
  - Both feed one timer; **longest expiry wins**. **Revoke/disable is unauthenticated** (only drives
    toward OFF → always safe).
  - Lease **value** is chosen here/UX; the **ceiling** is enforced in hardware.
- **Transport: independent UDP**, fully decoupled from MAVLink/PX4. Payload is a few bytes.
- **Robot status** rides back in a 1-byte field piggybacked on the challenge (free in AUTO).

## Threat model
In scope: accidental generation, record/playback. Out of scope: key extraction, MITM, a determined
adversary. (Recorded in [Safety Case](../04-safety-case.md).)

## Interfaces
- **Out (to HW):** "valid heartbeat → one kick." Protocol decides *when*; HW defines *what*.
- **In (from UX):** the `mode` index (lease selection), the arming gate (authorizes start), and the
  revoke/stop command.

## Open questions
- Nonce width and entropy source (HW RNG vs seeded CSPRNG).
- Exact packet byte layout (versioning, type, framing).
- Final AUTO interval / timeout values; single-shot **refresh** semantics.
- Multi-UUV addressing (single robot vs select-among-several).
