# Platform Hardware — Datasheet Review

**TPS482x-Q1 High-Side Switch:** Very similar to the one used in the hub PCB. Approved. The typical application on the first page of the datasheet looks good for our use case.

**CB1 Relay:** Replace with an automotive nano relay — 24 V coil, integrated flyback diode. Preferred families: TE Connectivity (AXICOM / V23079) or Bosch (e.g. 0 986 AH0 090). Both are smaller footprint than standard mini-ISO and rated for the full automotive temperature range. Use a PCB socket so the relay is field-replaceable without rework.
JLCPCB / LCSC availability (mid-2026): Panasonic CB1-D-WM-24V (24 V + diode, C1524412) is catalogued but currently out of stock — verify before ordering or consign separately. TE V23079 (24 V) has limited stock but is signal-relay grade; confirm automotive derating before use. Likely path: source relay directly from TE / Bosch / Panasonic and use JLCPCB for the PCB and socket only.

**NVMFS6H800NT1G MOSFET:** Overkill rating is welcome here. Verify whether the gate drive requirements are within the MCU's drive capability, or whether a gate driver is needed.

**TPS3850-Q1 Watchdog / Supervisor:** Looks perfect for this application.

**PCA9536 I/O Expander:** Looks perfect. Note for a later revision: adding a backup battery would enable timekeeping for time-dependent authentication (TOTP / Google Authenticator style) if that becomes a requirement.

**RPX-0.5Q Power Supply:** Looks good.

---

**Well done:**
1. Found the classic protection triad (reverse-voltage, overvoltage, inrush) that is easy to overlook.
2. Consistent use of automotive-grade, safety-conformant, and readily available components throughout.
