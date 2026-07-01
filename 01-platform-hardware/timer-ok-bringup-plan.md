# Timer OK — Teensy Internal-Timer Bring-Up

> Pillar 1 of 3 · handle **Safe state** · [↑ overview](../README.md) · [← README](README.md)

> **This is the detailed GPT register-level bring-up log for Design A** (Teensy-only) — see
> [failsafe-design-options.md](failsafe-design-options.md) for how Design A compares to the
> external-supervisor-chip alternative (Design B), and for the current overall architecture.
> **Note:** the current recommended approach within Design A has since simplified away from the
> pure-hardware `OM`-field pin action documented below, toward a plain-GPIO + timer-ISR pattern
> for both channels (simpler, avoids the IOMUX/`SION` issues found here) — this document is kept
> as-is because the bugs found and fixed along the way (the false-idle-level issue, the ISR
> double-fire issue) are directly relevant background, not because the `OM`-field approach itself
> is still the plan.

## Context

Each of the platform's two redundant 24V relay-cutoff channels is gated by three series enable
signals: Power OK, Watchdog OK, and Timer OK (see the
[block diagram](assets/Stringray_Fail-Safe_Box_Block_Diagram.png)). The block diagram currently
draws Timer OK as coming from an external PCA2131 RTC chip's `INTA` pin over SPI, shared
identically across both channels.

The software side already models this function in `simulator/lib/hal/platform_hal.h` as
`watchdogArm(windowMs)` / `watchdogKick()` — called by `PlatformNode::_enterArmed()`
(`simulator/lib/platform_node.cpp`) on **every** valid heartbeat grant, with
`windowMs = 2^n * 1000` where `n` (1–15, per `protocol_defs.h`) gives a lease from 2 seconds up to
32768 s ≈ 9.1 hours. This is the "single-shot autonomy window" / lease-ceiling timer — a one-shot,
re-armed-on-every-valid-heartbeat countdown, distinct from the fast CPU-liveness supervisor
(TPS3850) elsewhere in the design.

This document tracks bringing up that countdown using the Teensy 4.1's own internal hardware
timer peripheral (GPT) instead of the PCA2131, with two independent output channels — one per
redundant relay channel, using deliberately opposite pin conventions so a single systematic
config/firmware bug is less likely to defeat both channels identically.

**Whether this replaces the PCA2131 on the real platform PCB, or stays a validated alternative, is
deliberately deferred until bring-up results are in.** Status and open items are tracked at the
bottom of this document.

## Stage 1 — Minimal internal-timer firmware demo — **implemented, awaiting bench confirmation**

Code: `firmware/timer_test/` (PlatformIO project; `include/OneShotTimer.h`, `src/OneShotTimer.cpp`,
`src/main.cpp`).

**Bench test #1 result:** Ch1 idle level read correctly (LOW), but Ch2 idle read LOW instead of
the intended HIGH, and its "expiry" fired at the same millisecond as arming — a false positive.
Attempted fix #1 (priming the output latch via the `FOn` Force-Output bit) was applied but not
yet re-tested.

**Bench test #2 result (on a scope, not just `digitalRead()`):** pin 31 sat permanently HIGH and
pin 30 permanently LOW — never toggling at all. Two separate issues identified:
1. `digitalRead()` may not be trustworthy once a pad is muxed away from GPIO's own ALT function —
   confirmed against the RM's IOMUX chapter: the pad's `MUX_CTL` register has a `SION` (Software
   Input On) bit (bit 4) specifically for this ("LoopBack" use case); it wasn't being set.
