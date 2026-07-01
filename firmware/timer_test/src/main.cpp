#include <Arduino.h>
#include "SafetyChannel.h"
#include "FailsafeSupervisor.h"

// Design A (Teensy-only), Layer 2 -- fully manual/interactive test harness.
// SafetyChannel/FailsafeSupervisor now live in lib/ -- see those for the timer mechanism and
// revoke-gate details. This file is just the sketch: pin/channel wiring and the "verify lease"
// layer (manual serial input today, standing in for the real protocol later).
//
// Lease control is fully manual over serial, using the SAME n-exponent encoding the real
// protocol uses (protocol_defs.h): lease = 2^n seconds, n=0 = stop/revoke. Command format,
// entered as a single line, non-blocking:
//
//   n [offset1_ms] [offset2_ms]
//
// n:        0-15. 0 = stop (force both channels safe, no active lease). 1-15 = arm both
//           channels with a base lease of 2^n seconds.
// offset1:  optional, ms added to channel 1's (pin 31) actual arm value -- deliberately wrong
//           if nonzero. The supervisor still expects the *base* (un-offset) lease, so any
//           offset that persists past the tolerance window will show up as a mismatch.
// offset2:  same, for channel 2 (pin 32).
//
// Examples: "0" then "2" -> both channels armed for 4s, no fault. "2 0 500" -> channel 1 at
// 4000ms, channel 2 at 4500ms -- a deliberate 500ms mismatch, well past tolerance, should latch.

const uint8_t PIN_A = 31;   // safe = HIGH, enable = LOW
const uint8_t PIN_B = 32;   // safe = LOW,  enable = HIGH
const uint8_t N_MAX = 15;   // matches protocol_defs.h

SafetyChannel channels[2];
FailsafeSupervisor supervisor;
bool lastLatched = false;

char cmdBuf[64];
uint8_t cmdLen = 0;

void printUsage() {
    Serial.println("[cmd] usage: n [offset1_ms] [offset2_ms]  (n: 0-15, 0=stop, lease=2^n s)");
}

void processCommand(const char *cmd) {
    int n = -1, offset1 = 0, offset2 = 0;
    int parsed = sscanf(cmd, "%d %d %d", &n, &offset1, &offset2);
    if (parsed < 1 || n < 0 || n > N_MAX) {
        Serial.printf("[cmd] bad input: \"%s\"\n", cmd);
        printUsage();
        return;
    }

    if (n == 0) {
        supervisor.stopAll();
        Serial.printf("[cmd] n=0: stopped t=%lu pinA=%s pinB=%s (checked immediately, before any status tick)\n",
                      millis(), digitalRead(PIN_A) ? "HIGH" : "LOW", digitalRead(PIN_B) ? "HIGH" : "LOW");
        return;
    }

    uint32_t baseMs = (1UL << n) * 1000UL;
    if (!supervisor.armWith(baseMs, offset1, offset2)) {
        Serial.println("[cmd] REFUSED: revoke-gate not satisfied -- send \"0\" first "
                        "(required after boot and after any latched fault)");
        return;
    }
    Serial.printf("[cmd] n=%d base_ms=%lu -> lease1_ms=%ld lease2_ms=%ld (offset1=%d offset2=%d)\n",
                  n, baseMs, (long)baseMs + offset1, (long)baseMs + offset2, offset1, offset2);
    if (baseMs > 170000UL) {
        Serial.println("[cmd] warning: this exceeds IntervalTimer/PIT's practical range (~170s) -- expect it to misbehave, not run this long correctly");
    }
}

void pollSerialInput() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                processCommand(cmdBuf);
                cmdLen = 0;
            }
        } else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) { }

    Serial.println("1");   // reset marker -- printed once per boot/reset

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWriteFast(LED_BUILTIN, HIGH);   // boot blip: LED on, then off after 1s --
    delay(1000);                          // visual flag that a reset just happened
    digitalWriteFast(LED_BUILTIN, LOW);

    channels[0].begin(0, PIN_A, HIGH);
    channels[1].begin(1, PIN_B, LOW);
    supervisor.begin(channels, 2);

    Serial.println("[main] ready, both channels stopped (safe)");
    Serial.println("[main] revoke-gate active: send \"0\" once before the first arm will be accepted");
    printUsage();
}

uint32_t lastStatus = 0;

void loop() {
    pollSerialInput();

    bool latched = supervisor.update();
    if (latched && !lastLatched) {
        Serial.printf("[monitor] LATCHED FAULT t=%lu\n", millis());
    }
    lastLatched = latched;

    digitalWriteFast(LED_BUILTIN, latched ? HIGH : LOW);

    if (millis() - lastStatus >= 500) {
        lastStatus = millis();
        if (supervisor.running()) {
            Serial.printf("[status] t=%lu elapsed=%lu/%lu pinA=%s pinB=%s latched=%d\n",
                          millis(), supervisor.elapsedMs(), supervisor.leaseMs(),
                          digitalRead(PIN_A) ? "HIGH" : "LOW",
                          digitalRead(PIN_B) ? "HIGH" : "LOW",
                          latched);
        }
    }
}
