# Protocol vs. Safety Communication Standards

> Companion to [Trusted Heartbeat — Authorization Protocol](README.md) · [↑ overview](../README.md)
>
> Researched 2026-06-14 against IEC 61508-2, IEC 61784-3 Ed.4, IEC 62280, DNV-RU-UWT Pt5Ch8,
> IMCA R004, ISO 13849-1 / IEC 62061, IEC 62745.

---

## 1. Applicable Standards

### Normatively binding (if formal certification is sought)

| Standard | What it governs | Applicability |
|---|---|---|
| **IEC 61508-2:2010 cl. 7.4.11** | Gateway clause: any E/E/PE safety function using a communication subsystem must evaluate its SCL against the full threat taxonomy; residual error rate ≤ 1% of SIL PFH budget | Primary anchor — applies regardless of domain |
| **IEC 61784-3 Ed.4:2021** | Black-channel principle; 8-class threat taxonomy; TADI RER model (RRI+RRA+RRT+RRM); SIL-derived RER ceilings. Scoped to industrial fieldbuses but medium-agnostic by the black-channel principle | Direct analogy — acoustic modem is a black channel; no acoustic FSCP profile exists but the framework applies |
| **DNV-RU-UWT Pt5Ch8 (AUV)** | DNV classification for untethered UUVs: command-and-control link, mission abort on comms loss, health monitoring | Applies if DNV class notation is sought; defers protocol mechanism selection to IEC 61508 |

### Applicable by strong analogy

| Standard | Notes |
|---|---|
| **IEC 62280:2014 (EN 50159)** | Railway scope, but its 7-class threat taxonomy and MAC/challenge-response provisions (clauses 7.3.7–7.3.9) are cross-referenced across all functional safety communication standards. The canonical reference. |
| **IEC 62745:2017** | Wireless/cableless machinery safety (radio and IR only). Mandates safe-state-on-signal-loss and no re-latching of hazardous outputs — directly relevant to the relay design by analogy. |

### Not directly applicable

**ISO 13849-1 / IEC 62061** — set the SIL/PL target from which RER budgets are derived; contain no protocol content. **DNV OS-E402** and **IMCA R004/S028** — architecture and operations level only; no protocol-level specifications.

---

## 2. Threat Taxonomy Comparison (IEC 62280 / IEC 61784-3 master taxonomy)

### 2.1 Corruption

**Definition:** bits altered in transit by noise, EMI, or channel faults.

**Our mechanism:** 20-bit truncated MAC over `nonce ‖ n`. Any bit corruption in the reply produces
a MAC value that will not match any of the K buffer entries. Because the MAC is keyed, the
false-accept probability is bounded at K/2²⁰ regardless of channel BER — unlike a CRC, which
degrades with Pe. Corruption of the challenge nonce is also caught: the reply MAC will not match.

**Assessment: ✓ Covered**

**Gap:** IEC 61784-3 Ed.4 requires formal proof of the data integrity residual error probability
(the "RCRC-equivalent"). The K/2²⁰ bound must be documented and shown consistent with the SIL
budget. Not yet documented.

---

### 2.2 Unintended Repetition (Replay)

**Definition:** a previously valid frame is re-delivered after its intended window.

**Our mechanism:** nonce consumed (removed from buffer) on first successful MAC match. A replayed
reply either finds its nonce already consumed, or finds it expired (max_age). Both cases → reject.

**Assessment: ✓ Covered**

**Gap:** IEC 61784-3 cl. 5.4.2 specifies a *monotonically incrementing* sequence counter. Our
nonces are random, not monotonic. Functionally equivalent for a single-outstanding-challenge
protocol, but a formal certifier would require a documented argument for equivalence, or a small
monotonic frame counter added to the challenge.

---

### 2.3 Incorrect Sequence (Re-ordering)

**Definition:** messages arrive in a different order from transmission.

**Our mechanism:** each reply is MAC-bound to a specific nonce; a reply for the wrong challenge
fails the buffer lookup. With single-outstanding-challenge operation only the most recently issued
nonce is valid; out-of-order old replies fail automatically.

**Assessment: ~ Partially covered**

**Gap:** No strict monotonic ordering enforced. Functionally adequate for the stated operating
mode; requires documented justification for formal compliance.

---

### 2.4 Loss (Deletion)

**Definition:** a safety message is never delivered.

**Our mechanism:** lease-expiry watchdog — kicked only on a MAC-verified reply. If no valid reply
arrives before the current lease expires, the watchdog trips → relay → OFF. Direct implementation
of IEC 62280 cl. 7.3.4 and IEC 61784-3 cl. 5.4.4.

**Assessment: ✓ Covered**

