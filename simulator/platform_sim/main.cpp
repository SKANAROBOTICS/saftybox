#include "../lib/platform_node.h"
#include "sim_platform_hal.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <csignal>
#include <arpa/inet.h>
#include <unistd.h>

#include "../key_file.h"

static std::mutex          g_mutex;
static std::atomic<bool>   g_running{true};
static PlatformNode*       g_node = nullptr;

static void signal_handler(int) { g_running = false; }

// ── UDP receive thread ────────────────────────────────────────────────────────
// Mirrors AsyncUDP.onPacket() callback on Teensy.

static void udp_rx_thread(int sock)
{
    uint8_t buf[16];
    sockaddr_in src{};
    socklen_t   srclen = sizeof(src);

    while (g_running) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (sockaddr*)&src, &srclen);
        if (n == PACKET_LEN && g_node) {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_node->onPacket(buf, (size_t)n);
        }
    }
}

// ── Terminal display ──────────────────────────────────────────────────────────

static const char* state_name(PlatformState s) {
    switch (s) {
    case PlatformState::SAFE:    return "SAFE   ";
    case PlatformState::ARMING:  return "ARMING ";
    case PlatformState::ARMED:   return "ARMED  ";
    case PlatformState::TRIPPED: return "TRIPPED";
    }
    return "?";
}

static void render(const PlatformNode& node, SimPlatformHAL& hal)
{
    uint32_t now = hal.nowMs();
    uint32_t rem = node.leaseRemainingMs(now);

    printf("\033[H\033[J");   // clear screen
    printf("╔══════════════════════════════════════╗\n");
    printf("║    UUV SAFETY — PLATFORM SIMULATOR   ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  State   : %-7s                    ║\n", state_name(node.state()));
    printf("║  Relay   : %-6s                     ║\n",
           node.relayClosed() ? "\033[32mCLOSED\033[0m" : "\033[31mOPEN  \033[0m");
    printf("║  Lease   : %6u s remaining          ║\n", rem / 1000);
    printf("║  Last n  : %u (2^n = %u s)              ║\n",
           node.lastN(), 1u << node.lastN());
    printf("║  Nonces  : %u active                   ║\n", node.nonceCount());
    printf("║  WD kick : %u ms ago                ║\n",
           now > hal.simWatchdogKickMs() ? now - hal.simWatchdogKickMs() : 0);
    printf("╚══════════════════════════════════════╝\n");
    printf("  Press Ctrl-C to exit\n");
    fflush(stdout);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    const char* key_path  = (argc > 1) ? argv[1] : "key.bin";
    const char* home_ip   = (argc > 2) ? argv[2] : "127.0.0.1";
    uint16_t    home_port = (argc > 3) ? (uint16_t)atoi(argv[3]) : PORT_HOME;
    uint16_t    my_port   = (argc > 4) ? (uint16_t)atoi(argv[4]) : PORT_PLATFORM;

    uint8_t KEY[MAC_KEY_LEN];
    if (!key_file_load(key_path, KEY)) return 1;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in mine{};
    mine.sin_family      = AF_INET;
    mine.sin_port        = htons(my_port);
    mine.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&mine, sizeof(mine)) < 0) {
        perror("bind"); close(sock); return 1;
    }

    sockaddr_in homeAddr{};
    homeAddr.sin_family = AF_INET;
    homeAddr.sin_port   = htons(home_port);
    inet_pton(AF_INET, home_ip, &homeAddr.sin_addr);

    SimPlatformHAL hal(sock, homeAddr);
    PlatformNode   node(hal, KEY);
    g_node = &node;

    printf("Platform sim: listening on :%u, home at %s:%u  key=%s\n",
           my_port, home_ip, home_port, key_path);

    // Start UDP receive thread (mirrors Teensy AsyncUDP callback)
    std::thread rx(udp_rx_thread, sock);

    // Main loop (mirrors Teensy loop())
    while (g_running) {
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            node.tick();
        }
        render(node, hal);   // hal non-const for nowMs()
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    close(sock);
    rx.detach();
    return 0;
}
