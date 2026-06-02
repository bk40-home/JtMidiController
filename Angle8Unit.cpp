// =============================================================================
// Angle8Unit.cpp
// =============================================================================
#include "Angle8Unit.h"

bool Angle8Unit::begin(TwoWire* /*wire*/) {
    // M5_ANGLE8 only has begin(addr) — uses global Wire internally.
    // Does NOT call Wire.begin(), so no bus reinit risk.
    present_ = dev_.begin(Config::ADDR_ANGLE8);
    if (!present_) return false;

    // Create one ResponsiveAnalogRead per pot. Heap-allocated once at boot.
    // Pin argument is unused — we feed external values via update(rawValue).
    for (uint8_t i = 0; i < kNumPots; ++i) {
        filters_[i] = new ResponsiveAnalogRead(
            0,                          // pin (unused with external values)
            Config::POT_SLEEP_ENABLE,   // sleep when stable
            Config::POT_SNAP_MULT       // snap multiplier
        );
        filters_[i]->setAnalogResolution(kPotMax + 1);  // 12-bit range
    }

    return true;
}

void Angle8Unit::poll() {
    if (!present_) return;

    // Read all 8 pots and the switch in one pass
    for (uint8_t i = 0; i < kNumPots; ++i) {
        rawValues_[i] = dev_.getAnalogInput(i, _12bit);

        // Feed raw I2C value into the filter
        filters_[i]->update(rawValues_[i]);

        // Convert filtered value to 7-bit CC
        const uint8_t newCC = adcToCC(filters_[i]->getValue());

        // Flag change only when the CC value actually moves
        if (newCC != ccValues_[i]) {
            ccValues_[i] = newCC;
            changed_[i]  = true;
        }
    }

    switch_ = dev_.getDigitalInput();
}

uint8_t Angle8Unit::ccValue(uint8_t i) const {
    return (i < kNumPots) ? ccValues_[i] : 0;
}

bool Angle8Unit::ccChanged(uint8_t i) const {
    return (i < kNumPots) ? changed_[i] : false;
}

void Angle8Unit::clearChanged(uint8_t i) {
    if (i < kNumPots) changed_[i] = false;
}

uint16_t Angle8Unit::rawPot(uint8_t i) const {
    return (i < kNumPots) ? rawValues_[i] : 0;
}

uint8_t Angle8Unit::adcToCC(int value) {
    // M5 8-Angle reads HIGH at counterclockwise, LOW at clockwise.
    // Invert so clockwise = increasing CC value (0..127).
    if (value <= 0)    return 127;
    if (value >= (int)kPotMax) return 0;
    return 127 - (uint8_t)((uint32_t)value * 127 / kPotMax);
}

// ── LED control ─────────────────────────────────────────────────────────────

static inline uint32_t packRgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Angle8Unit::setLed(uint8_t ch, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t brightness) {
    if (!present_ || ch >= kNumLeds) return;
    dev_.setLEDColor(ch, packRgb(r, g, b), brightness);
}

void Angle8Unit::setAllLeds(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t brightness) {
    if (!present_) return;
    const uint32_t packed = packRgb(r, g, b);
    for (uint8_t i = 0; i < kNumLeds; ++i) {
        dev_.setLEDColor(i, packed, brightness);
    }
}

void Angle8Unit::allLedsOff() {
    if (!present_) return;
    for (uint8_t i = 0; i < kNumLeds; ++i) {
        dev_.setLEDColor(i, 0u, 0);
    }
}