**Gap:** IEC 61784-3 requires the watchdog timeout be determined by worst-case analysis of the
channel. The variable-lease design (2ⁿ seconds) is novel relative to the standard's fixed-window
assumption and requires explicit safety analysis to justify.

---

### 2.5 Unacceptable Delay

**Definition:** a message arrives too late to be acted upon safely.

**Our mechanism:** nonce `max_age` expiry — a reply arriving after `max_age` finds the nonce
pruned from the buffer and is rejected. Together with the lease watchdog, this creates a bounded
acceptance window.

**Assessment: ✓ Covered**

**Gap:** IEC 62280 cl. 7.3.3 and IEC 61784-3 Ed.4 prefer an explicit timestamp in the frame.
Our `max_age` operates on the sender side only — no frame timestamp. The timeliness residual error
(RRT) must be formally computed and the `max_age` value documented against the worst-case acoustic
round-trip. The IEC 61784-3 Ed.4 "implicit mechanism" concept supports this approach if documented.

---

### 2.6 Insertion

**Definition:** a spurious frame injected from an unauthorized source.

**Our mechanism:** an inserted reply must contain a valid 20-bit MAC for a nonce currently in the
robot's buffer. Without the symmetric key, false-accept probability is K/2²⁰. An inserted challenge
using a fabricated nonce will not be in the robot's buffer, so any reply it elicits is
automatically rejected.

**Assessment: ✓ Covered**

**Gap:** IEC 62280 cl. 7.3.5 and IEC 61784-3 cl. 5.4.5 prefer an explicit source/destination
identifier field. Our frames have none; authentication is implicit via the shared key. IEC 61784-3
Ed.4's "implicit mechanism" concept allows the connection ID to be included in the MAC input
without being transmitted — adding `MAC_key(nonce ‖ n ‖ connection_id)` with a pre-configured
constant would satisfy this without adding frame bytes.

---

### 2.7 Masquerade

**Definition:** a frame appears to come from the legitimate safety peer but originates elsewhere.
IEC 62280 and IEC 61784-3 treat this as an accidental fault, not an adversarial attack — directly
aligned with our declared scope.

**Our mechanism:** the MAC is the primary masquerade countermeasure. No entity without the shared
key can produce a valid MAC. Directly implements IEC 62280 cl. 7.3.9 (cryptographic techniques —
MAC with secret key) and IEC 61784-3 Ed.4 authenticity term (RRA).

**Assessment: ✓ Covered**

**Gap:** IEC 61784-3 Ed.4 requires the masquerade residual error rate (RRM) to be separately
calculated and shown within the SIL budget. Not yet documented.

---

## 3. Protocol Mechanisms Comparison

| Mechanism | IEC 62280 | IEC 61784-3 | Our protocol | Status |
|---|---|---|---|---|
| **Safety code / MAC** | cl. 7.3.8–7.3.9 | cl. 5.4.7 | 20-bit truncated HMAC over `nonce‖n` | ✓ — stronger than CRC at same width on high-BER acoustic channels |
| **Sequence number** | cl. 7.3.2 | cl. 5.4.2 | Random nonce (non-monotonic); consumption-on-use prevents replay | ~ — functionally adequate for single-outstanding-challenge; not formally a monotonic counter |
| **Timestamp** | cl. 7.3.3 | cl. 5.4.3 | `max_age` nonce expiry (sender-side) | ~ — functionally equivalent; no explicit frame field; must be documented as an implicit mechanism |
| **Watchdog / timeout** | cl. 7.3.4 | cl. 5.4.4 | Lease-expiry watchdog, kicked only on MAC-verified reply | ✓ — direct implementation |
| **Source/dest identifier** | cl. 7.3.5 | cl. 5.4.5 | Implicit via shared key (no explicit field) | ~ — sound but diverges from preferred explicit field; fixable by folding `connection_id` into MAC input without transmitting it |
| **Feedback / acknowledgment** | cl. 7.3.6 | cl. 5.4.6 | Entire protocol is challenge-response; home reply is the feedback | ✓ — fully compliant |
| **Challenge-response / identification** | cl. 7.3.7 | Annex A Table A.10 (highest DC variant) | Robot sends fresh TRNG nonce; home must compute correct MAC | ✓ — exceeds minimum; IEC 61508-2 recommends this only for higher SIL; we provide it unconditionally |
| **Cryptographic MAC** | cl. 7.3.9 (required for Category 3) | Ed.4 authenticity; not required for accidental-fault scope alone | Present — HMAC truncated to 20 bits | ✓ — exceeds minimum for our Category 2 scope |
| **Safe state on watchdog expiry** | implicit in cl. 7.3.4 | cl. 5.4.4; Annex D | Relay → OFF on lease expiry; revoke also drives to OFF (unauthenticated, by design) | ✓ |
| **Formal TADI RER documentation** | Annex C | Normative in Ed.4 | Not documented | ✗ — critical gap for formal compliance |

