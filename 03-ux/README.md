# Operator Will — Safe Command & Control (UX)

> Pillar 3 of 3 · handle **Intent** · [↑ overview](../README.md)

## Purpose
Let the operator **express and transfer their intent** — arm, set the autonomy window, stop — to the
system reliably and unambiguously.

## Main challenge
Capture intent without error: **deliberate to enable, effortless to stop, impossible to trigger by
accident.**

## Approach / key decisions
Front-panel concept: [`panel-sketch.html`](panel-sketch.html).

- **Screen = three rows:** **LEASE** (the window that will be granted next), **REMAINING** (live
  countdown on the running lease), and **ROBOT** (the robot's confirmed lease level). See below.
- **LEDs:** POWER · LINK · ROBOT · ARMED · FAULT. **Safety rule: stale ≠ live** — last-known robot
  state must look different (blink) from a live reading. ROBOT LED adds a sync dimension (see
  below).
- **Controls:**
  - **Master KEY** — enables the whole box; no key, nothing arms (operator-authority token).
  - **Mushroom UN-ARM** — latching; press cuts power; **releasing it never arms** (clears the latch
    only). It also stops heartbeats *and* sprays disable.
  - **SINGLE** — guarded ("missile") momentary toggle: grants one autonomy window of the LEASE value.
  - **AUTO** — guarded **latched** toggle: continuous heartbeat while on; **cover-close = OFF**.
  - **CHANGE-LEASE** — guarded, **disarmed-only**, beside the screen: flick to cycle the value, the
    LEASE field **blinks 5 s pending**, **hold to lock**, else reverts.
- **Arming is a multi-gate sequence**, never one action: key present + lift a lid + deliberate commit
  + a **fresh successful challenge-response** before relays close. Arming is guarded (dangerous
  direction); un-arm is instant and unguarded (safe direction).
- **Use-error prevention:** lease length changes only while disarmed (can't lengthen a running
  window); latched trip → **re-cock** required (release mushroom, cycle the switch, fresh exchange).
- **E-stop reality:** in single-shot with the link down, nothing reaches the robot — the **lease cap
  is the real time-to-stop**. Buzzer signals comms-loss / lease-low / un-arm-pending.
- **Mirror PCB:** the home unit is the UUV board configured as "home."

## Screen display

```
LEASE      512 s   n=9
REMAINING  04:32
  ROBOT    n=9 ✓
```

**LEASE** — the n value the home will grant on the next valid challenge. Set by the PROGRAM key
(×2 / ÷2); range n = 1–15. Blinks 5 s while a change is pending (unlocked), then locks.
n = 0 is never selectable via PROGRAM; it is the stop value emitted by revoke actions only.

**REMAINING** — an upper-bound countdown satisfying `displayed ≥ actual` at all times.

The robot starts its countdown only after the acoustic delay Δ, not when the home sends. The
nonce `max_age` bounds Δ, so adding it to the lease gives a safe upper bound:

```
display = (2^n_last_sent + max_age) − elapsed_since_send
```

The maximum overestimate is `max_age` — negligible for leases ≥ 64 s.

- *Reset (up)* — when home sends a new grant (any n ≥ current), restart from `2^n + max_age`.
  In AUTO mode this resets on every cycle so the display stays near the lease ceiling.
- *Hold on stop/decrease* — when home sends n = 0 (mushroom) or a lower n, **do not decrease
  the countdown**. The robot may still hold its old lease. Keep the running timer until the robot
  echoes n = 0 or the timer reaches zero on its own.
- *STOPPED* — when the robot echoes n = 0, jump immediately to `STOPPED`. This is the only
  downward event driven by a robot echo rather than elapsed time.
- *Stale* — no challenge received in >3×T: display blinks (last-known value, stale ≠ live).

**ROBOT** — the echoed n from the robot's last challenge, with a sync indicator:
- `n=X ✓` (green) — robot echoes n matching home's last grant; in sync.
- `n=X ⚠` (amber) — robot echoes a lower n; last grant is in flight (acoustic RTT in progress).
  Expected briefly after every SINGLE press or n change. If amber persists past
  `max_age + round-trip`, the grant was probably lost.
- `n=0` (red) — robot reports no active lease; stopped or watchdog fired.
- `--- ` — no challenge received yet.

## ROBOT LED sync states

The ROBOT LED carries both liveness (solid = live, blink = stale) and sync state (color):

| Color | Solid (live) | Blinking (stale, >3×T) |
|-------|-------------|----------------------|
| Green | Robot echoes n = home's last grant (in sync) | Same, but link gone stale |
| Amber | Robot echoes lower n (grant in flight) | Grant in flight, link gone stale |
| Red | Robot echoes n = 0 (stopped) | Last known: stopped |
| Off | No challenge received since boot | — |

Amber → green is the normal confirmation arc after every successful grant. If amber → stale-amber
(blink), the grant was lost; home should re-send on the next fresh challenge.

## LINK LED and challenge freshness

The nonce `max_age` bounds the acoustic delay and feeds the REMAINING upper-bound formula — but
it is still not a per-challenge display element. The LINK LED expresses link health at the
operator's resolution:

- **Solid blue** — challenge received within the last T seconds; a fresh response can be sent now.
- **Slow blink** — last challenge was 1–3×T ago; link alive but no fresh challenge in hand.
- **Off** — no challenge for >3×T; link degraded or lost.

The challenge window is not shown as a countdown. In SINGLE mode, the confirmation arc (LINK
solid → home responds → ROBOT turns green) gives the operator all the feedback they need without
exposing the raw 5-second timer.

## Display stability (anti-flutter)

The challenge cycle fires every T seconds. Without damping, every packet would flicker the display.

- **REMAINING**: driven by the send event (1-second tick from `T_last_send`), not by packet
  receipt. Resets only on a new higher-n send. No per-packet updates; no correction snaps.
  The only echo-driven event is n=0 → STOPPED.
- **ROBOT LED**: green→amber only when a new grant has just been sent and is unconfirmed.
  amber→green only when the echoed n catches up. Identical consecutive packets cause no transition.
- **LINK LED**: updated on a 1-second timer, not on every packet arrival.

## Interfaces
- **Out (to Protocol):** arming gate, `mode`/lease selection, revoke/stop.
- **In (from HW):** actual relay/link/fault state for the indicators (stale ≠ live).

## Open questions
- Cover color-coding (e.g. AUTO distinct from SINGLE / CHANGE).
- Momentary vs maintained switch under each guarded cover when sourcing.
- UUV-side indicators; optional software companion for logging.
