#pragma once
#include <stdint.h>
#include <stddef.h>

// Hardware Abstraction Layer — home unit side.
//
// The home node is nearly stateless (it just responds to challenges), so
// the HAL is minimal.  UI indicators (LEDs, screen, buzzer) are driven
// directly from HomeNode state by the application layer — they are not
// part of the HAL.

class HomeHAL {
public:
    virtual ~HomeHAL() = default;

    // Monotonic millisecond counter.
    // Arduino: millis()
    virtual uint32_t nowMs() = 0;

    // Transmit a response packet to the platform (UUV).
    // Destination is configured at construction time.
    // Arduino: udp.writeTo(buf, len, platformIp, platformPort)
    virtual void sendPacket(const uint8_t* buf, size_t len) = 0;
};
