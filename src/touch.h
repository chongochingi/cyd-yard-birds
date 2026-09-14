// Written by Alun Morris and Claude Code
#pragma once
#include <Arduino.h>

struct TouchEvent {
    enum Type { NONE, TAP, SWIPE_UP, SWIPE_DOWN, HOLD } type;
    int16_t x, y;   // screen coords (meaningful for TAP and HOLD)
    int16_t dy;      // total finger delta Y (meaningful for swipes)
};

// Initialise touch — runs calibration on first boot (no saved data in NVS).
// Subsequent boots use the saved calibration automatically.
//
// forceRecal exists because a bad calibration makes the panel unusable, so
// there has to be a way to redo it that does not depend on touch. Callers pass
// true when the user asks for one over the serial console.
void touchInit(bool forceRecal = false);

// Call from loop(). Returns the most recent gesture if one completed this tick.
TouchEvent touchProcess();