2. More likely explanation for the scope result: `begin()` was switching the pad over to GPT1
   control (with the channel's real Set/Clear mode already selected) *before* `arm()` ever wrote
   a real compare value into `OCR2`/`OCR3`. If the output-compare action continuously reflects
   "has the counter reached the compare register" rather than latching once at a discrete event,
   an unprogrammed (near-zero) `OCR` reads as already-satisfied well before any real countdown,
   so the pin snaps to its "matched" level almost immediately.

**Fix applied, and Ch1 confirmed working** on bench test #3: `arm()` writes the real compare value
first, then hands the pad to GPT1 (with `SION` set) right before enabling the counter — closing
the window where an unprogrammed `OCR` could look already-matched. Ch1 (pin 31, Set mode) idles
LOW and transitions to HIGH correctly on a scope. This also confirms the peripheral's output
latch defaults LOW after reset — which is exactly why Ch1 (idle LOW) "just works" once the
sequencing is right, while Ch2 (pin 30, Clear mode, wants idle HIGH) stayed permanently LOW:
Clear's only action is to assert LOW, so nothing was ever driving the latch HIGH in the first
place — it was never going to reach HIGH regardless of sequencing.

**Bench test #4 result:** with the latch primed via `FOn` inside `arm()`, pin 30 started
oscillating rapidly (~30µs period — one tick period at 32768 Hz) instead of holding a clean idle
level. Cause: `arm()` runs per-channel, sequentially (`ch1.arm()` then `ch2.arm()` in the test
sketch), and both channels share one `EN` bit — so by the time `ch2.arm()` ran its `Set → Force →
Clear` priming dance, `ch1.arm()` had already set `EN=1` and started the shared counter. The
priming was racing a live, ticking counter instead of a stopped one.

**Fix applied (not yet bench-confirmed):** moved Ch2's priming from `arm()` into `begin()`, where
`EN` is guaranteed still 0 for both channels (`begin()` always runs before either channel's
`arm()` in the test sketch's `setup()`) — so the prime now happens on a fully stopped counter,
with the pad still on plain GPIO (invisible to the outside) the whole time. `arm()` no longer
touches `OM`/`FOn` at all; it only writes the real compare value and hands the pad to GPT1.
Register-dump serial prints (`GPT1_CR/SR/CNT/OCR2/OCR3`) remain in place for further diagnosis.

**Pins — confirmed against the i.MX RT1062 reference manual, not guessed.** Cross-referencing the
RM's IOMUX tables against Teensyduino's `core_pins.h` shows GPT1_COMPARE2 is the only GPT signal
wired to a Teensy 4.1 header pin at **pin 31**, and GPT1_COMPARE3 at **pin 30**. GPT1_COMPARE1 and
all three of GPT2's compare outputs exist in silicon but are not routed to any Teensy 4.1 pin — so
only two GPT output pins are available at all, and both are channels of the *same* GPT1 module,
not two independent peripheral instances (see Known limitation below).

**Mechanism — genuinely hardware-only, not ISR-mediated.** GPT1's 32-bit counter, clocked from the
32kHz low-reference clock (`ipg_clk_32k`, `CLKSRC=100b`) with no prescale, reaches ~36 hours in one
shot — comfortably past the 9.1h (2^15 s) lease ceiling. At the programmed compare value, the GPT
hardware itself forces the pin; no interrupt, no ISR, no CPU involvement in the pin action:

- **Ch1 = pin 31** (GPT1 Output Compare 2, `OM2` = **Set**, `011b`) — idle LOW, forced HIGH at
  expiry.
- **Ch2 = pin 30** (GPT1 Output Compare 3, `OM3` = **Clear**, `010b`) — idle HIGH, forced LOW at
  expiry.
- Set/Clear (not Toggle) — one-directional, deterministic regardless of prior pin state, correct
  for a fail-safe.

**Known limitation, carried forward for later stages.** Both channels share GPT1's single counter
and `EN` bit, so they don't have independent timebases — arming starts one shared countdown, not
two truly separate ones. Fine for this bring-up test (both channels are armed together with the
same duration), but weaker channel-to-channel diversity than two separate peripherals would give.
GPT2's compare outputs aren't available on any Teensy 4.1 pin, so true cross-module GPT diversity
isn't achievable this way. Worth revisiting for the flight design (candidate: QuadTimer as a
genuinely separate second peripheral — its OFLAG pin routing is also confirmed, on pins 10/11/12,
but its 16-bit counter can't reach hours without cascading multiple channels, unlike GPT's 32-bit
one).

**Scope of this first version.** Arm-once only — no re-arm/kick semantics yet (deliberately
scoped out; see Stage 1 Step B below). `OM` is one-directional (Set-only or Clear-only), so
re-arming isn't just "call `arm()` again" — it needs the pin walked back to idle first, which
this version doesn't attempt.

**How the test program works:**
1. `begin(pin)` on each channel resets GPT1 once (guarded so the second channel's `begin()`
   doesn't clobber the first channel's config), configures the shared 32kHz clock + free-run +
   reset-on-enable, sets that channel's `OM` field, drives the pin to its idle level via plain
   GPIO, then hands the pad to GPT1 via the IOMUX mux-control register (`CORE_PIN31_CONFIG = 2` /
   `CORE_PIN30_CONFIG = 2`, both ALT2).
2. `arm(durationMs)` loads that channel's compare register (`OCR2`/`OCR3`) and starts the shared
   counter (`EN=1`) if not already running. Both channels are armed with the same 5-second test
   duration back-to-back in `setup()`.
3. `loop()` polls `digitalRead()` on each pin directly (no ISR, no library-side status flag) and
   serial-prints the moment each transitions.

Serial convention:
```
[Ch1] idle pin31=LOW (expect LOW)
[Ch2] idle pin30=HIGH (expect HIGH)
[Ch1] ARM t=12345 window_ms=5000
[Ch2] ARM t=12345 window_ms=5000
[Ch1] pin31=HIGH t=17402  <- expiry detected
[Ch2] pin30=LOW  t=17405  <- expiry detected
```
Onboard LED blinks while waiting, solid once both channels have reported.

### Step B — re-arm / kick semantics (not started — do after Step A is bench-confirmed)

- `watchdogKick()` can likely be a lightweight liveness confirmation (LED blink + serial line),
  not real hardware reprogramming — every call site of `watchdogArm()` in `platform_node.cpp` is
  immediately followed by `watchdogKick()` in the same function (`_enterArmed()`); there's no path
  that kicks without re-arming, or arms without kicking. All real hardware work belongs in
  `watchdogArm()`.
- Re-arm sequence needs: stop, reset, reprogram the compare register, **explicitly walk the pin
  back to its idle level** (since `OM` is one-directional), then restart — in that order, so a
  re-arm never produces a spurious transition.
- Needs a long-duration + re-arm proof (arm ~60s, re-arm every ~10s, confirm no early transition,
  confirm the final unkicked run honors the *new* full window) and a cadence stress test matching
  real AUTO-mode timing (`protocol_defs.h`: `N_AUTO=2` → 4s lease, `T_CHALLENGE_MS=500`).

## Stage 1b — Bring-up board schematic (designed, not yet built)

**One ULN2803A chip** (8-channel NPN Darlington, DIP-18, open-collector, ~2.7kΩ internal series
input R, internal flyback diodes to a common COM pin) — 3 of its 8 channels used: 1 for Channel 1,
2 cascaded for Channel 2.

**Channel 1 (direct):**
`Teensy pin 31 —[10kΩ pulldown to GND]— ULN IN1 → OUT1 (open collector) — relay #1 coil (low side)`;
coil high side → **+5V**. Boot/Hi-Z → pulldown holds input LOW → de-energized (safe). Firmware
HIGH → energized. Firmware LOW → de-energized.

**Channel 2 (cascaded double-Darlington):**
`Teensy pin 30 —[10kΩ pullup to 3.3V]— ULN IN_A → OUT_A —[10kΩ pullup, recommend to +5V]— ULN IN_B → OUT_B (open collector) — relay #2 coil (low side)`;
coil high side → +5V. Boot/Hi-Z → pullup holds IN_A HIGH → Darlington A on → sinks IN_B low →
Darlington B off → de-energized (safe, matches channel 1). Firmware HIGH → same as boot →
de-energized. Firmware LOW → A off → intermediate node pulled high → B on → energized. **3.3V is
required (not 5V) on the first pullup** since that node ties directly to a 3.3V-logic Teensy pin.

Both truth tables independently verified.

**Resistor values:** 10kΩ for all three pulls (matches the project's existing 10k-pulldown
convention; comfortably inside the Teensy's ~10mA/pin drive budget since each pull is a parallel
bleed path, not a series fight against the internal 2.7kΩ; pulls straight from the resistor kit
already on hand).

**Critical wiring point — flyback/COM:** the ULN2803A's **COM (pin 9) must tie to the +5V
relay-coil rail** — this is what gives the chip's internal flyback diodes a defined clamp
reference against the coil's turn-off inductive spike. Tying COM to GND is actively wrong (creates
a continuous leakage path whenever an output floats toward +5V); floating COM gives no protection
at all. Highest-risk, least-obviously-testable-by-static-checks wiring mistake on this board — it's
invisible until the first relay-off transient.

