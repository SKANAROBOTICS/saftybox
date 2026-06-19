#include "panel.h"
#include "vendor/imgui/imgui.h"
#include "../lib/protocol_defs.h"
#include <cstdio>
#include <cmath>

static inline ImVec2 V(float x, float y)  { return ImVec2{x, y}; }
static inline ImU32  C(ImVec4 v)          { return ImGui::ColorConvertFloat4ToU32(v); }
static inline ImU32  Ca(ImVec4 v, float a){ v.w = a; return ImGui::ColorConvertFloat4ToU32(v); }

static const ImVec4 COL_BG        = {0.055f, 0.067f, 0.086f, 1.f};
static const ImVec4 COL_BEZEL     = {0.106f, 0.122f, 0.153f, 1.f};
static const ImVec4 COL_GREEN     = {0.212f, 0.827f, 0.416f, 1.f};
static const ImVec4 COL_BLUE      = {0.227f, 0.627f, 1.000f, 1.f};
static const ImVec4 COL_RED       = {1.000f, 0.231f, 0.188f, 1.f};
static const ImVec4 COL_AMBER     = {1.000f, 0.690f, 0.125f, 1.f};
static const ImVec4 COL_DIM       = {0.118f, 0.141f, 0.192f, 1.f};
static const ImVec4 COL_METAL     = {0.875f, 0.902f, 0.933f, 1.f};
static const ImVec4 COL_DARK_RING = {0.078f, 0.094f, 0.122f, 1.f};
static const ImVec4 COL_MUSH_RED  = {0.878f, 0.149f, 0.102f, 1.f};
static const ImVec4 COL_SCREEN_BG = {0.039f, 0.122f, 0.082f, 1.f};
static const ImVec4 COL_SCREEN_FG = {0.267f, 1.000f, 0.584f, 1.f};
static const ImVec4 COL_SCREEN_DIM= {0.102f, 0.502f, 0.271f, 1.f};
static const ImVec4 COL_KEY_BODY  = {0.102f, 0.118f, 0.157f, 1.f};
static const ImVec4 COL_TICK_HI   = {0.373f, 0.831f, 0.627f, 1.f};

static bool blink_on(uint32_t now_ms, uint32_t period_ms = 500) {
    return (now_ms % period_ms) < (period_ms / 2);
}

// Set at the top of render(), read by all helpers so we avoid threading it everywhere.
static float S = 1.f;

static constexpr float PW       = 800.f;
static constexpr float PH       = 500.f;
static constexpr float KS_OUTER = 42.f;
static constexpr float KS_INNER = 34.f;

Panel::Panel(float scale)
    : _mushroomDepressed(true), _powerOn(true),
      _modeKeyIn(false), _progKeyIn(false),
      _scale(scale),
      _lastRobotRxMs(0), _robotFlashMs(0)
{}

// ── LED drawing ───────────────────────────────────────────────────────────────

static void draw_led(ImDrawList* dl, float cx, float cy, float r,
                     ImVec4 color, bool on, bool blink, uint32_t now_ms,
                     const char* label)
{
    bool visible = on && (!blink || blink_on(now_ms));
    ImU32 col    = visible ? C(color) : C(COL_DIM);

    dl->AddCircle(V(cx, cy), r + 2.f*S, Ca(COL_METAL, 0.15f), 32, 1.f);
    dl->AddCircleFilled(V(cx, cy), r, col);
    if (visible) {
        dl->AddCircleFilled(V(cx - r*0.3f, cy - r*0.3f), r*0.35f,
                            Ca(color, 0.5f));
        dl->AddCircle(V(cx, cy), r + 4.f*S, Ca(color, 0.25f), 32, 3.f);
    }
    if (label) {
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(V(cx - ts.x*0.5f, cy + r + 5.f*S),
                    visible ? C(COL_METAL) : C(COL_DIM), label);
    }
}