---

## 4. Quantitative Requirements

### Standards framework (IEC 61784-3, 1% rule from IEC 61508-2 cl. 7.4.11)

| SIL | Safety function PFH | SCL RER ceiling |
|---|---|---|
| SIL 1 | < 10⁻⁵ /h | < 10⁻⁷ /h |
| SIL 2 | < 10⁻⁶ /h | < 10⁻⁸ /h |
| SIL 3 | < 10⁻⁷ /h | < 10⁻⁹ /h |

### Our protocol's per-hour false-accept rate

```
P(false accept per hour) = (K / 2²⁰) × (3600 / T)
```

where T = challenge interval in seconds, K = buffer depth.

| K | T | Per-hour rate | Achieves |
|---|---|---|---|
| 1 | 10 s | 3.4 × 10⁻⁴ /h | Below SIL 1 |
| 1 | 60 s | 5.7 × 10⁻⁵ /h | Below SIL 1 |
| 1 | 600 s | 5.7 × 10⁻⁶ /h | Approaching SIL 1 |
| 4 | 60 s | 2.3 × 10⁻⁴ /h | Below SIL 1 |
| 1 | 3600 s | 9.5 × 10⁻⁷ /h | Approaches SIL 2 |

**The 20-bit MAC does not formally achieve SIL 2 (10⁻⁸ /h) at any practically useful acoustic
polling rate.** This is the single most significant quantitative gap.

### What would be required for SIL 3 at T = 60 s, K = 1

Required per-frame probability: 10⁻⁹ /h ÷ 60 /h ≈ **1.7 × 10⁻¹¹** → requires ~**36-bit MAC**.

A 40-bit reply (5 bytes: 4-bit n + 36-bit R) would achieve SIL 3 at T = 60 s, K = 1.

### Important advantage: MAC vs. CRC under high acoustic BER

A CRC's residual error probability degrades with channel Pe. Underwater acoustic Pe can reach
10⁻² to 10⁻¹, which would make a CRC of the same bit-width far weaker. A keyed MAC's false-accept
probability is **1/2²⁰ regardless of Pe** — this is a meaningful advantage on acoustic channels
that is not captured in the standard's CRC-oriented quantitative tables.

### Low-demand mode alternative framing

If the motor-cutoff function is classified as low-demand (activated on demand, not continuously),
IEC 61508 Table 2 applies: **PFD per demand** rather than PFH. The required PFD for SIL 2 is
< 10⁻³ to 10⁻². A per-frame false-accept probability of K/2²⁰ ≈ 4 × 10⁻⁶ is far below this
even for very frequent demands. The 20-bit MAC likely satisfies SIL 2 under the low-demand
framework. This framing requires formal justification in the safety case.

---

## 5. What Our Protocol Has That Standards Don't Require

### Challenge-response watchdog (exceeds minimum)
IEC 61508-2 Annex A Table A.10 *recommends* a challenge-response watchdog for higher SIL but
requires only a simple timeout. We implement the highest-coverage variant unconditionally: home
must compute a correct MAC over a fresh random nonce. A liveness timer or fixed heartbeat would
satisfy the standard; we provide computational proof of liveness.

### MAC as unified data integrity + authentication
Standard IEC 61784-3 profiles (PROFIsafe, FSoE, CIP Safety) typically use a CRC seeded by a
connection identifier — a CRC is not cryptographic and provides no authentication. Our HMAC covers
data integrity, source authentication, and anti-masquerade in a single operation, which is
architecturally stronger than the minimum required for the accidental-fault scope.

### TRNG nonces (stronger than monotonic counters)
A monotonic counter rolls over after power-cycle and creates a replay window. TRNG-seeded CSPRNG
nonces have no rollover hazard within any operationally relevant horizon.

### Unauthenticated revoke (architectural safety principle)
The revoke command requires no authentication because it can only drive toward OFF. This eliminates
the risk that an authenticated revoke fails to arrive during an emergency, leaving the motor
running. No standard explicitly requires this; our design exceeds their intent.

### Variable lease exponent (adaptive watchdog)
The 4-bit lease exponent `n` allows the watchdog timeout to be adapted in-band from 1 s to
32,768 s (≈ 9 h). All IEC 61784-3 profiles use fixed watchdog periods. This enables
single-shot operation through planned acoustic blackouts without the robot classifying them as a
safety trip — a novel operational feature with no standard equivalent.

---

## 6. What Standards Require That We Currently Lack

### 6.1 Formal TADI RER documentation (critical — blocks formal compliance)
IEC 61784-3 Ed.4 requires RRI + RRA + RRT + RRM to be separately estimated and shown ≤ SIL budget.
**Action:** assign SIL from hazard analysis; compute f = 3600/T; document RRI = RRA = RRM =
(K/2²⁰)×f; compute RRT from max_age and acoustic channel worst-case parameters; sum and compare.