**No pin conflicts:** the Ethernet kit (DEV-18615) solders onto a separate, dedicated onboard PHY
footprint (not the numbered GPIO header); built-in LED is fixed pin 13; USB serial uses the
separate USB connector. None collide with pins 30/31.

**Other flagged items:** ULN2803A's DIP-18 pin numbering wraps around the package (IN1/OUT1 are
opposite each other, not adjacent) — check the pinout each time rather than from memory. Use the
already-ordered 5.08mm terminal blocks for coil/power connections rather than direct-solder, for
rework-ability during bring-up. Firmware must never put pins 30/31 into
`INPUT_PULLUP`/`INPUT_PULLDOWN` mode, even transiently for readback — the safety argument depends
on the *external* resistor network being the sole authority over the boot/Hi-Z state.

## Stage 2 — Build the board and test it end-to-end (not started)

1. **Unpowered:** multimeter-verify all three resistor values; continuity-check every net,
   especially the chip's COM→+5V and GND connections; confirm no 5V-to-GND short before applying
   power.
2. **Powered, no firmware loaded:** confirm both relays read de-energized — validates the
   boot-safe claim in hardware alone, before any firmware exists to blame or credit.
3. **Minimal manual-toggle sketch:** command pin 31 and pin 30 independently HIGH/LOW via serial;
   confirm each relay's energize direction matches its truth table, and confirm no cross-talk
   between the two channels.
