#pragma once
#include "../lib/home_node.h"
#include <stdint.h>

// Panel renders the home-unit front panel using Dear ImGui custom drawing.
// It is stateless with respect to the protocol — it only reads HomeNode state
// and issues commands back via setMode() / setN() / mushroom().
//
// Call render() once per frame from the ImGui render loop.

class Panel {
public:
    Panel();

    // Draw the full panel for this frame.
    // node    — protocol state (read + commands)
    // now_ms  — monotonic timestamp for blink timing
    void render(HomeNode& node, uint32_t now_ms);

private:
    // ── Transient UI state (not protocol state) ───────────────────────────────
    uint32_t _singlePressedAt;   // timestamp of last SINGLE click (for auto-revert)
    uint32_t _upPressedAt;       // momentary highlight timing for UP button
    uint32_t _downPressedAt;     // momentary highlight timing for DOWN button
    bool     _mushroomDepressed; // true until mode key is cycled through SAFE
    bool     _powerOn;           // ON/OFF rocker state

    // ── Drawing helpers ───────────────────────────────────────────────────────
    void _drawLeds(HomeNode& node, uint32_t now_ms,
                   float cx, float cy);
    void _drawScreen(HomeNode& node, uint32_t now_ms,
                     float x, float y, float w, float h);
    void _drawModeKey(HomeNode& node, uint32_t now_ms,
                      float cx, float cy);
    void _drawProgramKey(HomeNode& node, uint32_t now_ms,
                         float cx, float cy);
    void _drawMushroom(HomeNode& node,
                       float cx, float cy);
    void _drawOnOff(float cx, float cy);

    // ── Key switch sub-renderer (shared between MODE and PROGRAM) ─────────────
    // Returns which position was clicked: -1 = left, 0 = none, 1 = right
    int _drawKeySwitch(float cx, float cy, int currentPos,
                       const char* leftLabel,  const char* leftSub,
                       const char* centerLabel,const char* centerSub,
                       const char* rightLabel, const char* rightSub,
                       uint32_t now_ms, const char* id);
};
