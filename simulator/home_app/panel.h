#pragma once
#include "../lib/home_node.h"
#include <stdint.h>

class Panel {
public:
    explicit Panel(float scale = 1.f);
    void render(HomeNode& node, uint32_t now_ms);

private:
    bool     _mushroomDepressed;
    bool     _powerOn;
    bool     _modeKeyIn;   // physical key inserted → buttons active; out → locked at HOLD
    bool     _progKeyIn;   // physical key inserted → programming allowed; out → locked
    float    _scale;
    uint32_t _lastRobotRxMs;
    uint32_t _robotFlashMs;

    void _drawLeds      (HomeNode& node, uint32_t now_ms, float cx, float cy);
    void _drawScreen    (HomeNode& node, uint32_t now_ms, float x, float y, float w, float h);
    void _drawModeKey   (HomeNode& node, float cx, float cy);
    void _drawProgramKey(HomeNode& node, float cx, float cy);
    void _drawMushroom  (HomeNode& node, float cx, float cy);
    void _drawOnOff     (float cx, float cy);

    // shared helper: draws a lock/key toggle and returns true when clicked
    bool _drawKeyLock(const char* id, float cx, float cy, bool keyIn, const char* label);
};
