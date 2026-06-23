// =============================================================================
// ByteButtonUnit.h — 8-button unit with edge detection
// =============================================================================
// Wraps the M5Stack ByteButton unit (I2C @ 0x47). Provides:
//   - Rising-edge press detection (for page navigation)
//   - Long-press detection (for opening the Patch Manager page)
//   - LED control (0xRRGGBB per channel)
//   - 9 LEDs (8 per-button + 1 indicator)
//
// ByteSwitch buttons are used exclusively for page selection — they never
// send MIDI CCs. LEDs show voice activity (amp envelope brightness, page
// colour) when the synth is connected.
//
// LONG-PRESS:
//   Held >= Config::LONG_PRESS_MS latches longPressed(i), once per physical
//   hold, the same way pressed() latches a short-press rising edge. The
//   caller (PageManager) is responsible for checking longPressed() BEFORE
//   acting on pressed() — crossing the threshold should suppress the
//   ordinary short-press action, not fire both.
//
//   "Once per hold" is enforced even though clearLongPress() is expected to
//   be called immediately after the caller acts on the event (same pattern
//   as clearPress()) — clearing the public flag must NOT let poll() re-arm
//   while the button is still held down. An internal-only flag tracks
//   whether this hold has already fired, separate from the public flag the
//   caller clears; only releasing the button resets it.
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

    // ── Long-press detection ────────────────────────────────────────────────
    // True once when button i has been held continuously for at least
    // Config::LONG_PRESS_MS. Latches like pressed() — call clearLongPress(i)
    // after acting on it. Clears automatically on release.
    bool longPressed(uint8_t i) const;
    void clearLongPress(uint8_t i);

    // ── LED control (packed 0xRRGGBB) ───────────────────────────────────────
    void setLed(uint8_t ch, uint32_t rgb);
    void setAllLeds(uint32_t rgb);
    void allLedsOff() { setAllLeds(0); }

    bool isPresent() const { return present_; }

private:
    // CONFIRMED via diagnostic build (boot-time read with nothing pressed
    // returned 1 on every channel): this hardware's switch register is
    // ACTIVE-LOW. dev_.getSwitchStatus(i) returns 0 when a button is
    // physically pressed and 1 when idle — the opposite of what a naive
    // "!= 0 means pressed" read would assume. This helper is the ONLY
    // place that raw-to-logical translation happens; every other method
    // in this file already correctly treats its own bool state as
    // "true == pressed", so centralising the inversion here means the
    // edge-detection logic in poll() needed no other changes.
    bool readPressed(uint8_t i) { return dev_.getSwitchStatus(i) == 0; }

    UnitByte dev_;
    bool     btnCurrent_[kNumButtons]      = {};
    bool     btnPrev_[kNumButtons]         = {};
    bool     btnPressed_[kNumButtons]      = {};  // latched short-press rising edge
    uint32_t pressStartMs_[kNumButtons]    = {};  // millis() at rising edge
    bool     longLatched_[kNumButtons]     = {};  // PUBLIC-facing long-press flag,
                                                    // consumer clears via clearLongPress()
    bool     longFiredThisHold_[kNumButtons] = {}; // INTERNAL: true once this hold has
                                                    // crossed the threshold, regardless
                                                    // of whether the consumer has since
                                                    // cleared longLatched_. Only reset on
                                                    // release. This is what actually makes
                                                    // the long-press one-shot-per-hold —
                                                    // longLatched_ alone isn't enough,
                                                    // since the consumer is expected to
                                                    // clear it immediately after acting.
    bool     present_ = false;
};