void Panel::_drawLeds(HomeNode& node, uint32_t now_ms, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r   = 11.f * S;
    const float gap = 165.f * S;
    float xs[5] = { cx-2*gap, cx-gap, cx, cx+gap, cx+2*gap };

    auto s = node.status();

    // POWER
    draw_led(dl, xs[0], cy, r, COL_GREEN, _powerOn, false, now_ms, "POWER");

    // LINK: ethernet/network path to robot IP (HAL routing probe; always true
    // on loopback, meaningful on a real network interface).
    bool link_up = node.linkLive();
    draw_led(dl, xs[1], cy, r, COL_BLUE, link_up, false, now_ms, "LINK");

    // ROBOT: 100 ms flash on every incoming challenge.
    //   Green = robot echoes n=0 (no active lease, safe/stopped).
    //   Red   = robot echoes n>0 (holds an active lease, running).
    if (s.lastRxMs != _lastRobotRxMs) {
        _lastRobotRxMs = s.lastRxMs;
        _robotFlashMs  = now_ms;
    }
    bool   robot_flash = (s.lastRxMs > 0) && (now_ms - _robotFlashMs < 100);
    ImVec4 robot_col   = (s.echoedN == 0) ? COL_GREEN : COL_RED;
    draw_led(dl, xs[2], cy, r, robot_col, robot_flash, false, now_ms, "ROBOT");

    // ARMED:
    //   Green solid  — not releasing any leases (HOLD, no mushroom)
    //   Green blink  — releasing n=0 (mushroom latched)
    //   Red solid    — releasing normal grants (SINGLE or AUTO, no mushroom)
    bool estop = node.mushroomActive();
    bool armed = (node.mode() != HomeMode::HOLD) && _powerOn && !estop;
    ImVec4 armed_col = estop ? COL_GREEN : (armed ? COL_RED : COL_AMBER);
    draw_led(dl, xs[3], cy, r, armed_col, true, false, now_ms, "ARMED");

    // FAULT: stopped unexpectedly (had a granted lease, now robot echoes n=0)
    bool fault = (s.lastRxMs > 0) && (s.echoedN == 0) && (s.lastGrantedN > 0);
    draw_led(dl, xs[4], cy, r, COL_AMBER, fault, false, now_ms, "FAULT");
}

// ── Screen ────────────────────────────────────────────────────────────────────

static void fmt_time(char* buf, size_t sz, uint32_t secs) {
    if (secs < 60) snprintf(buf, sz, "%u s", secs);
    else           snprintf(buf, sz, "%u:%02u", secs/60, secs%60);
}

void Panel::_drawScreen(HomeNode& node, uint32_t now_ms,
                        float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(V(x, y), V(x+w, y+h), C(COL_SCREEN_BG), 10.f*S);
    dl->AddRect      (V(x, y), V(x+w, y+h), Ca(COL_GREEN, 0.4f), 10.f*S, 0, 2.f);

    ImGui::SetCursorScreenPos(V(x+14.f*S, y+10.f*S));
    ImGui::PushStyleColor(ImGuiCol_Text, C(COL_SCREEN_FG));

    auto s = node.status();
    uint32_t lease_secs = (node.n() > 0) ? (1u << node.n()) : 1u;

    char lease_str[32]; fmt_time(lease_str, sizeof(lease_str), lease_secs);
    char n_str[16];     snprintf(n_str, sizeof(n_str), "n=%u", node.n());
    ImGui::Text("LEASE   %-10s %s", lease_str, n_str);

    ImGui::SetCursorScreenPos(V(x+14.f*S, y+10.f*S+36.f*S));
    // Countdown: display = (2^lastGrantedN + max_age) − elapsed_since_grant
    if (s.lastGrantMs == 0) {
        ImGui::Text("REMAINING ------");
    } else {
        uint32_t total_ms = ((1u << s.lastGrantedN) + NONCE_MAX_AGE_MS / 1000u) * 1000u;
        uint32_t elapsed  = now_ms - s.lastGrantMs;
        // Two sufficient conditions for STOPPED:
        //   1. Timer: full lease window elapsed — robot definitely expired.
        //   2. Robot confirms n=0 after max_age of no new grants — any in-flight
        //      nonce has expired, robot echoes no active lease.
        bool stopped = (elapsed >= total_ms) ||
                       (s.echoedN == 0 && elapsed >= NONCE_MAX_AGE_MS);
        if (stopped) {
            ImGui::Text("REMAINING STOPPED");
        } else {
            char rem_str[32];
            fmt_time(rem_str, sizeof(rem_str), (total_ms - elapsed) / 1000u);
            ImGui::Text("REMAINING %s", rem_str);
        }
    }

    ImGui::SetCursorScreenPos(V(x+14.f*S, y+10.f*S+72.f*S));
    ImGui::PushStyleColor(ImGuiCol_Text,
        s.echoedN == 0              ? C(COL_RED)      :
        s.echoedN >= s.lastGrantedN ? C(COL_SCREEN_FG):
                                      C(COL_AMBER));
    if (s.lastRxMs == 0) {
        ImGui::Text("ROBOT   ---");
    } else if (s.echoedN == 0 && s.lastGrantedN == 0) {
        ImGui::Text("ROBOT   n=0  (safe/idle)");
    } else {
        const char* sync =
            s.echoedN == 0              ? " x STOPPED"   :
            s.echoedN >= s.lastGrantedN ? " v IN SYNC"   :
                                          " ~ IN FLIGHT";
        char robot_str[40];
        snprintf(robot_str, sizeof(robot_str), "ROBOT  n=%-2u%s", s.echoedN, sync);
        ImGui::Text("%s", robot_str);
    }
    ImGui::PopStyleColor(2);
}

