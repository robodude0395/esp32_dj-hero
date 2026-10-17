#include "turntable.h"

bool Turntable::begin() {
    Wire.begin(TURNTABLE_SDA_PIN, TURNTABLE_SCL_PIN, TURNTABLE_I2C_FREQ);

    // ── Nunchuck unencrypted init sequence ────────────────────────────────
    // Write 0xF0, 0x55  (disable encryption)
    Wire.beginTransmission(TURNTABLE_I2C_ADDR);
    Wire.write(0xF0);
    Wire.write(0x55);
    if (Wire.endTransmission() != 0) {
        Serial.println("[Turntable] Init step 1 failed — check wiring and pull-ups");
        return false;
    }
    delay(10);

    // Write 0xFB, 0x00
    Wire.beginTransmission(TURNTABLE_I2C_ADDR);
    Wire.write(0xFB);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        Serial.println("[Turntable] Init step 2 failed");
        return false;
    }
    delay(10);

    _initialized = true;
    Serial.println("[Turntable] Initialized OK");
    return true;
}

bool Turntable::read(TurntableState &state) {
    if (!_initialized) return false;

    // ── Pipelined read ────────────────────────────────────────────────────
    // On first call _readRequested is false so we do a normal blocking read.
    // After that we always have a pending request from the previous call,
    // so we fetch immediately (no delay) then fire the next request.
    // This removes the 5ms blocking delay and roughly doubles poll rate.

    if (!_readRequested) {
        // First ever call — send request and wait the required 5ms
        Wire.beginTransmission(TURNTABLE_I2C_ADDR);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0) return false;
        delay(5);
        _readRequested = true;
    }

    // Fetch the 6 bytes prepared by the previous request
    uint8_t buf[6] = {0};
    uint8_t received = Wire.requestFrom((uint8_t)TURNTABLE_I2C_ADDR, (uint8_t)6);
    if (received < 6) {
        _readRequested = false; // reset so next call re-initialises
        return false;
    }
    for (int i = 0; i < 6; i++) buf[i] = Wire.read();

    // Fire the next request immediately — it will be ready by the next call
    Wire.beginTransmission(TURNTABLE_I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        _readRequested = false;
    }

    // ── Parse ─────────────────────────────────────────────────────────────
    // Reference: https://wiibrew.org/wiki/Wiimote/Extension_Controllers/DJ_Hero_(Wii)_Turntable
    //
    // Byte 0: RTT[4:3] | SX[5:0]
    // Byte 1: RTT[2:1] | SY[5:0]
    // Byte 2: RTT[0]   | ED[4:3] | CS[3:0] | RTT[5]
    // Byte 3: ED[2:0]  | LTT[4:0]
    // Byte 4: 0 | 0 | LBR | B- | 0 | B+ | RBR | LTT[5]
    // Byte 5: LBB | 0 | RBG | BE | LBG | RBB | 0 | 0

    state.stickX = buf[0] & 0x3F;
    state.stickY = buf[1] & 0x3F;

    // Right turntable: 6-bit signed
    // Bits: RTT[5] = buf[2] bit0, RTT[4:3] = buf[0] bits[7:6], RTT[2:1] = buf[1] bits[7:6], RTT[0] = buf[2] bit7
    uint8_t rtt_raw =
        ((buf[0] >> 6) & 0x03) << 3 |   // bits [4:3]
        ((buf[1] >> 6) & 0x03) << 1 |   // bits [2:1]
        ((buf[2] >> 7) & 0x01)       |   // bit  [0]
        ((buf[2] & 0x01) << 5);          // bit  [5] (sign)
    state.rightTurntable = decode6BitSigned(rtt_raw);

    // Left turntable: 6-bit signed
    // LTT[4:0] = buf[3] bits[4:0], LTT[5] = buf[4] bit0
    uint8_t ltt_raw =
        (buf[3] & 0x1F)              |   // bits [4:0]
        ((buf[4] & 0x01) << 5);          // bit  [5] (sign)
    state.leftTurntable = decode6BitSigned(ltt_raw);

    // Crossfader slider (4-bit)
    // CS[3:0] occupies bits[4:1] of byte 2 (bit7=RTT[0], bits[6:5]=ED[4:3], bits[4:1]=CS[3:0], bit0=RTT[5])
    state.crossfader = (buf[2] >> 1) & 0x0F;

    // Effect dial (5-bit)
    // ED[4:3] = buf[2] bits[6:5], ED[2:0] = buf[3] bits[7:5]
    state.effectDial =
        ((buf[2] >> 5) & 0x03) << 3 |   // bits [4:3]
        ((buf[3] >> 5) & 0x07);          // bits [2:0]

    // Buttons — hardware active LOW (bit=0 means pressed), invert to get true=pressed
    state.btnMinus    = !(buf[4] & 0x10);
    state.btnPlus     = !(buf[4] & 0x04);
    state.leftRed     = !(buf[4] & 0x20);
    state.rightRed    = !(buf[4] & 0x02);

    state.leftBlue    = !(buf[5] & 0x80);
    state.rightGreen  = !(buf[5] & 0x20);
    state.btnEuphoria = !(buf[5] & 0x10);
    state.leftGreen   = !(buf[5] & 0x08);
    state.rightBlue   = !(buf[5] & 0x04);

    return true;
}