4. Re-run Stage 1's firmware (Step A, then Step B) against the real board.
5. Exercise the Ethernet kit (QNEthernet or NativeEthernet, per `simulator/lib/hal/home_hal.h`'s
   comment) and the built-in LED/serial debug conventions together.

## Stage 3 — Merge into the simulator architecture / working platform PCB (not started)

Implement `TeensyPlatformHAL : public PlatformHAL` (new files: `teensy_platform_hal.h/.cpp` + a
`.ino`), covering every method in `simulator/lib/hal/platform_hal.h`: `nowMs()` → `millis()`;
`randomU32()` → i.MX RT1062 hardware TRNG; `relaySet()`/`relayReadback()` → digitalWrite/Read;
`performBIT()` → the blocking sequence already sketched as comments in
`simulator/platform_sim/sim_platform_hal.cpp` (needs a new `BIT_WINDOW_MS` constant in
`protocol_defs.h`); `watchdogArm()`/`watchdogKick()` → the GPT1 mechanism above;
`sendPacket()` → UDP via QNEthernet/NativeEthernet. Copy
`simulator/lib/{packet,mac,nonce_buf,platform_node}.cpp/.h` and `protocol_defs.h` verbatim into the
sketch folder, per `simulator/CMakeLists.txt`'s own comment that these are already
Arduino-portable.

## Open items / follow-ups

- **PCA2131 decision.** Deferred until Stage 1–2 bring-up results are in. Once decided, update
  [README.md](README.md)'s "Open questions" (timer-chip family) and the block diagram.
- **Relay polarity mapping.** The bring-up board's relays are a validation indicator only — which
  absolute energize state must mean "motors allowed" is a Stage 3 decision, once it's clear
  whether this exact circuit becomes the real Q3-equivalent gate driver or feeds a differently
  shaped final gate.
- **Diversity trade-off.** Both available GPT compare pins share one GPT1 module (see Stage 1). If
  the flight design wants genuinely independent per-channel timebases, revisit using QuadTimer
  (pins 10/11/12 confirmed) as a second, separate peripheral, accepting its cascading complexity.
- **Personality plug (hardware lease ceiling).** Not addressed by this work. A firmware-un-alterable
  cap on the maximum lease exponent `n` a (possibly buggy) firmware could request is a separate,
  still-open, must-resolve item per [`../02-protocol/threat-analysis.md`](../02-protocol/threat-analysis.md).
  This work makes the per-lease renewable timer function correctly; it does not by itself provide
  that independent ceiling.