// ── Key lock indicator ────────────────────────────────────────────────────────
// Draws a clickable KEY IN / KEY OUT badge; returns true when clicked (toggle).

bool Panel::_drawKeyLock(const char* id, float cx, float cy,
                         bool keyIn, const char* label)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lw = 80.f * S, lh = 22.f * S;
    float lx = cx - lw * 0.5f;

    ImVec4 bg = keyIn ? COL_GREEN : COL_DIM;
    dl->AddRectFilled(V(lx, cy), V(lx+lw, cy+lh), C(bg), 3.f*S);
    dl->AddRect      (V(lx, cy), V(lx+lw, cy+lh), Ca(COL_METAL, 0.25f), 3.f*S, 0, 1.f);

    const char* badge = keyIn ? "KEY IN" : "KEY OUT";
    ImVec2 bts = ImGui::CalcTextSize(badge);
    dl->AddText(V(lx + (lw-bts.x)*0.5f, cy + (lh-bts.y)*0.5f),
                keyIn ? C(COL_DARK_RING) : C(COL_METAL), badge);

    if (label) {
        ImVec2 lts = ImGui::CalcTextSize(label);
        dl->AddText(V(cx - lts.x*0.5f, cy + lh + 4.f*S), C(COL_METAL), label);
    }

    ImGui::SetCursorScreenPos(V(lx, cy));
    return ImGui::InvisibleButton(id, V(lw, lh));
}

// ── Mode key ──────────────────────────────────────────────────────────────────

