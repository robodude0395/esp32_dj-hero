#pragma once
#include <Arduino.h>
#include <Wire.h>

// DJ Hero (Wii) Turntable — I2C nunchuck extension, address 0x52
// Wire pull-ups: connect 4.7kΩ resistors from SDA (GPIO21) and SCL (GPIO22) to 3.3V
// Data format documented at: https://wiibrew.org/wiki/Wiimote/Extension_Controllers/DJ_Hero_(Wii)_Turntable

#define TURNTABLE_I2C_ADDR  0x52
#define TURNTABLE_SDA_PIN   21
#define TURNTABLE_SCL_PIN   22
#define TURNTABLE_I2C_FREQ  400000

struct TurntableState {
    // Analog stick (6-bit, 0–63, center ~32)
    uint8_t stickX;
    uint8_t stickY;

    // Turntable velocity — 6-bit signed (-32 to +31), positive = CW
    int8_t  rightTurntable;
    int8_t  leftTurntable;

    // Crossfader slider (4-bit, 0 = full left, 15 = full right)
    uint8_t crossfader;

    // Effect dial (5-bit, 0–31)
    uint8_t effectDial;

    // Buttons — true = pressed (already inverted from active-low hardware)
    bool btnPlus;       // B+
    bool btnMinus;      // B-
    bool btnEuphoria;   // BE (Euphoria / big button)
    bool leftRed;
    bool leftGreen;
    bool leftBlue;
    bool rightRed;
    bool rightGreen;
    bool rightBlue;
};

class Turntable {
public:
    // Call once in setup()
    bool begin();

    // Call in loop() — returns true if a fresh reading was obtained
    bool read(TurntableState &state);

private:
    bool     _initialized   = false;
    bool     _readRequested = false;

    // Decode a 6-bit signed integer stored in bits [5:0] of a byte
    // The sign bit is bit 5 (value 32), so range is -32 to +31
    static int8_t decode6BitSigned(uint8_t raw6) {
        raw6 &= 0x3F;
        if (raw6 & 0x20) {          // bit 5 set → negative
            return (int8_t)(raw6 - 64);
        }
        return (int8_t)raw6;
    }
};
