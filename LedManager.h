// =============================================================================
// LedManager.h — M5 unit LED feedback (Phase E)
// =============================================================================
// The LEDs answer one question: WHICH CONTROLS ARE LIVE RIGHT NOW?
//
// In Phase E the answer is simple, because the binding is: the visible rows of
// the current sub-tab, in order. Pots take rows 0..7, encoders take rows 8..15.
// A control with no row is DARK, so an unlit knob genuinely does nothing rather
// than doing something invisible.
//
//   ByteButton i  -> page i. Bright = current page.
//   Pot i         -> lit if row i exists. Red while pickup has not caught up.
//   Encoder i     -> lit if row i+8 exists. A toggle shows its STATE.
//
// All writes are change-gated: a static panel costs zero I2C traffic.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "Config.h"
#include "ColorUtils.h"
#include "ViewController.h"
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"

class LedManager {
public:
    void begin(Angle8Unit& angle, Encoder8Unit& encoder, ByteButtonUnit& buttons);

    void update(const ViewController& view,
                Angle8Unit& angle, Encoder8Unit& encoder,
                ByteButtonUnit& buttons);

private:
    uint32_t prevBtn_[8] = {};
    uint32_t prevEnc_[8] = {};
    uint32_t prevPot_[8] = {};
};
