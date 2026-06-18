#include "panel.h"
#include "vendor/imgui/imgui.h"
#include "../lib/protocol_defs.h"
#include <cstdio>
#include <cmath>

// ── Convenience shorthand ─────────────────────────────────────────────────────
static inline ImVec2 V(float x, float y)  { return ImVec2{x, y}; }
static inline ImU32  C(ImVec4 v)          { return ImGui::ColorConvertFloat4ToU32(v); }
static inline ImU32  Ca(ImVec4 v, float a){ v.w = a; return ImGui::ColorConvertFloat4ToU32(v); }

// ── Color palette ─────────────────────────────────────────────────────────────
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

// ── Blink helper ──────────────────────────────────────────────────────────────
static bool blink_on(uint32_t now_ms, uint32_t period_ms = 500) {
    return (now_ms % period_ms) < (period_ms / 2);
}

// ── LINK staleness thresholds ─────────────────────────────────────────────────
static constexpr uint32_t LINK_FRESH_MS = T_CHALLENGE_MS;
static constexpr uint32_t LINK_STALE_MS = T_CHALLENGE_MS * 3;

// ── Panel constants ───────────────────────────────────────────────────────────
static constexpr float PW       = 800.f;
static constexpr float PH       = 500.f;
static constexpr float KS_OUTER = 42.f;   // key-switch housing outer radius
static constexpr float KS_INNER = 34.f;   // key-switch face radius

Panel::Panel()
    : _singlePressedAt(0), _upPressedAt(0), _downPressedAt(0),
      _mushroomDepressed(false), _powerOn(true)
{}

// ── LED drawing ───────────────────────────────────────────────────────────────

static void draw_led(ImDrawList* dl, float cx, float cy, float r,
                     ImVec4 color, bool on, bool blink, uint32_t now_ms,
                     const char* label)
{
    bool visible = on && (!blink || blink_on(now_ms));
    ImU32 col    = visible ? C(color) : C(COL_DIM);

    dl->AddCircleFilled(V(cx, cy), r, col);
    if (visible) {
        dl->AddCircleFilled(V(cx - r*0.3f, cy - r*0.3f), r*0.35f,
                            Ca(color, 0.5f));
        dl->AddCircle(V(cx, cy), r + 4.f, Ca(color, 0.25f), 32, 3.f);
    }
    if (label) {
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(V(cx - ts.x*0.5f, cy + r + 5.f),
                    visible ? C(COL_METAL) : C(COL_DIM), label);
    }
}

void Panel::_drawLeds(HomeNode& node, uint32_t now_ms, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r   = 11.f;
    const float gap = 165.f;
    float xs[5] = { cx-2*gap, cx-gap, cx, cx+gap, cx+2*gap };

    auto s       = node.status();
    uint32_t age = now_ms - s.lastRxMs;

    // POWER
    draw_led(dl, xs[0], cy, r, COL_GREEN, _powerOn, false, now_ms, "POWER");

    // LINK
    bool link_stale = (age >= LINK_FRESH_MS) && (age < LINK_STALE_MS);
    bool link_dead  = (age >= LINK_STALE_MS);
    draw_led(dl, xs[1], cy, r, COL_BLUE, !link_dead, link_stale, now_ms, "LINK");

    // ROBOT: driven by echoedN vs lastGrantedN
    bool   robot_stale = link_stale || link_dead;
    bool   robot_on    = (s.lastRxMs > 0);
    ImVec4 robot_col   = COL_RED;
    if (robot_on) {
        if      (s.echoedN == 0)               robot_col = COL_RED;
        else if (s.echoedN >= s.lastGrantedN)   robot_col = COL_GREEN;
        else                                    robot_col = COL_AMBER;
    }
    draw_led(dl, xs[2], cy, r, robot_col, robot_on, robot_stale, now_ms, "ROBOT");

    // ARMED: blinks while heartbeating
    bool armed = (node.mode() != HomeMode::SAFE) && _powerOn;
    draw_led(dl, xs[3], cy, r, COL_RED, armed, armed, now_ms, "ARMED");

    // FAULT: stopped unexpectedly
    bool fault = robot_on && (s.echoedN == 0) && (s.lastGrantedN > 0);
    draw_led(dl, xs[4], cy, r, COL_AMBER, fault, false, now_ms, "FAULT");
}

// ── Screen ────────────────────────────────────────────────────────────────────

static void fmt_time(char* buf, size_t sz, uint32_t secs) {
    if (secs < 60) snprintf(buf, sz, "%u s", secs);
    else           snprintf(buf, sz, "%u:%02u", secs/60, secs%60);
}

