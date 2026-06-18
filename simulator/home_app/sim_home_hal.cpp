#include "sim_home_hal.h"
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

static uint64_t s_epoch_us = 0;

static uint64_t monotonic_us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

SimHomeHAL::SimHomeHAL(int sock, const sockaddr_in& platformAddr)
    : _sock(sock), _platformAddr(platformAddr)
{
    if (!s_epoch_us) s_epoch_us = monotonic_us();
}

uint32_t SimHomeHAL::nowMs()
{
    return (uint32_t)((monotonic_us() - s_epoch_us) / 1000ULL);
}

void SimHomeHAL::sendPacket(const uint8_t* buf, size_t len)
{
    sendto(_sock, buf, len, 0,
           (const sockaddr*)&_platformAddr, sizeof(_platformAddr));
}

bool SimHomeHAL::linkLive()
{
    uint32_t now = nowMs();
    if (now - _linkCheckMs < 1000u && _linkCheckMs != 0)
        return _linkUp;
    _linkCheckMs = now;

    // Check routing to platform IP without sending any traffic:
    // UDP connect() on SOCK_DGRAM only sets the destination — no packets sent.
    // Returns ENETUNREACH / EHOSTUNREACH if no route exists.
    // Arduino (Teensy + QNEthernet): return Ethernet.linkStatus() == LinkON;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { _linkUp = false; return false; }
    int r = connect(s, (const sockaddr*)&_platformAddr, sizeof(_platformAddr));
    close(s);
    _linkUp = (r == 0);
    return _linkUp;
}
