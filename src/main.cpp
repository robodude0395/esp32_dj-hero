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

    return r;
}

// ── Globals ───────────────────────────────────────────────────────────────
static Turntable turntable;

// ═══════════════════════════════════════════════════════════════════════════
//  DUAL-CORE DESIGN
//
//  Core 0:  turntableTask()  — polls I2C continuously at full speed and
//                              peak-holds the strongest turntable velocity.
//  Core 1:  loop()           — reads the shared snapshot, builds the HID
//                              report, and sends it over BLE.
//
//  The two cores share one TurntableState. Because core 0 writes it while
//  core 1 reads it, we guard access with a mutex (a FreeRTOS semaphore) so
//  one core never sees a half-updated struct.
// ═══════════════════════════════════════════════════════════════════════════

// Shared state written by the turntable task, read by the main loop.
static TurntableState g_sharedState;         // latest full reading
static int8_t         g_rightPeak = 0;       // strongest RTT since last consume
static int8_t         g_leftPeak  = 0;       // strongest LTT since last consume
static bool           g_haveData  = false;   // true once a valid read happened

// Mutex protecting the shared state above.
static SemaphoreHandle_t g_stateMutex = nullptr;

// Handle for the turntable task (optional, lets us inspect/stop it later).
static TaskHandle_t g_turntableTaskHandle = nullptr;

// ── Turntable polling task (runs on core 0) ────────────────────────────────
static void turntableTask(void *param) {
    // Each task has its own infinite loop. It never returns.
    for (;;) {
        TurntableState ts;
        bool ok = turntable.read(ts);

        if (ok) {
            // Take the mutex before touching shared data. portMAX_DELAY means
            // "wait forever until it's free" — the wait is microseconds here.
            if (xSemaphoreTake(g_stateMutex, portMAX_DELAY) == pdTRUE) {
                g_sharedState = ts;

                // Peak-hold: remember the strongest velocity seen since the
                // main loop last consumed the data, so fast flicks survive.
                if (abs(ts.rightTurntable) > abs(g_rightPeak)) g_rightPeak = ts.rightTurntable;
                if (abs(ts.leftTurntable)  > abs(g_leftPeak))  g_leftPeak  = ts.leftTurntable;

                g_haveData = true;

                // Always release the mutex as soon as you're done.
                xSemaphoreGive(g_stateMutex);
            }
        } else {
            // Turntable not responding — try to re-init once a second.
            static uint32_t lastRetryMs = 0;
            if (millis() - lastRetryMs > 1000) {
                lastRetryMs = millis();
                turntable.begin();
            }
        }

        // Yield briefly so the idle task / watchdog on core 0 can run.
        // vTaskDelay takes ticks; pdMS_TO_TICKS converts milliseconds to ticks.
        // 1ms still lets us poll ~1000Hz, which we proved is plenty.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== DJ Hero Turntable → BLE HID (dual-core) ===");
    Serial.println("WIRING: 4.7kΩ pull-ups on SDA (GPIO21) and SCL (GPIO22) to 3.3V");

    btHidBegin();
    Serial.println("BLE advertising as 'DJ Hero Turntable' — pair on your PC");

    if (!turntable.begin()) {
        Serial.println("[WARN] Turntable not detected — will retry in task");
    }

    // Create the mutex before starting the task that uses it.
    g_stateMutex = xSemaphoreCreateMutex();

    // Spawn the turntable task on core 0.
    //   xTaskCreatePinnedToCore(function, name, stackBytes, param,
    //                           priority, &handle, coreID)
    // Core 1 is where Arduino's setup()/loop() and the BLE stack live, so we
    // pin the I2C polling to core 0 to keep it isolated from BLE hiccups.
    xTaskCreatePinnedToCore(
        turntableTask,          // task function
        "turntable",            // human-readable name (for debugging)
        4096,                   // stack size in bytes
        nullptr,                // parameter passed to the task (unused)
        2,                      // priority (higher = more urgent; 0 is idle)
        &g_turntableTaskHandle, // handle out-param
        0                       // core 0
    );
}

// Main loop runs on core 1. It consumes the shared snapshot and sends BLE.
#define REPORT_INTERVAL_MS 8
static uint32_t lastReport = 0;

void loop() {
    uint32_t now = millis();

    if (now - lastReport < REPORT_INTERVAL_MS) {
        return;  // not time to send yet
    }

    // Copy the shared state out under the mutex, then release it quickly so
    // the turntable task isn't blocked while we do the slower BLE send.
    TurntableState snapshot;
    bool haveData = false;

    if (xSemaphoreTake(g_stateMutex, portMAX_DELAY) == pdTRUE) {
        if (g_haveData) {
            snapshot = g_sharedState;
            // Overwrite the velocity fields with the peak-held values.
            snapshot.rightTurntable = g_rightPeak;
            snapshot.leftTurntable  = g_leftPeak;
            // Reset peaks so the next window starts fresh.
            g_rightPeak = 0;
            g_leftPeak  = 0;
            haveData = true;
        }
        xSemaphoreGive(g_stateMutex);
    }

    // BLE send happens OUTSIDE the mutex — no need to hold up core 0 for it.
    if (haveData) {
        lastReport = now;
        btHidSendReport(buildReport(snapshot));
    }
}
