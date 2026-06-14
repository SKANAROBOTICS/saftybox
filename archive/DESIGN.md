# UUV Secondary Safety System — Requirements, UX & Principles of Implementation

**Status:** Definition / pre-implementation
**Date:** 2026-06-09

---

## 1. Context & Purpose

This is a **secondary, independent safety system** for a UUV, layered *on top of* PX4. PX4
remains the primary flight/dive stack; this system does one thing and does it simply:

> **Guarantee that motor power is removed if authorized communication with "home" lapses.**

It must be simple enough to fully audit, and independent of the complex primary stack (PX4 +
ground-control PC), since those are exactly the things that can fail or hang.

**Topology:** two near-identical PCBs running the same firmware base, configured by role:
- **UUV unit** — drives the motor-power relays, generates heartbeat challenges.
- **Home unit** — mirror of the UUV PCB, holds the key, answers challenges, exposes operator
  controls (ARM switch, ABORT button, LEDs). Optional software companion for logging/config only
  — never in the safety path.

The home authority lives in dedicated hardware, **not** on the GCS PC, to preserve independence.

---

## 2. The One Safety Invariant

> Motor power is delivered **only** while a valid, unexpired **authorization lease** is held.
> The default state is **OFF**.

Everything below serves this invariant. The MCU must *actively and continuously* hold the relays
closed; any loss (power, crash, hang, lease expiry) converges to motors-off.

---

## 3. Operating Modes — a single "lease" primitive

Both requested modes are the same mechanism with one parameter: **each valid heartbeat extends an
`authorized-until` timestamp by `lease(mode)`.**

| `mode` | Lease (UUV-stored) | Behavior |
|--------|--------------------|----------|
| 0 — **AUTO** | **3 s** | Home renews ~every **1 s**. Comms drop → lease expires in seconds → motors cut. For live-comms operation. |
| 1 — **SINGLE-SHOT short** | **2 min** | One heartbeat grants the autonomy window; link may then go fully dark. Home can **abort early** if comms return. |
| 2 — **SINGLE-SHOT med** | **5 min** | as above |
| 3 — **SINGLE-SHOT long** | **10 min** | as above |

One state machine, one expiry timer → minimal, auditable firmware.

### 3.1 Lease ownership & configuration (safety-critical)

- **The UUV owns the durations; the packet only selects among them.** A heartbeat **never carries a
  duration value** — it carries the `mode` index, which the UUV maps to its own stored seconds. This
  keeps payload tiny and keeps the safety ceiling owned by the device that cuts power.
- The `mode` index is **bound into the MAC** (`MAC(ver‖mode‖nonce)`), so a captured short-lease
  response cannot be replayed to obtain a longer lease.
- **Absolute hard cap** (e.g. **15 min**) compiled in firmware clamps every lease regardless of
  catalog — a misconfiguration backstop.
- **Provisioning = build-time constants.** Catalog + cap live in the certified firmware image;
  changing a limit means a version-controlled rebuild, re-flash, and re-verification — never a field
  tweak, never over the operational link.
- Both mirror PCBs are built from the **same catalog**, so the home OLED countdown matches UUV
  enforcement. If they ever disagree, the **UUV's value is authoritative** (it cuts power) → errs safe.

---

## 4. State Machine

- **DISARMED** (power-on default): relays open, motors off.
- **AUTHORIZED**: lease valid → relays closed → motors enabled.
- **TRIPPED (latched)**: lease expired *or* any fault → relays open. **Sticky.**

**Re-arm policy: latched.** After a trip, motors stay off until the operator **deliberately
re-arms** at home — twist the mushroom out, **re-cock** the controls (cycle AUTO OFF→ON or press
SINGLE), *and* a fresh successful challenge-response. Releasing the mushroom alone never arms.
Prevents a drifting vehicle from surprise-restarting when comms flicker back. (Full flow in §7.)

**Disable/revoke** is honored immediately and is **unauthenticated** — it can only drive toward
OFF, which is always safe. This satisfies "if comms exist I can disable the UUV."

---

## 5. Heartbeat Protocol (challenge-response)

Replay immunity comes from **freshness**, not from strong crypto:

1. UUV generates a fresh random **nonce `N`** (2–4 bytes).
2. **UUV → Home** (UDP): `{ver, type=CHALLENGE, N}`.
3. Home computes `R = truncate_2B( MAC_key( ver ‖ mode ‖ N ) )`.
4. **Home → UUV** (UDP): `{ver, type=RESPONSE, mode, R}`.
5. UUV recomputes `R` and compares **constant-time**. Match → extend lease by `lease(mode)`.
   Mismatch/timeout → no extension.

