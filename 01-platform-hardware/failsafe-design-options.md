# Failsafe Design Options — Watchdog OK & Timer OK

> Pillar 1 of 3 · handle **Safe state** · [↑ overview](../README.md) · [← README](README.md)

## Status: two candidate designs — decision pending

There are two gates in the fail-safe chain that this document is about:
- **Watchdog OK** — fast, seconds-scale: proves the CPU is alive and executing correctly, right now.
- **Timer OK** — slow, hours-scale: bounds how long the platform may run without a fresh
  heartbeat (the lease ceiling).

There are **two candidate ways to implement both gates**, and this document exists specifically
so any future conversation, commit, or schematic is unambiguous about **which one** it's talking
about:

| | Design A — Teensy-only | Design B — External supervisor chips |
|---|---|---|
| Status | Actively explored, partially bench-verified | Original block-diagram design; **TBD, not built this round** |
| Watchdog OK | RTWDOG (i.MX RT1062 internal) | TPS3850-Q1 (external, separate power domain) |
| Timer OK | GPT1 (i.MX RT1062 internal) | PCA2131 RTC (external, SPI) |
| Survives MCU brownout / die-level fault | No | Yes |
| Extra BOM / complexity | None (uses what's already on the Teensy) | TPS3850, PCA2131, associated passives |

**Neither design is "decided."** Design A is what the last several messages in this project's
working session actually built and reasoned through in depth; Design B is what the existing
[block diagram](assets/Stringray_Fail-Safe_Box_Block_Diagram.png) already drew before that
session started. They are alternatives, not layers on top of each other — see
[How to decide](#how-to-decide-between-them) below.

---

## Design A — Teensy-only

Everything in this design runs on the i.MX RT1062 already on the Teensy 4.1 — no additional
parts. It's built from three layers plus one protocol-level addition.

### Layer 1 — passive default (pull resistors)

Two GPIO outputs, one per redundant relay channel, each with an **external pull resistor toward
its own safe level**:
- Channel A: pull-**up** → safe = HIGH.
- Channel B: pull-**down** → safe = LOW.

This is the unconditional baseline: whenever a pin is *not* being actively driven (power-on, boot,
after any reset — including a Layer-3 reset, see below), the resistor wins and the channel is
safe with zero software required. It does **not** by itself protect against a CPU that already
drove a pin to "enable" and then hangs while still holding that level — an active GPIO driver
overpowers a passive pull resistor regardless of what the CPU is doing. That gap is closed by
Layers 2 and 3, not by the resistor.

Using opposite conventions on the two channels also buys real redundancy against **common-mode**
faults: anything that pushes GPIO uniformly in one direction (a brownout resetting a whole port, a
bug that zeroes or fills a shared register) can only ever push toward one channel's safe state,
never both — and since the two relays are wired in series, either one going safe is enough for the
whole system.

### Layer 2 — Timer OK (lease ceiling)

One hardware timer per channel, each with an ISR that does exactly one `digitalWrite` to force
that channel's safe level. **Both pins stay in plain GPIO mode the entire time** — this is a
deliberate simplification versus an earlier attempt at a "pure hardware, zero-ISR" pin action via
GPT's Output Compare (`OM`) field, which is documented in detail, including the bugs found along
the way, in [timer-ok-bringup-plan.md](timer-ok-bringup-plan.md). That approach is **not** being
carried forward as the current plan — plain GPIO + timer-triggered ISR is simpler, avoids the
IOMUX/`SION` readback issues that approach ran into, and is already bench-proven reliable (after
finding and fixing a real ISR double-fire bug — also logged in that document).

- Needs an hours-scale range (up to 2^15 s ≈ 9.1h, per `protocol_defs.h`'s `N_MAX`). Teensy's
  PIT/`IntervalTimer` tops out around 90–180s, nowhere close — GPT1's 32-bit counter at its
  32kHz reference clock reaches ~36h, which does cover it. Worth checking whether a ready-made
  library (e.g. `TeensyTimerTool`) exposes GPT's range through a plain callback API, avoiding any
  hand-rolled register work — not yet verified.
- Re-armed (fresh target, counter reset) on every valid heartbat grant.
- On a real detected expiry, the timer is also explicitly stopped, so it can't drift into a
  same-target repeat many hours later on rollover — belt-and-suspenders on top of that repeat
  already being harmless by construction (it can only ever re-assert the same safe state).

### Layer 3 — Watchdog OK (hang backstop)

RTWDOG (i.MX RT1062's dedicated windowed watchdog), kicked from the main loop — but
**conditionally**, not unconditionally:

- The main loop independently tracks, via `millis()`, what each channel's state *should* be
  right now (when it was last armed, for how long).
- Each iteration, that expectation is compared against the actual `digitalRead()` of both pins
  (trivially reliable here, since these pins never leave GPIO mode under this design).
- The RTWDOG kick only happens if they match. A loop that's still running but producing wrong
  results — not just a fully dead loop — withholds the kick and lets RTWDOG trip.
- RTWDOG's own range is capped hard at ~2 seconds (16-bit counter, fixed 32kHz clock, no
  prescaler — confirmed against the reference manual, not a tunable limitation) — which is
  correct for this gate's job: catch a hang in seconds, not hours.
- On trip: **full chip reset**. This is the piece that actually lets Layer 1's pull resistors
  take back over regardless of what Layers 1–2 were doing at the instant of the hang — a reset
  returns GPIO to Hi-Z, only then do the resistors win.

### Protocol-level addition: boot/reset revoke-gate

A gap specific to combining Layer 3 with the existing heartbeat protocol: if home is still
transmitting an active (`n>0`) lease at the exact moment a hang-induced reset happens, the
freshly-booted firmware could validate the very next heartbeat and re-arm within about a second —
silently, with no operator awareness a reset ever occurred.

Fix (not yet implemented): on every boot — including post-reset boots — `PlatformNode` must see
an explicit `n=0` (revoke) from home at least once before it will accept *any* `n>0` grant, even
a cryptographically valid one. This forces a genuine stop-and-reconfirm after any reset instead of
an automatic silent resume. Pure software, no hardware dependency, testable against the existing
`simulator/tests/test_platform_node.cpp` suite without any board involved.

### Known limitations of Design A

- Everything above shares the same die, clock tree, and power rail as the CPU it's protecting
  against. A brownout or a die-level fault can take RTWDOG and GPT down with the rest of the
  chip — internal mechanisms can't self-report the failure of their own foundation.
- This is exactly the gap Design B's separate-power-domain external chips exist to close, and
  exactly why the existing `README.md` already says *"Don't trust the MCU's internal
  brownout... Teensy is for prototyping only."*

---

## Design B — External supervisor chips (TBD)

The original design, drawn in the current
[block diagram](assets/Stringray_Fail-Safe_Box_Block_Diagram.png) before this session's
Teensy-only exploration began:

- **Watchdog OK** ← TPS3850-Q1 watchdog/voltage supervisor, on its own power domain — see
  `datasheets/TPS3850-Q1_Watchdog-Supervisor.pdf` and `datasheets/review.md` ("Looks perfect for
  this application").
- **Timer OK** ← PCA2131 RTC over SPI, `INTA` feeding the gate directly — see
  `datasheets/PCA2131_RTC.pdf`.
- Both are genuinely separate silicon from the MCU, so a fault that takes down the Teensy (clock,
  brownout, a wild firmware bug) doesn't necessarily take these down too.

**This design has not been built or tested this session.** It's recapped here only so it isn't
lost or conflated with Design A — the block diagram and datasheets already contain what's known
about it. Nothing about Design A's exploration invalidates it; they're being evaluated as
alternatives.

---

## How to decide between them

This is the same build-vs-outsource question already framed in
[`../04-safety-case.md`](../04-safety-case.md): Design A is cheaper and simpler (zero extra BOM)
but inherits the MCU's own single-point failure modes; Design B costs more parts and complexity
but survives a wider class of faults, including ones that take the whole MCU down. Per this
project's stated integrity target ("informal high-integrity... reliability target: prevent
equipment loss," not a certified SIL/PL), Design A may be an acceptable, deliberate trade-off —
but that's a call to make explicitly, once Design A has real bench results, not by default because
it's what got explored first. Revisit after Layer 2/3 are actually implemented and tested on
hardware.
