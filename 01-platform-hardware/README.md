# Fail-Safe Actuation — Platform Hardware

> Pillar 1 of 3 · handle **Safe state** · [↑ overview](../README.md)

## Purpose
Physically deliver or remove **24V relay control power** (≈ upto a couple of amps). The default, power-on, and
fault state is **OFF**. This is in series to the key shortcut plug. 24V->Key Control Plug->SafetyBox-> key relay -> 48V key bus. 

## Main challenge
**Converge to motors-off under any single *or latent* fault** — and never momentarily energize the
motors during boot, brownout, or a glitch.

## Approach / key decisions
- **De-energize-to-open.** Two **normally-open relays in series** on the 24 V bus. Both must be
  actively held closed to run; losing the hold (any reason) opens them.
- **Dynamic relay-enable, never a static GPIO level.** Drive the relay/optocoupler from a
  continuous square-wave / charge-pump that exists *only* while firmware runs correctly, AC-coupled
  so a stuck or glitching DC level (high *or* low) lets the relays open. Immunizes against boot/ROM
  pin states and brownout (see Teensy caveat). #T.B. Thoughts: We insure the firmware runs correctly with a dedicated watchdog, I think a dynamic enable is unnecessary 
- **External voltage supervisor** at a defined threshold forces the safe state below a known
  voltage, independent of the MCU.
- **Watchdog stage = the cutoff authority.** Prefer a **windowed / question-answer** watchdog (not a
  bare one-shot): trips on too-fast *and* too-slow, and proves the CPU is executing. Output
  **latches** the relays open. Here, *the kick is a verified heartbeat* (see Protocol contract).
- **Self-test on every arm** — deliberately let the cutoff trip once and confirm the relays open
  before enabling motors. An untested watchdog is often a dead one.
- **Lease-ceiling enforcement element.** A hardware timer / **personality plug** (resistor, pin-strap,
  or I²C value) bounds the single-shot autonomy window in hardware; **plug absent → shortest/zero**.
- **Relay diagnostics.** Force-guided / mechanically-linked contacts + **read-back**, so a latent
  weld is *detected* and the system refuses to arm (turns the worst latent fault into a detected one).
- **Diversity / no common-cause.** The two cutoff channels should use different mechanisms/timebases
  and separate power; harden against marine ingress, corrosion, ESD/EMI.
- **Don't trust the MCU's internal brownout.** Teensy 4.x has no dependable BOR and its pins aren't
  safe-by-default at boot → Teensy is for **prototyping only**; the safety lives in the external
  supervisor + watchdog + dynamic-enable + relays.

## Interfaces
- **In (from Protocol):** the kick / enable pulse — one per verified heartbeat. HW defines the kick
  waveform and window; Protocol decides when it is earned.
- **Out (to UX):** actual relay-state read-back and supervisor/fault status for indication.
- **Enforces:** the lease *ceiling* (value selected upstream).

## Open questions
- Timer-chip family: analog-R (TPL5010), I²C (DS1374), or pin-strap (MAX6369)?
- Formal (SIL/PL) vs informal high-integrity grade — drives part selection and process.
- Specific relay (force-guided, rating, arc suppression) and connector/sealing for depth.

## References
- Ganssle, *Great Watchdogs* — independent / windowed / how-to-kick.
- NXP **AN4442**, **AN12048** (Q&A watchdog, fail-safe machine); TI **SLLA546** (Q&A watchdog).
- Datasheets: TI **TPL5010/5110**, ADI **DS1374**, ADI **LTC6993**, ADI **MAX6369–74**.