void Panel::_drawModeKey(HomeNode& node, uint32_t /*now_ms*/, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bw  = 65.f * S;   // button width
    const float bh  = 32.f * S;   // button height
    const float gap =  5.f * S;
    const float tw  = 3.f*bw + 2.f*gap;
    const float x0  = cx - tw * 0.5f;
    const float by  = cy - bh * 0.5f;

    struct Btn { const char* label; HomeMode mode; ImVec4 selCol; };
    static const Btn btns[3] = {
        { "SINGLE", HomeMode::SINGLE, {1.f, 0.69f, 0.125f, 1.f} },   // amber
        { "HOLD",   HomeMode::HOLD,   {0.212f, 0.827f, 0.416f, 1.f} },// green
        { "AUTO",   HomeMode::AUTO,   {1.f, 0.231f, 0.188f, 1.f} },   // red
    };

    bool enabled = _modeKeyIn && _powerOn;

    for (int i = 0; i < 3; i++) {
        float bx = x0 + (float)i * (bw + gap);

        // Register button first so we can read active/deactivated state.
        bool clicked = false, active = false, deact = false;
        if (enabled) {
            char id[32]; snprintf(id, sizeof(id), "mode_btn_%d", i);
            ImGui::SetCursorScreenPos(V(bx, by));
            clicked = ImGui::InvisibleButton(id, V(bw, bh));
            active  = ImGui::IsItemActive();
            deact   = ImGui::IsItemDeactivated();
        }

        // Colour: SINGLE highlights while actively held even before node state updates.
        bool sel = (node.mode() == btns[i].mode) || (i == 0 && active);
        ImVec4 bg = !enabled ? COL_DIM : sel ? btns[i].selCol : COL_BEZEL;
        ImU32  fg = !enabled ? C(COL_DIM) : sel ? C(COL_DARK_RING) : C(COL_METAL);

        dl->AddRectFilled(V(bx, by), V(bx+bw, by+bh), C(bg), 4.f*S);
        dl->AddRect      (V(bx, by), V(bx+bw, by+bh),
                          Ca(COL_METAL, enabled ? 0.35f : 0.1f), 4.f*S, 0, 1.f);
        ImVec2 ts = ImGui::CalcTextSize(btns[i].label);
        dl->AddText(V(bx + (bw-ts.x)*0.5f, by + (bh-ts.y)*0.5f), fg, btns[i].label);

        if (enabled) {
            if (i == 0) {  // SINGLE: hold → grants flow; release → spring to HOLD
                if (active) node.setMode(HomeMode::SINGLE);
                if (deact && node.mode() == HomeMode::SINGLE) node.setMode(HomeMode::HOLD);
            } else if (clicked) {
                node.setMode(btns[i].mode);
                if (btns[i].mode == HomeMode::HOLD && _mushroomDepressed) {
                    node.releaseMushroom();
                    _mushroomDepressed = false;
                }
            }
        }
    }

    // Key lock — positioned below the buttons.
    float lock_y = by + bh + 8.f*S;
    if (_drawKeyLock("mode_lock", cx, lock_y, _modeKeyIn, "MODE")) {
        _modeKeyIn = !_modeKeyIn;
        if (!_modeKeyIn && node.mode() != HomeMode::HOLD)
            node.setMode(HomeMode::HOLD);
    }
}

// ── Program key ───────────────────────────────────────────────────────────────

