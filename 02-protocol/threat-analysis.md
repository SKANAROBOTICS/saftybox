# Protocol Threat & Immunity Analysis

> Companion to [Trusted Heartbeat — Authorization Protocol](README.md) · [↑ overview](../README.md)
>
> Scope: accidental-fault-safe only. See [Safety Case](../04-safety-case.md) for the system-level
> threat model. Researched 2026-06-14 against IEC 61508, IEC 62280, NIST SP 800-107/800-224,
> acoustic UUV literature, and MAC-truncation security literature.

---

## 1. The Accidental-Fault / Adversary-Proof Boundary

These are two formally distinct threat models. The protocol commits to the first and explicitly
declines the second.

**Accidental-fault-safe (IEC 61508 scope)** assumes no intelligent adversary. Threats arise from
entropy sources, physics, firmware bugs, channel noise, and power failures — Shannon's noisy channel
plus Murphy's law. The question is: *can a random physical-world event cause the system to authorize
something it should not, or fail to cut power when it should?*

**Adversary-proof (IEC 62443 / cryptographic security scope)** assumes an intelligent, motivated
attacker who can observe ciphertexts, choose inputs, replay messages at will, and exploit
computational weaknesses. A 24-bit MAC is cryptographically weak in this scope (NIST SP 800-224
minimum is 32 bits; Black & Cochran, "MAC Reforgeability," IACR 2006, show truncation degrades
unforgeability against chosen-message attackers). But for the accidental-fault scope the question is
only: *what is the probability that a randomly corrupted acoustic frame passes the MAC check?* These
are entirely different calculations with entirely different answers.

**Critical architectural constraint:** Revoke/disable is intentionally unauthenticated because it
can only drive toward OFF — always safe. This is a deliberate design decision. Future maintainers
must NOT add authentication to the revoke path; doing so would convert an unconditionally-safe
feature into a security-dependent one.

---

## 2. Threats IN SCOPE (Accidental-Fault Domain)

### 2.1 Random-Frame False Acceptance

**Mechanism:** The acoustic link delivers a corrupted or unrelated 3-byte frame. The robot checks
it against K buffered nonces (one comparison each — `n` is received explicitly in the frame).
Probability of any match: **K / 2²⁰**.

**Current handling:** Explicitly accepted: ~1-in-131,000 per received packet for K=8. This is the
designed residual risk.

**Gaps:**
- The 1-in-131,000 figure is per received packet, not per unit time. The quantity that matters for a
  safety case (IEC 61508, IEC 62280) is:
  ```
  P(false accept per hour) = F_packets_per_hour × K / 2²⁰
  ```
  For K=8, F=3,600 (one spurious frame/second), this gives **0.028 expected false accepts per hour
  — one every 36 hours**. For F=0.1 (one per 10 seconds), one every 15 days. Underwater acoustic
  BER is 10⁻³ to 10⁻¹; the spurious frame rate must be characterized before deployment and this
  budget closed against the integrity target. **This calculation is currently absent.**
- A noise packet matching at n=15 (32,768-second lease) is quantitatively more dangerous than one
  matching at n=0. The hardware lease ceiling (personality plug) is the backstop — must be resolved
  before deployment.

### 2.2 Accidental Replay via Circular Buffer

**Mechanism:** A valid R, whose nonce was already consumed, is re-delivered by the acoustic
transport (multipath echo, ARQ retransmit).

**Current handling:** Correctly handled by consume-on-first-match. This is the standard anti-replay
window mechanism (cf. IPsec RFC 6479, acoustic UUV security literature).

**Gaps:**
- This defense works only if the modem driver surfaces each physical burst at most once at the
  application layer. If modem firmware performs link-layer ARQ and can surface two application-layer
  deliveries from one physical burst, deduplication must happen below the application comparison
  loop. **Responsibility must be assigned explicitly.**
- Post-reset behavior is unspecified. If the robot reboots before consuming a nonce (nonce in
  buffer, R in flight), the nonce remains valid post-reset and a late-arriving R can re-arm. This
  may or may not be the operator's intent. Must be stated explicitly.

### 2.3 Nonce Entropy Failure

