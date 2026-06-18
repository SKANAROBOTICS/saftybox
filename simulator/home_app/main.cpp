// Home app entry point — UI stub.
// Protocol is fully wired; UI (Dear ImGui panel) to be added.
// To test the protocol end-to-end, this stub drives the home node
// from the terminal: type 's' (SAFE), 'a' (SINGLE), 'u' (AUTO),
// '+'/'-' (adjust n).

#include "../lib/home_node.h"
#include "sim_home_hal.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../key_file.h"

static std::mutex        g_mutex;
static std::atomic<bool> g_running{true};
static HomeNode*         g_node = nullptr;

static void signal_handler(int) { g_running = false; }

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

static const char* mode_name(HomeMode m) {
    switch (m) {
    case HomeMode::SAFE:   return "SAFE  ";
    case HomeMode::SINGLE: return "SINGLE";
    case HomeMode::AUTO:   return "AUTO  ";
    }
    return "?";
}

static void render(const HomeNode& node)
{
    auto s = node.status();
    printf("\033[H\033[J");
    printf("╔══════════════════════════════════════╗\n");
    printf("║     UUV SAFETY — HOME APP (stub)     ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  Mode    : %-6s                     ║\n", mode_name(node.mode()));
    printf("║  n       : %2u  (lease = %u s)          ║\n",
           node.n(), 1u << node.n());
    printf("║  Link    : %-4s                       ║\n",
           s.linkLive ? "\033[32mLIVE\033[0m" : "\033[31mLOST\033[0m");
    printf("║  Robot   : status=0x%X  last_n=%u       ║\n",
           s.robotStatus, s.lastGrantedN);
    printf("╚══════════════════════════════════════╝\n");
    printf("  Keys: [s]AFE  [a] SINGLE  [u] AUTO  [+/-] n  [m] mushroom\n");
    fflush(stdout);
}

static void set_raw_mode(bool enable) {
    static termios saved;
    if (enable) {
        termios t;
        tcgetattr(STDIN_FILENO, &saved);
        t = saved;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
}

int main(int argc, char* argv[])
{
    const char* key_path  = (argc > 1) ? argv[1] : "key.bin";
    const char* plat_ip   = (argc > 2) ? argv[2] : "127.0.0.1";
    uint16_t    plat_port = (argc > 3) ? (uint16_t)atoi(argv[3]) : PORT_PLATFORM;
    uint16_t    my_port   = (argc > 4) ? (uint16_t)atoi(argv[4]) : PORT_HOME;

    uint8_t KEY[MAC_KEY_LEN];
    if (!key_file_load(key_path, KEY)) return 1;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in mine{};
    mine.sin_family      = AF_INET;
    mine.sin_port        = htons(my_port);
    mine.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&mine, sizeof(mine)) < 0) {
        perror("bind"); close(sock); return 1;
    }

    sockaddr_in platAddr{};
    platAddr.sin_family = AF_INET;
    platAddr.sin_port   = htons(plat_port);
    inet_pton(AF_INET, plat_ip, &platAddr.sin_addr);

    SimHomeHAL hal(sock, platAddr);
    HomeNode   node(hal, KEY);
    g_node = &node;

    printf("Home app: listening on :%u, platform at %s:%u\n",
           my_port, plat_ip, plat_port);

    std::thread rx(udp_rx_thread, sock);
    set_raw_mode(true);

    while (g_running) {
        char c = 0;
        read(STDIN_FILENO, &c, 1);
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            switch (c) {
            case 's': node.setMode(HomeMode::SAFE);   break;
            case 'a': node.setMode(HomeMode::SINGLE); break;
            case 'u': node.setMode(HomeMode::AUTO);   break;
            case '+': node.setN(node.n() + 1);        break;
            case '-': node.setN(node.n() - 1);        break;
            case 'm': node.mushroom();                 break;
            case 'q': g_running = false;               break;
            }
            node.tick();
        }
        render(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    set_raw_mode(false);
    close(sock);
    rx.detach();
    return 0;
}
