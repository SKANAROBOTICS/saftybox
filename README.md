# UUV Secondary Safety System

An independent, secondary safety system for a UUV, independant of **of PX4/vera**. It does one thing:

> **Guarantee that motor power is removed unless a genuine, current authorization to run is present.**
Relaiability level: Prevent Equipment Loss. Injury during maintenance is out of scope by design (key required even power on). 
On later veraion this might be extended to more general purpose higher relibility system by updating the design. 

These documents capture the **main challenges** of the design — they are deliberately *not* a
complete specification. The earlier long-form draft is kept at [`archive/DESIGN.md`](archive/DESIGN.md).

## The idea: a chain of custody of intent

> Operator's **will** → captured by **UX** → carried as a **trusted heartbeat** → enforced by
> **fail-safe hardware**.

The safety property is the contrapositive: **break the chain anywhere — operator stops, link
drops, firmware hangs, power sags — and the motors lose power.** Each pillar below owns one link of
that chain and hands off to the next.

## The three pillars

| Handle | Document | Functional purpose | Main challenge |
|---|---|---|---|
| **Safe state** | [Fail-Safe Actuation — Platform Hardware](01-platform-hardware/README.md) | deliver / remove 24 V motor power; default OFF | converge to motors-off under **any single or latent fault** |
| **Trust** | [Trusted Heartbeat — Authorization Protocol](02-protocol/README.md) | continuously prove authorization (the *lease*) over a low-bandwidth link | enable **only** on a genuine, fresh, *intended* heartbeat — immune to accidental triggering & replay |
| **Intent** | [Operator Will — Safe Command & Control (UX)](03-ux/README.md) | capture & transfer the operator's intent (arm / set autonomy / stop) | deliberate to enable, effortless to stop, **impossible to trigger by accident** |

System-level safety framing that belongs to no single pillar lives in
**[Safety Case & Residual Risk](04-safety-case.md)** (scope, threat model, hazard analysis,
residual-risk register, integrity target, build-vs-outsource).

## Interface contracts (the glue between the pillars)

Keeping the pillars in separate contexts only works if their boundaries are explicit:

- **UX → Protocol** — arming (key + sequence) authorizes the protocol to start; the mode/lease
  selection feeds the `mode` index; the mushroom maps to *stop-heartbeat + revoke*.
- **Protocol → HW** — *a valid heartbeat = one watchdog kick / enable pulse.* The protocol decides
  **when** a kick is earned; the hardware defines **what** a kick looks like. The lease **value** is
  chosen by Protocol/UX; the lease **ceiling** is enforced in hardware.
- **HW ↔ UX** — the key and mushroom **physically** gate enable; indicators reflect the **actual**
  hardware state (relay read-back, link health), with **stale ≠ live**.

## Artifacts

- [`03-ux/panel-sketch.html`](03-ux/panel-sketch.html) — home-unit front-panel concept sketch (open in a browser).
- [`archive/DESIGN.md`](archive/DESIGN.md) — prior detailed draft (superseded by these docs).
