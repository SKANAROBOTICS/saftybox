# Trusted Heartbeat — Authorization Protocol

> Pillar 2 of 3 · handle **Trust** · [↑ overview](../README.md)

## Purpose
Continuously establish and prove **authorization to run** (the *lease*) between the home unit and
the UUV, over an **extremely low-bandwidth, high-latency** link (acoustic).

## Main challenge
Motors enable **only** on a **genuine, fresh, intended** heartbeat — immune to **accidental**
triggering and **record/replay**. (Resistance to a *malicious* actor is explicitly **out of scope**.)

## Packet format

Designed for minimum acoustic payload — both directions are exactly **3 bytes**.

| Direction | Layout | Content |
|---|---|---|
| Robot → Home | `[ status : 4 bits \| nonce : 20 bits ]` | Fresh challenge + echoed last-accepted n |
| Home → Robot | `[ n : 4 bits \| R : 20 bits ]` | Lease grant + truncated MAC |

- **nonce** — 20-bit random value, guaranteed unique within the robot's active buffer (see below).
- **status** — 4-bit field carrying the **last n value the robot successfully validated**
  (i.e., the lease it currently holds). See [Status field](#status-field) below.
- **n** — 4-bit lease exponent chosen by home; lease granted = `2ⁿ` seconds.
- **R** — `truncate₂₀( MAC_key( nonce ‖ n ) )`, the authenticated lease grant.

### Status field

The 4-bit `status` field in the robot's challenge packet echoes the **last n the robot accepted**
from a valid home response. This lets the home confirm its grant was received without adding bytes
or a separate acknowledgement channel.

| Value | Meaning |
|---|---|
| `0` | Robot holds no active lease — stopped, disarmed, or watchdog has fired |
| `1–15` | Robot's current lease level; lease duration = `2ⁿ` seconds |

**n = 0 as the stop value.** Home emits `n = 0` in the response field as an explicit revoke (e.g.,
mushroom pressed, mode key at SAFE). This is consistent with the unauthenticated-revoke principle:
a replayed `n = 0` packet can only drive the robot toward OFF, which is always safe. The PROGRAM
key on the home unit never selects `n = 0`; stop is a separate control action.

**Confirmation semantics.** When the home has just sent `n = 9` and the robot's next challenge
echoes `status = 9`, the grant is confirmed. While the robot still echoes a lower value, the grant
is in flight (acoustic round-trip in progress). If the echoed value does not advance to the
expected n within `max_age + round-trip`, the response was probably lost.

## Robot-side flow

1. Every T seconds, generate a fresh 20-bit **nonce**. Before adding it to the buffer, verify it is
   not already present — regenerate if so (collision probability K/2²⁰, negligible in practice).
   Append to the circular buffer; displace the oldest entry if the buffer is full.
2. Pack `[status : 4 | nonce : 20]` and send over the acoustic link.
3. On receiving any 3-byte reply `(n, R)`:
   - Prune any expired nonces from the buffer first (see below).
   - For each `nonce_i` in the buffer (newest first): compute
     `truncate₂₀( MAC_key( nonce_i ‖ n ) )` and compare to R.
   - **First match** → consume `nonce_i` (remove from buffer), kick the watchdog, grant lease
     `2ⁿ` seconds.
   - No match across the full buffer → discard silently.
4. If the watchdog is not kicked before the current lease expires → relays open.

**Kick ordering:** the nonce must be consumed from the buffer *before* the watchdog kick is issued.

**Nonce expiry:** each buffer slot stores `(nonce, expires_at)` where
`expires_at = time_sent + max_age`. A nonce is only eligible for matching if
`current_time < expires_at`; expired slots are pruned and treated as absent. This bounds the
valid reply window to a defined wall-clock duration regardless of how long the nonce has been
sitting in the buffer — preventing a TCP-tunnelled or otherwise pathologically delayed response
from being accepted. `max_age` is set to comfortably cover the expected worst-case acoustic
round-trip but well below any transport-layer buffering delay (e.g., a few seconds for a
short-range link, tens of seconds for a long-range link). It is a separate parameter from the
lease duration.

**Buffer sizing:** size K so that K × T covers the expected acoustic round-trip, consistent with
`max_age`. A good working heuristic is T ≈ lease / 4 (send ~4 challenges per lease window),
making K ≈ round-trip / T. K is typically small; keeping it small also keeps the false-accept
probability low.

## Home-side flow

1. Receive a challenge from the robot.
2. Operator's mode and the mushroom state determine `n`:
   - Mushroom held → `n = 0` (stop/revoke)
   - Mode = SAFE or TRIPPED → do not respond
   - Mode = SINGLE or AUTO → `n` = operator-selected lease exponent (1–15)
3. Pack the 3-byte MAC input: `msg = (nonce << 4) | n` as a 24-bit big-endian value.
   Equivalently: `msg[0] = nonce>>12`, `msg[1] = (nonce>>4)&0xFF`, `msg[2] = ((nonce&0xF)<<4)|n`.
4. Compute `R = truncate₂₀( HMAC_SHA256_key( msg ) )` — top 20 bits of the digest.
5. Transmit `[n : 4 | R : 20]` — 3 bytes.

Home is stateless beyond the current mode/n selection; the robot's nonce buffer absorbs latency
and reordering.

**Sending n = 0:** a stop/revoke packet uses the same 3-byte format with `n = 0`. Because any
replayed `n = 0` packet is safe (drives toward OFF), authentication is not required — the robot
accepts a valid MAC for `n = 0` as a normal grant with a zero-second lease, which immediately
expires. See [UX description](../03-ux/description-of-operation.md) for when the home emits `n = 0`.

## Why this works

- **Replay immunity** — nonce changes every T seconds; a captured `(n, R)` is valid only against
  the specific nonce it was computed for. Once that nonce is consumed, the response is useless.
- **Buffer uniqueness** — the pre-send uniqueness check ensures all K active nonces are distinct,
  eliminating birthday collision in the buffer by construction.
- **Lease authentication** — `n` is bound inside the MAC. A captured short-lease response cannot be
  presented as a long-lease one; the MAC fails for any other n. Home explicitly controls the lease
  granted; the robot does not need to search across n values.
- **Accidental false-accept odds** — robot checks K combinations against a 20-bit MAC:
  **K / 2²⁰ per received frame.** For K=8: ≈ 1-in-131,000.
- **Wrong-key rejection** — a unit without the correct key cannot produce a valid R.
- **Unauthenticated revoke** — stop/revoke carries no MAC by design. It can only drive toward OFF,
  which is always safe. This is a deliberate architectural constraint — do not add authentication
  to the revoke path.

## Not immune to
- **A malicious adversary** — key extraction, MITM, deliberate replay, brute-force forgery, and
  jamming are explicitly out of scope. (Recorded in [Safety Case](../04-safety-case.md) and
  [Threat Analysis](threat-analysis.md).)

## Interfaces
- **Out (to HW):** one watchdog kick per valid heartbeat; kick carries no data — HW enforces the
  lease ceiling independently via personality plug.
- **In (from UX):** the `n` selection (lease level), the arming gate, and the revoke/stop command.

## Open questions
- Send interval T, buffer depth K, and nonce `max_age` — all set by acoustic link round-trip
  characterisation. T ≈ lease/4; K ≈ round-trip/T; max_age covers worst-case round-trip with
  margin but rejects transport-layer buffering delays.
- Hardware ceiling on `n` (personality plug / pin-strap max value) — must be defined before
  deployment.
- Multi-UUV addressing — per-pair key derivation recommended:
  `key_i = KDF( master_key, UUV_serial )`.

## Entropy source (resolved)

**Prototype (Teensy 4.x / i.MX RT1062):** the chip has a hardware TRNG peripheral based on
free-running ring oscillators (FIPS 140-2 tested, independent of the main clock). Access via the
`TRNG_ENT0`–`ENT15` registers; poll `TRNG_MCTL_ENT_VAL` before first read.

**Production MCU:** hardware TRNG is a hard selection criterion. Standard on STM32 (F4/L4/H7/U5),
Nordic nRF52/53, NXP i.MX RT / LPC55S.

**Pattern on both:**
1. At boot, block until TRNG ready flag is set (typically < 10 ms).
2. Collect 32 bytes from TRNG; seed a ChaCha20 or AES-CTR-DRBG CSPRNG.
3. Clear TRNG output from RAM after seeding.
4. Generate all nonces from the CSPRNG (fast; forward-secure).
5. On long missions, reseed from TRNG periodically (e.g., every hour).

**After reset:** soft reset (RAM preserved) — CSPRNG state survives, continues immediately.
Hard reset / power cycle — block nonce generation until TRNG is ready and CSPRNG is reseeded.
Never seed from a fixed value (counter, zero, compile-time constant).