void Panel::_drawScreen(HomeNode& node, uint32_t /*now_ms*/,
                        float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(V(x, y), V(x+w, y+h), C(COL_SCREEN_BG), 10.f);
    dl->AddRect      (V(x, y), V(x+w, y+h), Ca(COL_GREEN, 0.4f), 10.f, 0, 2.f);

    ImGui::SetCursorScreenPos(V(x+14.f, y+10.f));
    ImGui::PushStyleColor(ImGuiCol_Text, C(COL_SCREEN_FG));

    auto s = node.status();
    uint32_t lease_secs = (node.n() > 0) ? (1u << node.n()) : 1u;

    char lease_str[32]; fmt_time(lease_str, sizeof(lease_str), lease_secs);
    char n_str[16];     snprintf(n_str, sizeof(n_str), "n=%u", node.n());
    ImGui::Text("LEASE   %-10s %s", lease_str, n_str);

    ImGui::SetCursorScreenPos(V(x+14.f, y+10.f+36.f));
    uint32_t rem_secs = 0;
    if (node.mode() != HomeMode::SAFE && s.lastGrantedN > 0)
        rem_secs = 1u << s.lastGrantedN;
    char rem_str[32]; fmt_time(rem_str, sizeof(rem_str), rem_secs);
    ImGui::Text("REMAINING %s", rem_str);

    ImGui::SetCursorScreenPos(V(x+14.f, y+10.f+72.f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        s.echoedN == 0              ? C(COL_RED)      :
        s.echoedN >= s.lastGrantedN ? C(COL_SCREEN_FG):
                                      C(COL_AMBER));
    if (s.lastRxMs == 0) {
        ImGui::Text("ROBOT   ---");
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

// ── Key switch ────────────────────────────────────────────────────────────────

int Panel::_drawKeySwitch(float cx, float cy, int currentPos,
                          const char* leftLabel,  const char* leftSub,
                          const char* centerLabel,const char* centerSub,
                          const char* rightLabel, const char* rightSub,
                          uint32_t /*now_ms*/, const char* id)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddCircleFilled(V(cx, cy), KS_OUTER, C(COL_DARK_RING));
    dl->AddCircleFilled(V(cx, cy), KS_INNER, C(COL_METAL));

    // Travel arc through 12 o'clock, from 10:30 to 1:30
    dl->PathArcTo(V(cx, cy), (KS_INNER + KS_OUTER)*0.5f,
                  (float)M_PI * 1.25f, (float)M_PI * 1.75f, 20);
    dl->PathStroke(C(COL_DIM), false, 2.f);

    // Tick marks at left (135°std), center (90°std), right (45°std)
    // Screen coords: dx=cos(θ), dy=-sin(θ)  (y flipped)
    struct TickPos { float dx, dy; bool keyOut; };
    TickPos ticks[3] = {
        {-0.707f, -0.707f, false},
        { 0.000f, -1.000f, true },
        { 0.707f, -0.707f, false},
    };
    for (int i = 0; i < 3; i++) {
        float tx1 = cx + ticks[i].dx * KS_INNER;
        float ty1 = cy + ticks[i].dy * KS_INNER;
        float tx2 = cx + ticks[i].dx * KS_OUTER;
        float ty2 = cy + ticks[i].dy * KS_OUTER;
        ImU32  tc = ticks[i].keyOut ? C(COL_TICK_HI) : C(COL_DIM);
        float  tw = ticks[i].keyOut ? 3.f : 2.f;
        dl->AddLine(V(tx1,ty1), V(tx2,ty2), tc, tw);
        if (ticks[i].keyOut)
            dl->AddCircleFilled(V(tx2, ty2), 4.f, C(COL_TICK_HI));
    }

    // Key body pointing at current position
    float kdx = (currentPos == -1) ? -0.707f : (currentPos == +1) ? 0.707f : 0.f;
    float kdy = (currentPos ==  0) ? -1.000f : -0.707f;
    float kx  = cx + kdx * 20.f;
    float ky  = cy + kdy * 20.f;
    dl->AddLine(V(cx, cy), V(kx, ky), C(COL_DARK_RING), 6.f);
    dl->AddLine(V(cx, cy), V(kx, ky), Ca(COL_METAL, 0.4f), 2.f);
    dl->AddCircleFilled(V(kx, ky), 9.f, C(COL_KEY_BODY));
    dl->AddCircle      (V(kx, ky), 9.f, Ca(COL_METAL, 0.5f), 16, 1.5f);
    dl->AddCircle      (V(kx, ky), 4.f, Ca(COL_METAL, 0.4f), 16, 1.f);

    // Position labels
    const float labelGap = KS_OUTER + 18.f;
    struct LabelDef { float dx, dy; const char* label; const char* sub; };
    LabelDef labels[3] = {
        {-0.707f, -0.707f, leftLabel,   leftSub  },
        { 0.000f, -1.000f, centerLabel, centerSub},
        { 0.707f, -0.707f, rightLabel,  rightSub },
    };
    for (auto& l : labels) {
        float lx = cx + l.dx * labelGap;
        float ly = cy + l.dy * labelGap;
        ImVec2 ts = ImGui::CalcTextSize(l.label);
        dl->AddText(V(lx - ts.x*0.5f, ly - 12.f), C(COL_METAL), l.label);
        if (l.sub) {
            ImVec2 ss = ImGui::CalcTextSize(l.sub);
            dl->AddText(V(lx - ss.x*0.5f, ly + 2.f), C(COL_DIM), l.sub);
        }
    }

    // Hit areas: left and right halves
    int clicked = 0;
    char id_L[64], id_R[64];
    snprintf(id_L, sizeof(id_L), "%s_L", id);
    snprintf(id_R, sizeof(id_R), "%s_R", id);
    float bsz = KS_OUTER;
    ImGui::SetCursorScreenPos(V(cx - KS_OUTER - bsz, cy - bsz));
    if (ImGui::InvisibleButton(id_L, V(bsz, bsz*2.f))) clicked = -1;
    ImGui::SetCursorScreenPos(V(cx + KS_OUTER, cy - bsz));
    if (ImGui::InvisibleButton(id_R, V(bsz, bsz*2.f))) clicked = +1;

    return clicked;
}

void Panel::_drawModeKey(HomeNode& node, uint32_t now_ms, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    int curPos = (node.mode() == HomeMode::SINGLE) ? -1 :
                 (node.mode() == HomeMode::AUTO)   ? +1 : 0;
    if (_singlePressedAt && (now_ms - _singlePressedAt) > 300)
        curPos = 0;

    int clicked = _drawKeySwitch(cx, cy, curPos,
        "SINGLE", "momentary", "SAFE", "key out", "AUTO", "latched",
        now_ms, "modekey");

    if (clicked == -1 && _powerOn && !_mushroomDepressed) {
        node.setMode(HomeMode::SINGLE);
        _singlePressedAt = now_ms;
    } else if (clicked == +1 && _powerOn && !_mushroomDepressed) {
        node.setMode(HomeMode::AUTO);
        _singlePressedAt = 0;
    }

    // Center (SAFE) hit area — also clears mushroom latch
    ImGui::SetCursorScreenPos(V(cx - KS_OUTER*0.5f, cy - KS_OUTER - 30.f));
    if (ImGui::InvisibleButton("modekey_C", V(KS_OUTER, 25.f))) {
        node.setMode(HomeMode::SAFE);
        _singlePressedAt = 0;
        _mushroomDepressed = false;
    }

    ImVec2 ts = ImGui::CalcTextSize("MODE");
    dl->AddText(V(cx - ts.x*0.5f, cy + KS_OUTER + 20.f), C(COL_METAL), "MODE");
}

void Panel::_drawProgramKey(HomeNode& node, uint32_t now_ms, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    int curPos = 0;
    if (_upPressedAt   && (now_ms - _upPressedAt)   < 150) curPos = +1;
    if (_downPressedAt && (now_ms - _downPressedAt) < 150) curPos = -1;

    int clicked = _drawKeySwitch(cx, cy, curPos,
        "DOWN", "/2", "KEEP", "key out", "UP", "x2",
        now_ms, "progkey");

    if (clicked == -1 && _powerOn) { node.setN(node.n() - 1); _downPressedAt = now_ms; }
    else if (clicked == +1 && _powerOn) { node.setN(node.n() + 1); _upPressedAt = now_ms; }

    uint32_t lease_secs = (node.n() > 0) ? (1u << node.n()) : 1u;
    char buf[32]; snprintf(buf, sizeof(buf), "2^%u = %u s", node.n(), lease_secs);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(V(cx - ts.x*0.5f, cy + KS_OUTER + 4.f), C(COL_DIM), buf);
    ImVec2 ts2 = ImGui::CalcTextSize("PROGRAM");
    dl->AddText(V(cx - ts2.x*0.5f, cy + KS_OUTER + 20.f), C(COL_METAL), "PROGRAM");
}

void Panel::_drawMushroom(HomeNode& node, float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float R_RING = 74.f;
    const float R_CAP  = 59.f;

    dl->AddCircleFilled(V(cx, cy), R_RING, C(COL_BEZEL));
    for (int i = 0; i < 16; i++) {
        float a0 = (float)(i*2)   * (float)M_PI / 16.f;
        float a1 = (float)(i*2+1) * (float)M_PI / 16.f;
        dl->PathArcTo(V(cx, cy), R_RING - 3.f, a0, a1, 8);
        dl->PathStroke(C(COL_AMBER), false, 4.f);
    }

    bool dep = _mushroomDepressed;
    float oy = dep ? 4.f : 0.f;
    ImVec4 capCol = dep ? ImVec4{0.6f,0.1f,0.07f,1.f} : COL_MUSH_RED;
    dl->AddCircleFilled(V(cx, cy - 4.f + oy), R_CAP, C(capCol));
    dl->AddCircleFilled(V(cx - R_CAP*0.3f, cy - R_CAP*0.6f + oy),
                        R_CAP * 0.2f, Ca(ImVec4{1,1,1,1}, 0.15f));

    ImGui::SetCursorScreenPos(V(cx - R_RING, cy - R_RING));
    if (ImGui::InvisibleButton("mushroom", V(R_RING*2.f, R_RING*2.f))) {
        if (!_mushroomDepressed) {
            node.mushroom();
            _mushroomDepressed = true;
        }
    }

    ImVec2 ts = ImGui::CalcTextSize("UN-ARM");
    dl->AddText(V(cx - ts.x*0.5f, cy + R_RING + 6.f), C(COL_RED), "UN-ARM");
    ImVec2 ts2 = ImGui::CalcTextSize("press \xe2\x80\x94 latches");
    dl->AddText(V(cx - ts2.x*0.5f, cy + R_RING + 22.f), C(COL_DIM),
                "press \xe2\x80\x94 latches");
}

void Panel::_drawOnOff(float cx, float cy)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = 32.f, H = 80.f;
    float x0 = cx - W*0.5f, y0 = cy - H*0.5f;

    dl->AddRectFilled(V(x0-3, y0-3), V(x0+W+3, y0+H+3), C(COL_DARK_RING), 8.f);

    ImVec4 iCol = _powerOn ? COL_GREEN : COL_DIM;
    dl->AddRectFilled(V(x0, y0), V(x0+W, y0+H*0.45f), C(iCol), 4.f);
    ImVec2 ti = ImGui::CalcTextSize("I");
    dl->AddText(V(x0 + W*0.5f - ti.x*0.5f, y0 + H*0.1f), C(COL_DARK_RING), "I");

    ImVec4 oCol = _powerOn ? COL_DIM : COL_METAL;
    dl->AddRectFilled(V(x0, y0+H*0.55f), V(x0+W, y0+H), C(oCol), 4.f);
    ImVec2 to = ImGui::CalcTextSize("O");
    dl->AddText(V(x0 + W*0.5f - to.x*0.5f, y0 + H*0.65f), C(COL_DIM), "O");

    ImGui::SetCursorScreenPos(V(x0-10, y0-10));
    if (ImGui::InvisibleButton("onoff", V(W+20, H+20)))
        _powerOn = !_powerOn;

    ImVec2 ts = ImGui::CalcTextSize("ON/OFF");
    dl->AddText(V(cx - ts.x*0.5f, y0 + H + 8.f), C(COL_METAL), "ON/OFF");
}

// ── Main render ───────────────────────────────────────────────────────────────

void Panel::render(HomeNode& node, uint32_t now_ms)
{
    if (!_powerOn && node.mode() != HomeMode::SAFE)
        node.setMode(HomeMode::SAFE);

    if (node.mode() == HomeMode::SINGLE && _singlePressedAt &&
        (now_ms - _singlePressedAt) > 300) {
        node.setMode(HomeMode::SAFE);
        _singlePressedAt = 0;
    }

    ImGui::SetNextWindowSize(V(PW, PH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(V(0, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(COL_BG));
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(V(10,10), V(PW-10,PH-10), C(COL_DARK_RING), 24.f);
    dl->AddRectFilled(V(22,22), V(PW-22,PH-22), C(COL_BEZEL), 16.f);

    const char* title = "UUV SAFETY  \xe2\x80\x94  HOME UNIT";
    ImVec2 tts = ImGui::CalcTextSize(title);
    dl->AddText(V(PW*0.5f - tts.x*0.5f, 36.f), C(COL_METAL), title);

    _drawLeds     (node, now_ms, PW*0.5f, 78.f);
    _drawOnOff    (95.f, 210.f);
    _drawScreen   (node, now_ms, 290.f, 148.f, 300.f, 138.f);
    _drawModeKey  (node, now_ms, 165.f, 380.f);
    _drawMushroom (node, PW*0.5f, 382.f);
    _drawProgramKey(node, now_ms, PW - 165.f, 380.f);

    ImGui::End();
    ImGui::PopStyleColor();
}