**Mechanism:** The hardware RNG fails silently (stuck bit, biased output), or a CSPRNG is re-seeded
from the same low-entropy source after every power cycle (e.g., RTC at zero). Result: repeated or
predictable nonces. A response R computed for nonce_i at T1 remains valid against nonce_i appearing
again in the buffer at T2.

**Current handling:** Listed as an open question. **This is the most significant unresolved
accidental-fault gap.** Without a qualified entropy source, replay immunity is not formally
established.

**Birthday collision in the buffer:** With K active nonces simultaneously and no uniqueness check,
collision probability ≈ K(K-1) / 2^(W+1). At W=20 (1M values) and K=8 this would be ~1-in-18,000
per batch — marginal. The design resolves this by **checking uniqueness before adding a nonce to
the buffer** (regenerate if the candidate already exists). With this check, all K active nonces are
distinct by construction; birthday collision probability in the buffer = 0. W=20 is therefore
sufficient; the uniqueness check is the load-bearing mechanism.

### 2.4 Buffer Window Sizing (Latency Mismatch)

**Mechanism:** Acoustic round-trip exceeds K×T seconds. Valid R arrives after nonce aged out of the
buffer. Watchdog not kicked. Relay opens. This is an availability (nuisance-trip) fault.

**Current handling:** Correctly identified: "Latency tolerance = K×T seconds. Tune K to cover
expected acoustic round-trip."

**Gaps:**
- No minimum liveness requirement is stated. Without it, K and T cannot be formally validated
  against link characterization data.
- Buffer-full behavior on sustained blackout is unspecified. Silent displacement of unacknowledged
  nonces is the correct behavior — **this must be documented explicitly** so no implementer treats
  buffer-full as an error condition that stops nonce generation.
- Clock drift between home and robot can narrow the effective window. Maximum permissible T drift
  should be specified.

### 2.5 Firmware Hang — Watchdog Starvation

**Mechanism:** UUV firmware hangs. Watchdog not kicked. Lease expires. Relays open. **This is the
intended fail-safe behavior.**

**Gaps:**
- The dangerous asymmetric case is a hang that produces a *spurious kick* (residual risk #3:
  "watchdog proves alive not correct"). Mitigation is "tiny reviewed logic + HW lease cap." This
  must be paired with a specific test-vector set and fault-injection test plan exercising the
  exhaustive search loop, nonce-consume step, and kick path in isolation.
- **Atomic ordering must be specified:** consume nonce first, then kick watchdog. If the kick fires
  before the nonce is consumed, a second delivery of the same R within the buffer window could kick
  a second time.

### 2.6 Key Corruption in Flash

**Mechanism:** Single-event upset, flash wear, or incomplete write corrupts the stored pre-shared
key. The two units now hold different keys. Every subsequent MAC comparison fails. Watchdog expires.
Relays open. **Fail-safe direction is safe** — key corruption causes denial of authorization, not
false authorization.

**Gaps:**
- Key corruption is **silent until the first MAC verification fails** — discovered mid-mission.
  A boot-time known-answer test (KAT): compute MAC(test_vector) against a stored expected output,
  refuse to arm if mismatch. This is standard practice (FIPS 140-3 conditional self-tests).
- No redundant key storage is specified. A CRC-protected duplicate in a separate flash region
  provides accidental-fault resilience.
- Provisioning mismatch (wrong key flashed at factory) produces a permanently inoperative pair
  with **no diagnostic distinguishing "keys differ" from "acoustic link down."** A factory
  acceptance test (bench challenge-response before deployment) is mandatory.

### 2.7 Lease Grant Accuracy

**Mechanism:** `n` is transmitted explicitly by home in the reply packet `[n:4 | R:20]`. The robot
reads `n` directly and grants lease `2ⁿ` seconds if and only if the 20-bit MAC matches. There is
no exhaustive search across n values; the robot grants exactly the n home sent.

**Current handling:** Correctly handled by explicit n transmission. A noise packet that passes the
MAC check will grant whatever `n` is in that packet — the hardware lease ceiling is the backstop
against an accidentally high `n` value. **Ceiling must be resolved before deployment.**

**Gaps:**
- Hardware lease ceiling (personality plug max n) is still an open question. Until resolved, a
  noise packet matching at n=15 could grant 32,768 seconds with no hardware backstop.