**Design notes**
- **Mode is bound into the MAC** so a captured automatic-mode response can't be reused to grant a
  long single-shot lease.
- **Transport:** independent UDP, decoupled from MAVLink/PX4. Security-relevant payload is the
  **2-byte MAC**; with framing a packet is only a few bytes — well within "extremely low bandwidth."
- **Crypto:** symmetric truncated MAC (HMAC-SHA256 or AES-CMAC, truncated to 2 B). Shared key on
  both units.
- **Threat model (as specified):** immune to **accidental** triggering and **record/playback** (a
  recorded `(N,R)` pair is useless next cycle because `N` changed). A 2-byte response gives
  ~1/65536 per-attempt guess odds against a *fresh* challenge → accidental success effectively
  impossible. **Not** resistant to a malicious actor who extracts the shared key — out of scope by
  decision.
- **Nonce entropy:** hardware RNG if available; otherwise seed a CSPRNG from ADC/thermal/timer
  jitter at boot. Nonce must not repeat predictably within a mission.

---

## 6. Hardware (mirror PCB)

- **MCU + Ethernet.** Candidate: STM32 + external PHY, or an MCU + **W5500** hardware-UDP module
  (very simple, offloads the stack). To be chosen in implementation phase.
- **Relays:** two **normally-open** relays **in series** on the **24 V motor bus (~couple of A)**.
  Rated with margin (e.g. 5–10 A / 30 VDC) for inductive inrush. Flyback diode / snubber per relay.
  Series + NO ⇒ redundant against a single welded contact; de-energized ⇒ open ⇒ safe.
- **Independent drive paths:** relay A from a plain MCU GPIO; relay B gated through a **hardware
  watchdog / charge-pump** that requires continuous toggling — so a *hung* MCU output cannot hold
  both relays closed. Firmware must service the watchdog; if it stops, relays open.
- **Self-powered from 24 V.** Loss of box power ⇒ relays open ⇒ motors off.
- **Status LEDs:** power, link, comms-OK / last-response-OK, ARMED, TRIPPED.
- **Role config** (UUV vs HOME) via strap/jumper or stored config. Same firmware image.

---

## 7. UX — Home Unit Front Panel

### Panel layout

```
 ┌────────────────────────────────────────────────┐
 │  ⌐lid┐ ◀── change         ┌──────────────────┐   │
 │  CHANGE-LEASE             │  LEASE    05:00  │   │
 │  (tap=cycle, hold=lock)   │  REMAINING 04:32  │   │
 │                           └──────────────────┘   │
 │                                                   │
 │               ╔══════════════╗                    │
 │               ║   MUSHROOM    ║   press = UN-ARM   │
 │               ╚══════════════╝                    │
 │                                                   │
 │   🔑 KEY          ⌐lid┐ SINGLE      ⌐lid┐ AUTO      │
 │   (master)       (momentary)       (latched)      │
 └────────────────────────────────────────────────┘
```

### Controls
- **Master KEY switch** — enables the whole box. Key out ⇒ nothing can arm; an unattended box is
  inert. Operator-authority / "certified home" token (distinct from the crypto MAC key — this is
  *operator* authority, that is *machine* identity).
- **MUSHROOM (un-arm)** — press ⇒ immediately disarm. It **stops generating heartbeats** (lease
  expires → motors cut, always works) **and sprays repeated DISABLE** (instant cut if link up).
  Latching: twisting it out only **clears the latch — it never arms** (see re-arm flow). *Worst
  case:* in single-shot with the link down nothing reaches the robot, so the lease cap is the real
  time-to-stop — pick it conservatively.
- **SINGLE** — lid-protected **momentary**. Issues one single-shot lease of the current `LEASE`
  duration. Pressing again while armed re-issues the **same** window (refresh); duration can't change
  while armed.
- **AUTO** — lid-protected **latched**. ON ⇒ continuous heartbeat (rolling short lease); OFF ⇒ stop.
- **Lids are flip-up guarded toggles ("missile switch") — closing the cover forces the toggle to
  OFF.** So slamming the AUTO cover shut is itself a fast disarm, no fine motor control needed.
- **CHANGE-LEASE** — lid-protected **momentary, beside the screen, disarmed-only**. Tap cycles
  `LEASE` through the allowed catalog; on release the `LEASE` value **blinks 5 s as pending**;
  **long-press to lock**; if not locked in time it reverts. No double-entry needed.
- **No single action arms**, and **arming is the guarded direction**: key present + lift a lid +
  actuate, then a fresh challenge-response must succeed before relays close (can't arm into a dead
  link). Un-arm is instant and unguarded (safe direction).

