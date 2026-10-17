#include "bt_hid.h"
#include <BleGamepad.h>

static BleGamepad gamepad("DJ Hero Turntable", "ESP32", 100);
static BleGamepadConfiguration cfg;

void btHidBegin() {
    cfg.setAutoReport(false);
    cfg.setButtonCount(9);
    cfg.setAxesMin(0);
    cfg.setAxesMax(32767);
    cfg.setIncludeXAxis(true);   // stick X
    cfg.setIncludeYAxis(true);   // stick Y
    cfg.setIncludeZAxis(true);   // right turntable velocity
    cfg.setIncludeRzAxis(true);  // left turntable velocity
    cfg.setIncludeRxAxis(true);  // crossfader
    cfg.setIncludeRyAxis(true);  // effect dial
    cfg.setIncludeSlider1(false);
    cfg.setIncludeSlider2(false);
    cfg.setHatSwitchCount(0);
    gamepad.begin(&cfg);
}

bool btHidConnected() {
    return gamepad.isConnected();
}

void btHidSendReport(const HidReport &r) {
    if (!gamepad.isConnected()) return;

    r.btnLeftRed    ? gamepad.press(BUTTON_1) : gamepad.release(BUTTON_1);
    r.btnLeftGreen  ? gamepad.press(BUTTON_2) : gamepad.release(BUTTON_2);
    r.btnLeftBlue   ? gamepad.press(BUTTON_3) : gamepad.release(BUTTON_3);
    r.btnRightRed   ? gamepad.press(BUTTON_4) : gamepad.release(BUTTON_4);
    r.btnRightGreen ? gamepad.press(BUTTON_5) : gamepad.release(BUTTON_5);
    r.btnRightBlue  ? gamepad.press(BUTTON_6) : gamepad.release(BUTTON_6);
    r.btnEuphoria   ? gamepad.press(BUTTON_7) : gamepad.release(BUTTON_7);
    r.btnPlus       ? gamepad.press(BUTTON_8) : gamepad.release(BUTTON_8);
    r.btnMinus      ? gamepad.press(BUTTON_9) : gamepad.release(BUTTON_9);

    gamepad.setX(r.stickX);
    gamepad.setY(r.stickY);
    gamepad.setZ(r.rightTurntable);
    gamepad.setRZ(r.leftTurntable);
    gamepad.setRX(r.crossfader);
    gamepad.setRY(r.effectDial);

    gamepad.sendReport();
}
