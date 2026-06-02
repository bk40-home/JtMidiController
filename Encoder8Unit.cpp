// =============================================================================
// Encoder8Unit.cpp
// =============================================================================
#include "Encoder8Unit.h"

bool Encoder8Unit::begin(TwoWire* wire) {
    // Match the working demo exactly: pass only (wire, addr).
    // Do NOT pass SDA/SCL/speed — the library calls Wire.begin()
    // internally, and on core 3.3.1 even Wire.begin(8,7,50000) when
    // already initialized destroys and recreates the bus.
    dev_.begin(wire, Config::ADDR_ENCODER8);

    // Probe I2C ACK as ground-truth presence check
    wire->beginTransmission(Config::ADDR_ENCODER8);
    present_ = (wire->endTransmission() == 0);
    if (!present_) return false;

    // Prime the previous counts so the first poll() shows zero delta
    for (uint8_t i = 0; i < kNumEncoders; ++i) {
        prevCounts_[i] = dev_.getEncoderValue(i);
    }
    return true;
}

void Encoder8Unit::poll() {
    if (!present_) return;

    for (uint8_t i = 0; i < kNumEncoders; ++i) {
        // ── Rotation delta ──────────────────────────────────────────────
        const int32_t current = dev_.getEncoderValue(i);
        deltas_[i]     = current - prevCounts_[i];
        prevCounts_[i] = current;

        // ── Button edge detection ───────────────────────────────────────
        btnCurrent_[i] = dev_.getButtonStatus(i);
        // Rising edge: was released, now pressed → latch the event
        if (btnCurrent_[i] && !btnPrev_[i]) {
            btnPressed_[i] = true;
        }
        btnPrev_[i] = btnCurrent_[i];
    }

    switch_ = dev_.getSwitchStatus();
}

int32_t Encoder8Unit::delta(uint8_t i) const {
    return (i < kNumEncoders) ? deltas_[i] : 0;
}

bool Encoder8Unit::pressed(uint8_t i) const {
    return (i < kNumEncoders) ? btnPressed_[i] : false;
}

void Encoder8Unit::clearPress(uint8_t i) {
    if (i < kNumEncoders) btnPressed_[i] = false;
}

bool Encoder8Unit::buttonHeld(uint8_t i) const {
    return (i < kNumEncoders) ? btnCurrent_[i] : false;
}

// ── LED control ─────────────────────────────────────────────────────────────

void Encoder8Unit::setLed(uint8_t ch, uint32_t rgb) {
    if (!present_ || ch >= kNumEncoders) return;
    dev_.setLEDColor(ch, rgb);
}

void Encoder8Unit::setAllLeds(uint32_t rgb) {
    if (!present_) return;
    dev_.setAllLEDColor(rgb);
}
