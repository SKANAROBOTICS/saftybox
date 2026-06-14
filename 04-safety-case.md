# Safety Case & Residual Risk

> Cross-cutting · [↑ overview](README.md) · binds [HW](01-platform-hardware/README.md) ·
> [Protocol](02-protocol/README.md) · [UX](03-ux/README.md)

System-level safety framing that belongs to no single pillar.

## Scope & threat model
- **Reliability target: prevent equipment loss.** Injury prevention during maintenance is out of
  scope by design — the key is required even for power-on, so the box is inert without an operator.
- **Independent of PX4 / Vera** — this system does not rely on, and is not defeated by, faults in
  the primary autopilot stack.
- **In scope:** random/accidental faults (power, firmware, comms), and **record/replay** of the
  heartbeat.
- **Out of scope (by decision):** a malicious actor — key extraction, MITM, jamming-as-attack.
  "Safe" here means *accidental-fault safe*, not *adversary-proof*.
- **Future:** the design may be extended to a higher-reliability, more general-purpose system in a
  later version; the current scope is deliberately narrow.

## Safety property
The chain of custody: **operator will → UX → trusted heartbeat → fail-safe hardware.** Any break in
the chain → motors off.

## Hazard analysis (start here, not the electronics)
- **Motors-off ≠ vehicle-safe.** Cutting thrust can cause an uncontrolled **sink at depth**, drift
  into an obstacle, or surfacing under a vessel. Confirm motors-off is the safe state in **every
  mission phase**; consider whether the true safe state needs **buoyancy / drop-weight** safing.
- Define the demand sources and the required time-to-safe per phase.

## Residual-risk register
| # | Fault / hazard | Effect | Current mitigation | Gap |
|---|---|---|---|---|
| 1 | **Latent relay weld** | one channel dead, undetected | series redundancy + self-test on arm | needs force-guided contacts + **read-back** to detect |
| 2 | **Common-cause** (overcurrent, shared rail/clock, water ingress, EMI) | both channels fail together | diversity intent, marine hardening | requires real separation + ingress/EMC qualification |
| 3 | Watchdog proves **alive, not correct** | a logic bug issues a valid kick | tiny reviewed logic; HW lease cap bounds damage | systematic-fault process (review/test) |
| 4 | **Systematic/design faults** | dominant real-world failure | review, FMEA, V&V | formal process evidence if certifying |
| 5 | **Single-shot exposure window** | no comms protection for up to the cap | conservative lease cap (HW-enforced) | inherent; minimize the cap |
| 6 | **Malicious** heartbeat forgery | unauthorized enable | out of scope | accept, or add real crypto auth later |

## Integrity target
Current architecture is **informal high-integrity** (de-energize-to-safe, single-fault-tolerant;
shape of a Cat-3 / 1oo2 cutoff). It is **not** a certified SIL/PL. Claiming a *number* requires:
FMEDA with component FIT data, computed PFH/PFD, demonstrated diagnostic coverage, a proof-test
interval, development-process (systematic-capability) evidence, and ideally third-party assessment
(exida / TÜV).

## Build vs. outsource
- **FORT EPC** (SIL 3, IEC 61508, Ethernet) + **SRC Pro** (SIL 3 handheld) = a certified, purchasable
  version of the **AUTO heartbeat** layer; their dead-man timeout is 250 ms–1 s.
- **Mismatch:** no safety-remote product does a multi-minute comms-loss bypass → **single-shot is the
  part you build**; none run **submerged** (RF), so only the EPC-over-Ethernet is relevant underwater.
- Likely shape: **hybrid** — COTS certified dead-man for AUTO, custom logic for the single-shot lease
  and the 24 V motor cutoff + depth packaging.
- **Verify with FORT:** fully-offline operation (no cloud)? any time-limited autonomous bypass? depth
  rating?

## To move the needle (priority order)
1. Relay **read-back / force-guided contacts** (kills the worst latent fault).
2. **Proof test** each channel on every arm.
3. **Channel separation / diversity** + marine ingress / EMI hardening.
4. **Hazard analysis** confirming the safe state at depth (maybe add ballast safing).
5. Keep safety logic **minimal and reviewed**; HW lease cap bounds logic-bug damage.
6. If a *number* is ever required → FMEDA + assessment.