### Mode behavior
- Both AUTO and SINGLE feed **one lease timer**; the **longest expiry wins** — no conflict if both
  are active (a SINGLE 5 min dominates AUTO's rolling 3 s, and AUTO holds it after).
- **AUTO:** lid + latch ON ⇒ box runs the challenge-response loop continuously; the robot's lease
  timeout is the dead-man. Latch OFF or mushroom stops it.
- **SINGLE:** lid + momentary press ⇒ one lease of `LEASE`; `REMAINING` counts it down; link may
  then drop.

### Screen (timers only)
- Two lines only: **`LEASE`** (lease that will be granted next) and **`REMAINING`** (live countdown
  on the running lease). Everything else (mode, link, armed, robot status) is on the LEDs.
- `LEASE` blinks while a change is pending (unlocked).

### Status LEDs (carry everything off the screen)
- POWER, FAULT, **ROBOT STATUS** (RGB), **LINK**, **ARMING**.
- **ROBOT STATUS rule (safety-critical):** *stale must look different from live.*
  - Solid green = link live + robot armed/running
  - Solid red = link live + robot tripped/motors off
  - **Blinking** = link lost, showing **last-known** (stale) state
  - Off = no data yet / box off
- Robot's actual relay state + trip-reason ride back in a **1-byte status field** the UUV piggybacks
  on its challenge packet (free in AUTO; absent in single-shot-link-down → shown stale).

### Audible alarm
- **Buzzer** with distinct tones for: **comms-loss** (expected heartbeats stopped), **lease-low**
  (last ~30 s of a single-shot window), and **un-arm pending** (mushroom pressed but disable not yet
  confirmed because the link is down).

### Re-arm flow (latched — "release ≠ arm")
After a mushroom press or any trip, the system stays **DISARMED even if the AUTO latch is still
physically ON**. To resume you must **re-cock**: twist the mushroom out, then cycle AUTO OFF→ON (or
press SINGLE again), and a **fresh successful challenge-response** must complete before relays close.
Nothing auto-restarts.

### UUV unit
- LEDs mirror state; latched TRIP indicator with reason.
- **Re-arm only from home** (explicit), per the latched policy.

---

## 8. Configurable Parameters (with safe defaults / caps)

| Param | Default | Notes |
|-------|---------|-------|
| lease catalog (per `mode`) | 3 s / 2 / 5 / 10 min | **build-time constants** (§3.1); change = rebuild + re-flash |
| heartbeat interval | 1 s | auto mode |
| absolute hard cap | 15 min | firmware clamp on any lease |
| UDP ports / peer IP | — | static config |
| shared key | — | compiled in; inject at flash time for per-pair keys (§10) |
| watchdog window | — | tune to firmware loop |
| lease-low warning | 30 s | buzzer + OLED before single-shot expiry |
| comms-loss detect | ~2× interval | declare LINK LOST → blink ROBOT STATUS, buzzer |

---

## 9. Failure-Mode Sketch (FMEA)

| Event | Result | Safe? |
|-------|--------|-------|
| MCU crash/hang | watchdog not serviced → relays open | ✅ |
| Box power loss | relays open | ✅ |
| Cable cut / link down | lease expires → relays open | ✅ |
| Relay A contact welded | relay B still opens (series) | ✅ |
| Recorded packet replayed | stale nonce → rejected | ✅ |
| Home PC compromised | out of scope; key is in the box, not the PC, limiting exposure | ⚠️ by design |

---

## 10. Key Management

- Per-pair (or per-fleet) shared key, provisioned at manufacture.
- Lease catalog + cap are build-time constants, but **keep the key separable**: inject it at
  **flash time** (e.g. a fixed flash slot written per unit) so a per-pair key doesn't require a
  per-pair firmware rebuild.
- Store in MCU flash; enable read-protect where available (noting this is not malicious-proof,
  consistent with the threat model).
- Define a key-rotation / re-provisioning procedure.

---

## 11. Open Items (to confirm before/at implementation)

- MCU + Ethernet approach (W5500 module vs integrated MAC).
- Confirm default lease values and the single-shot hard cap.
- Single-shot grants: refreshable / stackable? (default: each GRANT resets the window, capped.)
- Any external certification scope (class society / internal safety case)?
- Marine environmental & EMC requirements (sealing, conformal coat, temperature).

---

## 12. Implement vs. Outsource (for decision)

- **Firmware + protocol:** small, safety-critical, and the core IP — **recommend in-house** so it
  stays fully auditable.
- **PCB layout, relay/watchdog drive, EMC and marine environmental qualification:** standard but
  specialized — **candidates to outsource** if internal bandwidth is limited.

A pragmatic split: own the firmware/protocol and the safety architecture; partner on hardware
layout and environmental/EMC qualification.
