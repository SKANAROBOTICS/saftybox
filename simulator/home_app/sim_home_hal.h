#pragma once
#include "../lib/hal/home_hal.h"
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

class SimHomeHAL : public HomeHAL {
public:
    SimHomeHAL(int sock, const sockaddr_in& platformAddr);

    uint32_t nowMs()                              override;
    void     sendPacket(const uint8_t* buf,
                        size_t len)               override;

private:
    int         _sock;
    sockaddr_in _platformAddr;
};
