// =============================================================================
// Angle8Unit.h — 8-pot unit with analog filtering
// =============================================================================
// Wraps the M5Stack 8-Angle unit (I2C @ 0x43) with ResponsiveAnalogRead
// filtering on each pot. Raw 12-bit I2C values are smoothed and converted
// to 7-bit CC values. Change detection happens at the CC level.
//
// FILTERING RATIONALE:
//   Raw I2C ADC values jitter ±2–5 LSBs even when the pot is stationary.
//   Without filtering, this would cause constant CC spam on the UART link.
//   ResponsiveAnalogRead provides:
//     - Smoothing via exponential moving average
//     - Sleep mode (tighter filter when idle, faster when moving)
//     - Snap-to-value (reduces jitter at rest)
//
// USAGE:
//   1. Call poll() every loop iteration
//   2. Check ccChanged(i) for each pot
//   3. Read ccValue(i) for the filtered 7-bit CC value
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <M5_ANGLE8.h>
#include <ResponsiveAnalogRead.h>
#include "Config.h"

class Angle8Unit {
public:
    static constexpr uint8_t  kNumPots = 8;
    static constexpr uint8_t  kNumLeds = 9;   // 8 pot LEDs + 1 switch LED
    static constexpr uint16_t kPotMax  = Config::POT_ADC_MAX;

    Angle8Unit() = default;

    // Initialise the unit and filters. Returns true if the unit was found.
    // wire pointer kept for interface symmetry; M5_ANGLE8 uses global Wire.
    bool begin(TwoWire* wire);

    // Poll all 8 pots and the scene switch. Call every loop iteration.
    // Internally feeds each raw value through ResponsiveAnalogRead.
    void poll();

    // ── Filtered CC output ──────────────────────────────────────────────────
    // 7-bit CC value (0..127) for pot i, after filtering
    uint8_t ccValue(uint8_t i) const;
    // True if the CC value changed since the last poll() that returned true
    bool    ccChanged(uint8_t i) const;
    // Clear the changed flag for pot i (call after processing the change)
    void    clearChanged(uint8_t i);

    // ── Raw access (for diagnostics) ────────────────────────────────────────
    uint16_t rawPot(uint8_t i) const;

    // ── Scene switch ────────────────────────────────────────────────────────
    bool switchOn() const { return switch_; }

    // ── LED control ─────────────────────────────────────────────────────────
    void setLed(uint8_t ch, uint8_t r, uint8_t g, uint8_t b,
                uint8_t brightness = 40);
    void setAllLeds(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 40);
    void allLedsOff();

    // ── Presence ────────────────────────────────────────────────────────────
    bool isPresent() const { return present_; }

private:
    M5_ANGLE8           dev_;
    ResponsiveAnalogRead* filters_[kNumPots] = {};  // heap-allocated per-pot
    uint8_t             ccValues_[kNumPots]  = {};   // last emitted CC value
    bool                changed_[kNumPots]   = {};   // CC changed since last clear
    uint16_t            rawValues_[kNumPots] = {};   // last raw I2C read
    bool                switch_  = false;
    bool                present_ = false;

    // Convert 12-bit filtered value to 7-bit CC (0..127)
    static uint8_t adcToCC(int value);
};
