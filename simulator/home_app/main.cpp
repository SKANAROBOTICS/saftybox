#include "../lib/home_node.h"
#include "sim_home_hal.h"
#include "panel.h"
#include "../key_file.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/backends/imgui_impl_sdl2.h"
#include "vendor/imgui/backends/imgui_impl_sdlrenderer2.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>

static std::mutex        g_mutex;
static std::atomic<bool> g_running{true};
static HomeNode*         g_node = nullptr;

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

static uint32_t now_ms() {
    using namespace std::chrono;
    static auto epoch = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(
        steady_clock::now() - epoch).count();
}

int main(int argc, char* argv[])
{
    const char* key_path  = (argc > 1) ? argv[1] : "key.bin";
    const char* plat_ip   = (argc > 2) ? argv[2] : "127.0.0.1";
    uint16_t    plat_port = (argc > 3) ? (uint16_t)atoi(argv[3]) : PORT_PLATFORM;
    uint16_t    my_port   = (argc > 4) ? (uint16_t)atoi(argv[4]) : PORT_HOME;

    uint8_t KEY[MAC_KEY_LEN];
    if (!key_file_load(key_path, KEY)) return 1;

    // ── UDP setup ─────────────────────────────────────────────────────────────
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

    std::thread rx(udp_rx_thread, sock);

    // ── SDL2 + ImGui init ─────────────────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "UUV Safety — Home Unit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 500, SDL_WINDOW_SHOWN);
    if (!window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // no imgui.ini clutter
    // Load a font with a wider glyph range so em-dashes and box-drawing render
    static const ImWchar glyph_ranges[] = { 0x0020, 0x00FF, 0x2013, 0x2014, 0 };
    const char* font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        nullptr
    };
    for (int i = 0; font_paths[i]; i++) {
        if (io.Fonts->AddFontFromFileTTF(font_paths[i], 15.f, nullptr, glyph_ranges))
            break;
    }
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    Panel panel;

    // ── Render loop ───────────────────────────────────────────────────────────
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) g_running = false;
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE) g_running = false;
        }

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            node.tick();
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            // Use hal.nowMs() so age = now - lastRxMs uses the same epoch
            panel.render(node, hal.nowMs());
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 14, 17, 22, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    close(sock);
    rx.detach();
    return 0;
}
