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

    // First poll is a BASELINE, not an event source. The prev arrays default
    // to zero/false, so whatever the very first I2C read returns — including
    // a not-yet-ready device reporting every button down — would register as
    // eight simultaneous rising edges and latch eight phantom presses. That
    // was the "popup opens itself at startup" defect: the phantom press on
    // the encoder bound to OSC1 PITCH OFFSET opened its option list.
    if (!seeded_) {
        for (uint8_t i = 0; i < kNumEncoders; ++i) {
            prevCounts_[i] = dev_.getEncoderValue(i);
            btnPrev_[i]    = dev_.getButtonStatus(i);
        }
        switch_ = dev_.getSwitchStatus();
        seeded_ = true;
        return;
    }

    for (uint8_t i = 0; i < kNumEncoders; ++i) {
        // ── Rotation delta ──────────────────────────────────────────────
        // The counter moves 2 per detent. Divide with remainder CARRY —
        // truncation toward zero works for both directions, and the leftover
        // count persists so two half-detent polls still add up to one step.
        const int32_t current = dev_.getEncoderValue(i);
        residual_[i]  += current - prevCounts_[i];
        prevCounts_[i] = current;
        deltas_[i]     = residual_[i] / Config::ENC_COUNTS_PER_DETENT;
        residual_[i]  -= deltas_[i] * Config::ENC_COUNTS_PER_DETENT;

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
