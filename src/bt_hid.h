#pragma once
#include <Arduino.h>

// ── HID Report ────────────────────────────────────────────────────────────
// Buttons (active high):
//   B1=LeftRed B2=LeftGreen B3=LeftBlue
//   B4=RightRed B5=RightGreen B6=RightBlue
//   B7=Euphoria B8=Plus B9=Minus
//
// Absolute axes (int16, 0–32767):
//   stickX, stickY    — analog stick
//   crossfader        — 0=left, 32767=right
//   effectDial        — 0..32767
//
// Turntable axes (int16, 0–32767, centre=16383):
//   rightTurntable    — velocity mapped around centre
//   leftTurntable

struct HidReport {
    bool btnLeftRed, btnLeftGreen, btnLeftBlue;
    bool btnRightRed, btnRightGreen, btnRightBlue;
    bool btnEuphoria, btnPlus, btnMinus;

    int16_t stickX, stickY;
    int16_t crossfader, effectDial;
    int16_t rightTurntable, leftTurntable;
};

void btHidBegin();
bool btHidConnected();
void btHidSendReport(const HidReport &report);