### 2.8 Post-Reset Buffer State

**Mechanism:** Robot reboots. Nonce buffer reinitialized to empty. R packets in flight are
orphaned. System cannot re-arm until K new nonces generated and at least one valid R received.

**Gaps:**
- If a software reset preserves RAM, nonces from the previous session may remain valid in the
  buffer. A late-arriving R from before the reset could re-arm. **The buffer must be cleared on
  every reset path**, or every reset path must be analyzed explicitly.
- The protocol should specify what status the UUV reports post-reset vs. post-trip.

---

## 3. Threats OUT OF SCOPE (Adversarial Domain)

These are correctly excluded. Brief notes on boundary clarity follow.

| Threat | Why adversarial | Note |
|---|---|---|
| **Key extraction** (JTAG, side-channel, decap) | Requires physical access and deliberate intent | Flash read-protect is sufficient for this scope |
| **Man-in-the-middle** | Requires active acoustic transmitter and key knowledge | MAC binding of n prevents lease substitution even with the nonce |
| **Deliberate replay** (recorded R, timed retransmit) | Requires deliberate recording and timed retransmission | Consume-on-match defeats accidental replay for free |
| **Brute-force forgery** (16M crafted 3-byte frames) | 131,072 frames at acoustic bandwidth = deliberate, sustained injection | An upper bound on modem spurious frame rate makes this boundary explicit (see §2.1) |
| **Deliberate acoustic jamming** | Requires a transmitter and intent | Accidental link loss (multipath, depth, obstacle) is in-scope and handled by lease expiry |
| **Multi-UUV cross-routing** | Routing a response from one UUV to another is deliberate | Accidental cross-authorization via shared key is in-scope; per-pair key derivation eliminates it |
| **Side-channel on MAC computation** | Requires deliberate measurement infrastructure | |
| **Supply-chain key injection attack** | Deliberate insider threat | Provisioning mismatch (accidental wrong key) is in-scope (§2.6) |

**Unauthenticated revoke:** An adversary could send a stop command. This is correctly accepted —
it can only drive toward OFF. However, this means the system does not prevent a **remote nuisance
trip** in an adversarial environment. This should be documented as a known adversarial consequence
that is outside the declared scope, not a design oversight.

---

## 4. MAC Truncation: Is 20 Bits Adequate?

**Current design:** 20-bit R, explicit 4-bit n, reply = 3 bytes. Per-frame false-accept: K/2²⁰.

**Literature position:**
- NIST SP 800-224 (2024, supersedes SP 800-107 Rev 1): absolute minimum 32-bit tag; tags below
  64 bits require explicit risk analysis. The 20-bit tag is below this floor.
- Black & Cochran, "MAC Reforgeability" (IACR 2006): t-bit truncated MAC permits forgery after
  2^t queries in the adversarial setting. For 20 bits: ~1 million queries — trivial for a machine.
- For the **accidental-fault scope**, "queries" are randomly corrupted acoustic frames. 1 million
  corrupted 3-byte frames arriving on an acoustic modem is physically implausible on any reasonable
  timescale. The analysis reduces to the per-hour false-accept budget in §2.1.

**Verdict:** 20-bit MAC is adequate for the accidental-fault scope *if and only if* the per-hour
false-accept probability is closed against the integrity target. If acoustic noise characterization
yields F > ~0.1 spurious frames/second, upgrade to **32-bit R (4-byte reply: n:4 | R:32)**, which
brings false-accept odds to K/2³² ≈ negligible at any realistic acoustic noise rate.

---

## 5. Circular Buffer Design: Assessment

**What is correct:**
- Newest-first iteration with consume-on-match: standard, correct anti-replay window.
- Home is stateless (just responds to any nonce it receives): eliminates synchronization faults.
- Latency tolerance = K×T: clean, tunable.

**What needs attention:**
- Birthday collision in the buffer is eliminated by the pre-send uniqueness check (see §2.3).
- Buffer displacement on sustained blackout: home unit should send a fresh response to the most
  recently received nonce on link recovery, not retry old responses.
- Lease-expiry / R-arrival race in AUTO mode: watchdog window must be set with sufficient margin
  above the heartbeat interval T to avoid nuisance trips from processing jitter.

