// =============================================================================
// LedManager.h — Context-aware LED feedback
// =============================================================================
// Drives LEDs on all three units based on the current page state:
//
//   ByteButton LEDs:
//     - Active page button lit in page colour, others dim
//     - Future: voice activity (amp envelope brightness from Teensy)
//
//   Angle8 LEDs:
//     - Active pots: page colour at brightness proportional to CC value
//     - Inactive/unmapped pots: off
//     - Seeking pots (pickup mode): dim pulsing to indicate "seeking"
//
//   Encoder8 LEDs:
//     - Active encoders: page colour
//     - Sub-page selector: active sub-page bright, inactive dim
//     - Toggle encoders: bright = on, dim = off
//
// All LED writes are change-gated — only issued when state actually changes.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "PageDefs.h"
#include "PageManager.h"
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"
#include "ColorUtils.h"

class LedManager {
public:
    LedManager() = default;

    // Initialise all LEDs to off
    void begin(Angle8Unit& angle, Encoder8Unit& encoder,
               ByteButtonUnit& buttons);

    // Update LEDs based on current page state. Call at LED_UPDATE_MS cadence.
    void update(const PageManager& pages,
                Angle8Unit& angle, Encoder8Unit& encoder,
                ByteButtonUnit& buttons);

private:
    // Cached states to avoid redundant I2C writes
    uint32_t prevBtnLed_[8]  = {};
    uint32_t prevEncLed_[8]  = {};
    uint32_t prevPotLed_[8]  = {};
    PageID   prevPage_       = PageID::HOME;
    uint8_t  prevSubPage_    = 0;

    void updateByteButtons(const PageManager& pages, ByteButtonUnit& buttons);
    void updateEncoderLeds(const PageManager& pages, Encoder8Unit& encoder);
    void updatePotLeds(const PageManager& pages, Angle8Unit& angle);
};