void Panel::_drawProgramKey(HomeNode& node, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bw  = 36.f * S;   // +/- button width
    const float bh  = 32.f * S;
    const float dw  = 96.f * S;   // value display width
    const float gap =  5.f * S;
    const float tw  = 2.f*bw + dw + 2.f*gap;
    const float x0  = cx - tw * 0.5f;
    const float by  = cy - bh * 0.5f;

    float down_x = x0;
    float disp_x = x0 + bw + gap;
    float up_x   = x0 + bw + gap + dw + gap;

    // Gate: key inserted AND mushroom latched
    bool enabled = _progKeyIn && _mushroomDepressed;

    bool down_clicked = false, up_clicked = false;
    if (enabled) {
        ImGui::SetCursorScreenPos(V(down_x, by));
        down_clicked = ImGui::InvisibleButton("prog_dn", V(bw, bh));
        ImGui::SetCursorScreenPos(V(up_x, by));
        up_clicked = ImGui::InvisibleButton("prog_up", V(bw, bh));
    }
    if (down_clicked) node.setN(node.n() - 1);
    if (up_clicked)   node.setN(node.n() + 1);

    // Draw /2 button
    ImVec4 btn_col = enabled ? COL_BEZEL : COL_DIM;
    ImU32  btn_fg  = enabled ? C(COL_METAL) : C(COL_DIM);
    dl->AddRectFilled(V(down_x, by), V(down_x+bw, by+bh), C(btn_col), 4.f*S);
    dl->AddRect      (V(down_x, by), V(down_x+bw, by+bh),
                      Ca(COL_METAL, enabled ? 0.35f : 0.1f), 4.f*S, 0, 1.f);
    ImVec2 dts = ImGui::CalcTextSize("/2");
    dl->AddText(V(down_x + (bw-dts.x)*0.5f, by + (bh-dts.y)*0.5f), btn_fg, "/2");

    // Draw value display
    uint32_t lease_secs = 1u << node.n();
    char disp[32];
    if (lease_secs < 60)
        snprintf(disp, sizeof(disp), "n=%u  %u s", node.n(), lease_secs);
    else
        snprintf(disp, sizeof(disp), "n=%u  %u:%02u", node.n(), lease_secs/60, lease_secs%60);
    dl->AddRectFilled(V(disp_x, by), V(disp_x+dw, by+bh), C(COL_SCREEN_BG), 4.f*S);
    dl->AddRect      (V(disp_x, by), V(disp_x+dw, by+bh), Ca(COL_GREEN, 0.4f), 4.f*S, 0, 1.f);
    ImVec2 dvts = ImGui::CalcTextSize(disp);
    dl->AddText(V(disp_x + (dw-dvts.x)*0.5f, by + (bh-dvts.y)*0.5f),
                C(COL_SCREEN_FG), disp);

    // Draw ×2 button
    dl->AddRectFilled(V(up_x, by), V(up_x+bw, by+bh), C(btn_col), 4.f*S);
    dl->AddRect      (V(up_x, by), V(up_x+bw, by+bh),
                      Ca(COL_METAL, enabled ? 0.35f : 0.1f), 4.f*S, 0, 1.f);
    ImVec2 uts = ImGui::CalcTextSize("\xc3\x97" "2");   // ×2 UTF-8
    dl->AddText(V(up_x + (bw-uts.x)*0.5f, by + (bh-uts.y)*0.5f), btn_fg, "\xc3\x97" "2");

    // Status hint when locked
    if (!enabled) {
        const char* hint = !_progKeyIn        ? "insert key to program"   :
                           !_mushroomDepressed ? "press UN-ARM to program" : nullptr;
        if (hint) {
            ImVec2 hts = ImGui::CalcTextSize(hint);
            dl->AddText(V(cx - hts.x*0.5f, by + bh + 4.f*S), C(COL_DIM), hint);
        }
    }

    // Key lock
    float lock_y = by + bh + (enabled ? 8.f : 22.f) * S;
    if (_drawKeyLock("prog_lock", cx, lock_y, _progKeyIn, "PROGRAM"))
        _progKeyIn = !_progKeyIn;
}

void Panel::_drawMushroom(HomeNode& node, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float R_RING = 74.f * S;
    const float R_CAP  = 59.f * S;

    dl->AddCircleFilled(V(cx, cy), R_RING, C(COL_BEZEL));
    for (int i = 0; i < 16; i++) {
        float a0 = (float)(i*2)   * (float)M_PI / 16.f;
        float a1 = (float)(i*2+1) * (float)M_PI / 16.f;
        dl->PathArcTo(V(cx, cy), R_RING - 3.f*S, a0, a1, 8);
        dl->PathStroke(C(COL_AMBER), false, 4.f*S);
    }

    bool dep = _mushroomDepressed;
    // Not pressed: cap raised R_CAP*0.18 above ring centre (clearly protruding).
    // Pressed: cap flush with ring centre (sunken in).
    float cap_cy = dep ? cy : cy - R_CAP * 0.18f;
    ImVec4 capCol = dep ? ImVec4{0.6f, 0.1f, 0.07f, 1.f} : COL_MUSH_RED;
    dl->AddCircleFilled(V(cx, cap_cy), R_CAP, C(capCol));
    dl->AddCircleFilled(V(cx - R_CAP*0.3f, cap_cy - R_CAP*0.4f),
                        R_CAP * 0.2f, Ca(ImVec4{1,1,1,1}, 0.15f));

    // Raw click detection — InvisibleButton fires on the first frame from a
    // stale SDL event, which would spuriously latch the estop on startup.
    {
        ImVec2 mp = ImGui::GetMousePos();
        bool inZone = mp.x >= cx - R_RING && mp.x <= cx + R_RING &&
                      mp.y >= cy - R_RING && mp.y <= cy + R_RING;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inZone) {
            if (!_mushroomDepressed) {
                node.mushroom();
                _mushroomDepressed = true;
            } else {
                node.releaseMushroom();
                _mushroomDepressed = false;
            }
        }
    }

    ImVec2 ts = ImGui::CalcTextSize("UN-ARM");
    dl->AddText(V(cx - ts.x*0.5f, cy + R_RING + 6.f*S), C(COL_RED), "UN-ARM");
    const char* sub = dep ? "click to reset" : "press \xe2\x80\x94 latches";
    ImVec2 ts2 = ImGui::CalcTextSize(sub);
    dl->AddText(V(cx - ts2.x*0.5f, cy + R_RING + 22.f*S),
                dep ? C(COL_AMBER) : C(COL_DIM), sub);
}