---

## 6. Key Management Requirements (Accidental-Fault Scope)

Even without an adversary, key management creates accidental-fault hazards:

| Requirement | Status | Priority |
|---|---|---|
| Boot-time KAT (MAC(test_vector) vs stored expected) | Not specified | **Must-have before deployment** |
| Redundant/CRC-protected key storage in flash | Not specified | High |
| Per-pair key derivation `key_i = KDF(master, UUV_serial)` | Listed as option | High — eliminates fleet-wide blast radius |
| Factory acceptance test (bench challenge-response) | Not specified | **Must-have before deployment** |
| Key rotation procedure | Listed as open | Must be resolved before first field deployment |
| Flash read-protect on production MCU | Mentioned | Low (adversarial-scope mitigation, not accidental) |

---

## 7. Residual-Risk Register Additions

Items not yet in the register that should be added:

| # | Fault | Effect | Mitigation | Gap |
|---|---|---|---|---|
| 7 | **Per-hour false-accept rate** (acoustic noise × K/2²⁰) | Accidental motor enable | 20-bit MAC; hardware lease ceiling | Per-hour budget not yet computed or compared against integrity target |
| 8 | **Nonce entropy source failure** (stuck RNG, cold-boot seed) | Nonce reuse; replay immunity lost | HW RNG qualification or qualified CSPRNG seeding | Entropy source unspecified |
| 9 | **Key corruption in flash** (bit-rot, incomplete write) | Permanent denial of authorization | Fail-safe: relays open | No boot-time KAT; no redundant storage |
| 10 | **Cross-n accidental collision** (different n, same 24-bit MAC) | Robot grants wrong lease duration | HW lease ceiling bounds maximum damage | Not quantified; ceiling not yet defined |
| 11 | **Safe-state at depth** (motors-off → uncontrolled sink) | Vehicle loss or collision | Mission-phase analysis; possibly drop-weight | Listed in hazard analysis; no resolution |

---

## 8. Open Questions That Must Be Resolved Before Implementation

~~1. **Nonce width W**~~ — **Resolved:** W=20 bits, with pre-send uniqueness check guaranteeing
all K active nonces are distinct. Birthday collision = 0 by construction.

~~2. **Entropy source**~~ — **Resolved:** hardware TRNG (i.MX RT1062 on prototype; hard criterion
for production MCU). TRNG → CSPRNG seed at boot; block nonce generation until TRNG ready.
See [Entropy source section](README.md#entropy-source-resolved).

3. **Per-hour false-accept budget** — requires measured/bounded acoustic spurious frame rate.
4. **Hardware lease ceiling** — personality plug max value must be defined and enforced in HW.
5. **Archive/DESIGN.md 2-byte MAC** — formally mark superseded to prevent implementation confusion.
6. **Revoke path** — document as architectural constraint, not an omission.
7. **Consume-then-kick ordering** — specify as atomic in firmware requirements.
8. **Modem deduplication responsibility** — assign explicitly to modem layer or application layer.
9. **Post-reset buffer state** — must be cleared on every reset path; state machine specified.
10. **Buffer-full eviction behavior** — document as expected, not an error condition.

---

## Sources

- NIST SP 800-224 (2024) / SP 800-107 Rev 1 — MAC truncation guidance
- Black & Cochran, "MAC Reforgeability," IACR ePrint 2006/095
- IEC 61508:2010 — Functional Safety of E/E/PE Safety-related Systems
- IEC 62280:2014 — Safety-related communication in transmission systems (railway; threat taxonomy)
- MDPI Electronics 2025 — Cryptography-Based Secure Underwater Acoustic Communication for UUVs
- PMC/Sensors 2012 — A Secure Communication Suite for Underwater Acoustic Sensor Networks
- ResearchGate — Replay-Attack Countermeasures for Underwater Acoustic Networks
- exida — PFH: Probability of Dangerous Failure per Hour
- Memfault — Firmware Watchdog Best Practices
- US Patent 9032258 — Safety system challenge-and-response using modified watchdog timer
- RFC 6479 — IPsec Anti-Replay Algorithm without Bit Shifting (sliding window / consume-on-match)
