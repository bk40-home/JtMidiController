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
    return true;
}

void ByteButtonUnit::poll() {
    if (!present_) return;

    for (uint8_t i = 0; i < kNumButtons; ++i) {
        btnCurrent_[i] = (dev_.getSwitchStatus(i) != 0);

        // Rising edge: released → pressed
        if (btnCurrent_[i] && !btnPrev_[i]) {
            btnPressed_[i] = true;
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
