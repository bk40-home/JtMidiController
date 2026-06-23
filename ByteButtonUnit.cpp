// =============================================================================
// ByteButtonUnit.cpp
// =============================================================================
#include "ByteButtonUnit.h"

bool ByteButtonUnit::begin(TwoWire* wire, uint8_t sda, uint8_t scl,
                           uint32_t freq) {
    // Match the working demo: pass all args through so the library's
    // internal Wire.begin(sda, scl, freq) uses the CORRECT pins.
    dev_.begin(wire, Config::ADDR_BYTEBTN, sda, scl, freq);

    // Probe I2C ACK for presence
    wire->beginTransmission(Config::ADDR_BYTEBTN);
    present_ = (wire->endTransmission() == 0);
    if (!present_) return false;

    // Manual LED mode + baseline brightness on all channels
    dev_.setLEDShowMode(BYTE_LED_USER_DEFINED);
    for (uint8_t i = 0; i < kNumLeds; ++i) {
        dev_.setLEDBrightness(i, kLedBrightness);
    }
    setAllLeds(0);  // start dark

    // Read initial button state so the first poll() doesn't see false
    // rising edges.  Without this, any button that reads as "on" during
    // boot (e.g. momentary contact bounce) triggers an unwanted page jump.
    //
    // Also seed pressStartMs_ to "now" for every channel — without this, a
    // button already-held at boot has pressStartMs_ left at its
    // zero-initialised default, so poll()'s long-press check
    // (now - pressStartMs_ >= LONG_PRESS_MS) becomes true as soon as
    // millis() exceeds the threshold, even though no real press-and-hold
    // ever happened.
    const uint32_t bootMs = millis();
    for (uint8_t i = 0; i < kNumButtons; ++i) {
        btnCurrent_[i]   = readPressed(i);
        btnPrev_[i]      = btnCurrent_[i];
        pressStartMs_[i] = bootMs;
    }

    return true;
}

void ByteButtonUnit::poll() {
    if (!present_) return;

    const uint32_t now = millis();

    for (uint8_t i = 0; i < kNumButtons; ++i) {
        btnCurrent_[i] = readPressed(i);

        if (btnCurrent_[i] && !btnPrev_[i]) {
            // Rising edge: released → pressed. Start the hold timer and
            // latch the short-press event. Whether the short-press is
            // ACTED ON is the caller's call — if this turns into a long
            // press, PageManager checks longPressed() first and skips it.
            btnPressed_[i]        = true;
            pressStartMs_[i]      = now;
            longFiredThisHold_[i] = false;  // fresh hold — allow it to fire once
        } else if (btnCurrent_[i] && btnPrev_[i] && !longFiredThisHold_[i]) {
            // Still held, and this hold hasn't fired its long-press yet —
            // gate on longFiredThisHold_, NOT longLatched_, so a caller
            // that promptly clears longLatched_ (the documented usage
            // pattern, mirroring clearPress()) doesn't cause an immediate
            // re-latch on the very next poll() while still held.
            if (now - pressStartMs_[i] >= Config::LONG_PRESS_MS) {
                longLatched_[i]        = true;
                longFiredThisHold_[i]  = true;
            }
        } else if (!btnCurrent_[i] && btnPrev_[i]) {
            // Falling edge: released. Clear both flags so the next press
            // on this button starts a fresh hold and can long-press again.
            longLatched_[i]        = false;
            longFiredThisHold_[i]  = false;
        }

        btnPrev_[i] = btnCurrent_[i];
    }
}

bool ByteButtonUnit::pressed(uint8_t i) const {
    return (i < kNumButtons) ? btnPressed_[i] : false;
}

void ByteButtonUnit::clearPress(uint8_t i) {
    if (i < kNumButtons) btnPressed_[i] = false;
}

bool ByteButtonUnit::buttonHeld(uint8_t i) const {
    return (i < kNumButtons) ? btnCurrent_[i] : false;
}

bool ByteButtonUnit::longPressed(uint8_t i) const {
    return (i < kNumButtons) ? longLatched_[i] : false;
}

void ByteButtonUnit::clearLongPress(uint8_t i) {
    if (i < kNumButtons) longLatched_[i] = false;
}

// ── LED control ─────────────────────────────────────────────────────────────

void ByteButtonUnit::setLed(uint8_t ch, uint32_t rgb) {
    if (!present_ || ch >= kNumLeds) return;
    dev_.setRGB888(ch, rgb);
}

void ByteButtonUnit::setAllLeds(uint32_t rgb) {
    if (!present_) return;
    for (uint8_t i = 0; i < kNumLeds; ++i) {
        dev_.setRGB888(i, rgb);
    }
}