void Panel::_drawOnOff(float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = 32.f * S, H = 80.f * S;
    float x0 = cx - W*0.5f, y0 = cy - H*0.5f;

    dl->AddRectFilled(V(x0-3*S, y0-3*S), V(x0+W+3*S, y0+H+3*S), C(COL_DARK_RING), 8.f*S);

    ImVec4 iCol = _powerOn ? COL_GREEN : COL_DIM;
    dl->AddRectFilled(V(x0, y0), V(x0+W, y0+H*0.45f), C(iCol), 4.f*S);
    ImVec2 ti = ImGui::CalcTextSize("I");
    dl->AddText(V(x0 + W*0.5f - ti.x*0.5f, y0 + H*0.1f), C(COL_DARK_RING), "I");

    ImVec4 oCol = _powerOn ? COL_DIM : COL_METAL;
    dl->AddRectFilled(V(x0, y0+H*0.55f), V(x0+W, y0+H), C(oCol), 4.f*S);
    ImVec2 to = ImGui::CalcTextSize("O");
    dl->AddText(V(x0 + W*0.5f - to.x*0.5f, y0 + H*0.65f), C(COL_DIM), "O");

    ImGui::SetCursorScreenPos(V(x0-10*S, y0-10*S));
    if (ImGui::InvisibleButton("onoff", V(W+20*S, H+20*S)))
        _powerOn = !_powerOn;

    ImVec2 ts = ImGui::CalcTextSize("ON/OFF");
    dl->AddText(V(cx - ts.x*0.5f, y0 + H + 8.f*S), C(COL_METAL), "ON/OFF");
}

// ── Main render ───────────────────────────────────────────────────────────────

void Panel::render(HomeNode& node, uint32_t now_ms)
{
    S = _scale;
    float pw = PW * S, ph = PH * S;

    if (!_powerOn) {
        if (node.mode() != HomeMode::HOLD) node.setMode(HomeMode::HOLD);
        if (_mushroomDepressed) { node.releaseMushroom(); _mushroomDepressed = false; }
    }

    ImGui::SetNextWindowSize(V(pw, ph), ImGuiCond_Always);
    ImGui::SetNextWindowPos(V(0, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(COL_BG));
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(V(10*S,10*S), V(pw-10*S,ph-10*S), C(COL_DARK_RING), 24.f*S);
    dl->AddRectFilled(V(22*S,22*S), V(pw-22*S,ph-22*S), C(COL_BEZEL), 16.f*S);

    const char* title = "UUV SAFETY  \xe2\x80\x94  HOME UNIT";
    ImVec2 tts = ImGui::CalcTextSize(title);
    dl->AddText(V(pw*0.5f - tts.x*0.5f, 36.f*S), C(COL_METAL), title);

    _drawLeds      (node, now_ms, pw*0.5f,       78.f*S);
    _drawOnOff     (95.f*S,       210.f*S);
    _drawScreen    (node, now_ms, 290.f*S, 148.f*S, 300.f*S, 138.f*S);
    _drawModeKey   (node, now_ms, 165.f*S,       380.f*S);
    _drawMushroom  (node,         pw*0.5f,        382.f*S);
    _drawProgramKey(node,         pw - 165.f*S,  380.f*S);

    ImGui::End();
    ImGui::PopStyleColor();
}
