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

- **Screen = two timers only:** **LEASE** (the window that will be granted next) and **REMAINING**
  (live countdown on the running lease). Everything else moves to LEDs.
- **LEDs:** POWER · LINK · ROBOT · ARMED · FAULT. **Safety rule: stale ≠ live** — last-known robot
  state must look different (blink) from a live reading.
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

## Interfaces
- **Out (to Protocol):** arming gate, `mode`/lease selection, revoke/stop.
- **In (from HW):** actual relay/link/fault state for the indicators (stale ≠ live).

## Open questions
- Cover color-coding (e.g. AUTO distinct from SINGLE / CHANGE).
- Momentary vs maintained switch under each guarded cover when sourcing.
- UUV-side indicators; optional software companion for logging.