### 6.2 Implicit connection identifier in MAC input (moderate)
IEC 62280 cl. 7.3.5 and IEC 61784-3 cl. 5.4.5 want a connection ID binding each frame to a
specific sender-receiver pair. No explicit field exists in our 3-byte frames.
**Action:** without changing the wire format, document the symmetric key as the implicit connection
authenticator, or fold a pre-configured `connection_id` constant into the MAC input:
`MAC_key(nonce ‖ n ‖ connection_id)` — satisfies IEC 61784-3 Ed.4's implicit mechanism concept.

### 6.3 Documented argument for nonce-as-sequence-number (minor for our operating mode)
IEC 62280 cl. 7.3.2 and IEC 61784-3 cl. 5.4.2 require a monotonic counter.
**Action:** document that single-outstanding-challenge operation makes strict ordering irrelevant,
and that nonce consumption-on-use provides equivalent replay and repetition protection. Or add a
small monotonic nibble counter to the challenge frame's 4-bit status field.

### 6.4 Worst-case acoustic channel characterization (critical for parameter setting)
IEC 61784-3 requires watchdog timeout values to be set by worst-case analysis, not empirical
measurement. The max_age value and lease duration `n` cannot be formally validated without knowing
the worst-case Pe, maximum one-way propagation delay, and maximum contiguous outage duration for
the deployment acoustic channel.
**Action:** characterize the acoustic link during development trials; set max_age and minimum `n`
from measured worst-case values; document in the safety case.

### 6.5 MAC bit-width for formal SIL 2/3 at practical polling rates (design choice)
The 20-bit MAC does not achieve IEC 61784-3 SIL 2 (10⁻⁸ /h) at polling rates faster than ~one
challenge per hour. If formal SIL 2 is required:
- Increase reply to 5 bytes: `[n:4 | R:36]` → achieves SIL 3 at T = 60 s, K = 1
- Or formally argue the low-demand (PFD) framework instead of PFH

---

## 7. Verdict

### Architecture: well-aligned with standards practice
The protocol is recognizable as a black-channel safety communication system to any functional
safety engineer. The challenge-response watchdog with TRNG nonces, MAC-based authentication,
fail-safe relay, and unauthenticated revoke are consistent with — and in several respects exceed —
IEC 61784-3 and IEC 62280 minimum requirements.

### Threat coverage: complete in substance
All seven IEC 62280 threat classes are addressed. Coverage of corruption, insertion, masquerade,
loss, and delay is strong. Unintended repetition and incorrect sequence are handled correctly for
the single-outstanding-challenge operating mode but use non-standard mechanisms that require
documented justification rather than assumed equivalence.

### Quantitative compliance: the primary shortfall
The 20-bit MAC does not formally satisfy IEC 61784-3 SIL 2/3 residual error targets at practical
acoustic polling rates. The **right response is not to reflexively widen the MAC** but to:
1. Formally assign a SIL from hazard analysis
2. Compute the TADI RER breakdown with measured acoustic channel parameters
3. If the SIL analysis requires SIL 2: widen MAC to 32–36 bits (one additional reply byte)
   or formally argue the low-demand PFD framework
4. If SIL 1 or low-demand SIL 2 is sufficient: the 20-bit MAC may be adequate with documentation

### Documentation: the blocking issue for formal certification
The protocol design is sound. The safety case documentation is absent: no SIL assignment, no
TADI calculation, no channel characterization, no argument for implicit mechanisms. **None of
these are architectural changes** — they are analysis and documentation tasks that can be
addressed without touching the wire format.

---

## Sources

- IEC 61508-2:2010 clause 7.4.11 — Data communication integrity; 1% rule; black-channel definition
- IEC 61784-3 Ed.4.1:2024 — TADI model; 8 threat classes; SIL RER ceilings
- IEC 62280:2014 (EN 50159:2010+A1:2020) — 7-class threat taxonomy; MAC endorsement (cl. 7.3.9); challenge-response identification (cl. 7.3.7)
- IEC 62745:2017 — Cableless machinery safety; safe-state-on-signal-loss requirement
- DNV-RU-UWT Part 5 Chapter 8 — AUV classification requirements
- IMCA R004 Rev 6 (2024) — ROV/AUV operational guidance
- ODVA Conference 2022: CIP Safety Embracing IEC 61784-3 Ed.4 — TADI normative model detail
- PMC/MDPI Sensors 2021 — Functional Safety Networks and Protocols in the Industrial IoT Era
- MDPI Electronics 2025 — Cryptography-Based Secure Underwater Acoustic Communication for UUVs
