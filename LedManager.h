// =============================================================================
// LedManager.h — M5 unit LED feedback (Phase E)
// =============================================================================
// The LEDs answer two questions: WHICH CONTROLS ARE LIVE, and WHICH ROW EACH
// ONE DRIVES.
//
// Identity is a COLOUR: physical control i lights in JtHw::kSlotColour[i]
// (HwPalette.h), and RowList paints the bound row's chip in the same colour —
// match knob to chip by eye, no counting. A control with no row is DARK, so an
// unlit knob genuinely does nothing rather than doing something invisible.
//
//   ByteButton i  -> page mode: page palette, bright = current page.
//                    toggle mode: slot colour, brightness = toggle state.
//   Pot i         -> slot colour when bound; the unit's brightness input
//                    carries the pickup state (dim = seeking, bright = live).
//   Encoder i     -> full slot colour when bound, dark otherwise. No state:
//                    the table has no Toggle rows on Encoder control.
//
// All writes are budgeted per unit per update (kLedWriteBudget): the units
// drop back-to-back writes, so over-budget slots stay dirty and retry.
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
