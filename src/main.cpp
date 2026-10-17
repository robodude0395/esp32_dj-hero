#include <Arduino.h>
#include "turntable.h"
#include "bt_hid.h"

// ── Scaling helpers ───────────────────────────────────────────────────────

// 6-bit stick (0..63, centre ~32) → int16 (0..32767, centre 16383)
static int16_t scaleStick(uint8_t val) {
    return (int16_t)((uint32_t)val * 32767 / 63);
}

// 4-bit crossfader (0..15) → int16 (0..32767)
static int16_t scaleCrossfader(uint8_t val) {
    return (int16_t)((uint32_t)val * 32767 / 15);
}

// 5-bit effect dial (0..31) → int16 (0..32767)
static int16_t scaleEffectDial(uint8_t val) {
    return (int16_t)((uint32_t)val * 32767 / 31);
}

// ── Turntable scaling ─────────────────────────────────────────────────────
// Send velocity centred on 16383 (still=centre, CW=above, CCW=below).
// Discard spikes above MAX_INPUT (noise from the encoder).
// Sensitivity scales how far from centre each velocity unit reaches.

#define TURNTABLE_SENSITIVITY 4500
#define TURNTABLE_MAX_INPUT   28    // discard spikes; max real value is ~15

static int8_t s_lastRightAim = 0;
static int8_t s_lastLeftAim  = 0;

static int16_t scaleTurntable(int8_t velocity, int8_t &lastAim) {
    if (abs(velocity) >= TURNTABLE_MAX_INPUT) {
        velocity = lastAim;  // spike — use last known good value
    } else {
        lastAim = velocity;
    }
    int32_t val = 16383 + (int32_t)velocity * TURNTABLE_SENSITIVITY;
    return (int16_t)constrain(val, 0, 32767);
}

static HidReport buildReport(const TurntableState &ts) {
    HidReport r = {};

    r.btnLeftRed    = ts.leftRed;
    r.btnLeftGreen  = ts.leftGreen;
    r.btnLeftBlue   = ts.leftBlue;
    r.btnRightRed   = ts.rightRed;
    r.btnRightGreen = ts.rightGreen;
    r.btnRightBlue  = ts.rightBlue;
    r.btnEuphoria   = ts.btnEuphoria;
    r.btnPlus       = ts.btnPlus;
    r.btnMinus      = ts.btnMinus;

    r.stickX         = scaleStick(ts.stickX);
    r.stickY         = scaleStick(ts.stickY);
    r.rightTurntable = scaleTurntable(ts.rightTurntable, s_lastRightAim);
    r.leftTurntable  = scaleTurntable(ts.leftTurntable,  s_lastLeftAim);
    r.crossfader     = scaleCrossfader(ts.crossfader);
    r.effectDial     = scaleEffectDial(ts.effectDial);
    r.crossfader     = scaleCrossfader(ts.crossfader);
    r.effectDial     = scaleEffectDial(ts.effectDial);

    return r;
}

// ── Globals ───────────────────────────────────────────────────────────────
static Turntable turntable;
static uint32_t  lastDebugMs  = 0;
static uint32_t  lastReportMs = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== DJ Hero Turntable → BLE HID ===");
    Serial.println("WIRING: 4.7kΩ pull-ups on SDA (GPIO21) and SCL (GPIO22) to 3.3V");

    btHidBegin();
    Serial.println("BLE advertising as 'DJ Hero Turntable' — pair on your PC");

    if (!turntable.begin()) {
        Serial.println("[WARN] Turntable not detected — will retry in loop");
    }
}

// The I2C read runs every loop (~1200Hz). The hardware gives clean velocity
// data at that rate. We peak-hold the strongest reading between BLE reports
// so a fast scratch is never lost, and send to Dolphin at a fixed ~130Hz.
#define REPORT_INTERVAL_MS 8

static uint32_t lastReport = 0;
static int8_t   rightPeak  = 0;  // strongest RTT since last BLE report
static int8_t   leftPeak   = 0;

void loop() {
    uint32_t now = millis();

    TurntableState ts;
    bool ok = turntable.read(ts);

    if (ok) {
        // Peak-hold the strongest velocity between reports
        if (abs(ts.rightTurntable) > abs(rightPeak)) rightPeak = ts.rightTurntable;
        if (abs(ts.leftTurntable)  > abs(leftPeak))  leftPeak  = ts.leftTurntable;
    } else {
        static uint32_t lastRetryMs = 0;
        if (now - lastRetryMs > 1000) {
            lastRetryMs = now;
            turntable.begin();
        }
    }

    // Send a BLE report at a fixed rate using the peak-held values
    if (ok && (now - lastReport >= REPORT_INTERVAL_MS)) {
        lastReport = now;
        TurntableState report = ts;
        report.rightTurntable = rightPeak;
        report.leftTurntable  = leftPeak;
        btHidSendReport(buildReport(report));
        rightPeak = 0;  // reset for next window
        leftPeak  = 0;
    }
}
