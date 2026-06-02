// =============================================================================
// Encoder8Unit.h — 8-encoder unit with delta and edge tracking
// =============================================================================
// Wraps the M5Stack 8-Encoder unit (I2C @ 0x41). Tracks:
//   - Per-poll rotation delta (not cumulative count)
//   - Button press edge detection (rising edge = press event)
//   - Scene switch state
//
// DELTA TRACKING:
//   The M5 library returns cumulative encoder counts. We compute the delta
//   per poll() call so the PageManager gets signed steps (+CW, -CCW) that
//   it can apply to CC values. The absolute position is irrelevant — only
//   movement matters.
//
// BUTTON EDGES:
//   Raw button state is level (true = currently pressed). We detect rising
//   edges and latch a "pressed" flag that the consumer must read and clear.
//   This prevents held buttons from firing repeatedly.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <UNIT_8ENCODER.h>
#include "Config.h"

class Encoder8Unit {
public:
    static constexpr uint8_t kNumEncoders = 8;

    Encoder8Unit() = default;

    bool begin(TwoWire* wire);
    void poll();

    // ── Rotation delta ──────────────────────────────────────────────────────
    // Signed steps since last poll(). Positive = clockwise.
    int32_t delta(uint8_t i) const;

    // ── Button edges ────────────────────────────────────────────────────────
    // True if button i was pressed since last clearPress(i).
    bool pressed(uint8_t i) const;
    // Clear the pressed flag after processing. Call after acting on the press.
    void clearPress(uint8_t i);
    // Current level (true = physically held down right now)
    bool buttonHeld(uint8_t i) const;

    // ── Scene switch ────────────────────────────────────────────────────────
    bool switchOn() const { return switch_; }

    // ── LED control (packed 0xRRGGBB) ───────────────────────────────────────
    void setLed(uint8_t ch, uint32_t rgb);
    void setAllLeds(uint32_t rgb);
    void allLedsOff() { setAllLeds(0); }

    bool isPresent() const { return present_; }

private:
    UNIT_8ENCODER dev_;
    int32_t  prevCounts_[kNumEncoders] = {};  // previous cumulative count
    int32_t  deltas_[kNumEncoders]     = {};  // per-poll delta
    bool     btnCurrent_[kNumEncoders] = {};  // current button level
    bool     btnPrev_[kNumEncoders]    = {};  // previous button level
    bool     btnPressed_[kNumEncoders] = {};  // latched rising edge
    bool     switch_  = false;
    bool     present_ = false;
};
