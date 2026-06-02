// =============================================================================
// ByteButtonUnit.h — 8-button unit with edge detection
// =============================================================================
// Wraps the M5Stack ByteButton unit (I2C @ 0x47). Provides:
//   - Rising-edge press detection (for page navigation)
//   - LED control (0xRRGGBB per channel)
//   - 9 LEDs (8 per-button + 1 indicator)
//
// ByteSwitch buttons are used exclusively for page selection — they never
// send MIDI CCs. LEDs show voice activity (amp envelope brightness, page
// colour) when the synth is connected.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <unit_byte.hpp>
#include "Config.h"

class ByteButtonUnit {
public:
    static constexpr uint8_t kNumButtons    = 8;
    static constexpr uint8_t kNumLeds       = 9;   // 8 + 1 indicator
    static constexpr uint8_t kLedBrightness = 100;  // 0..255

    ByteButtonUnit() = default;

    // Signature matches the working demo: pass SDA/SCL so the library's
    // internal Wire.begin() uses the correct pins (8/7 on this board).
    // Passing (wire) alone causes Wire.begin(0,0,0) → GPIO 0 → bus death.
    bool begin(TwoWire* wire, uint8_t sda, uint8_t scl,
               uint32_t freq = 400000);
    void poll();

    // ── Press edge detection ────────────────────────────────────────────────
    // True if button i was pressed since last clearPress(i).
    bool pressed(uint8_t i) const;
    // Clear the pressed flag after processing.
    void clearPress(uint8_t i);
    // Current level (true = physically held down)
    bool buttonHeld(uint8_t i) const;

    // ── LED control (packed 0xRRGGBB) ───────────────────────────────────────
    void setLed(uint8_t ch, uint32_t rgb);
    void setAllLeds(uint32_t rgb);
    void allLedsOff() { setAllLeds(0); }

    bool isPresent() const { return present_; }

private:
    UnitByte dev_;
    bool     btnCurrent_[kNumButtons] = {};
    bool     btnPrev_[kNumButtons]    = {};
    bool     btnPressed_[kNumButtons] = {};  // latched rising edge
    bool     present_ = false;
};
