// =============================================================================
// TCA9554.h — TCA9554 I/O expander driver (I2C)
// =============================================================================
// Minimal driver for the TCA9554 on the Waveshare ESP32-S3-Touch-LCD-3.5.
// Controls LCD reset via EXIO1.
//
// IMPORTANT: Do NOT read registers during begin(). If I2C isn't fully stable
// the reads return 0xFF, corrupting the shadow state and causing all
// subsequent writes to clobber other expander pins.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>

class TCA9554 {
public:
    explicit TCA9554(uint8_t addr = 0x20) : addr_(addr) {}

    // Store wire pointer only — no I2C transactions during begin().
    // The bus must already be initialized via Wire.begin() before calling this.
    bool begin(TwoWire* wire = &Wire) {
        wire_ = wire;
        // Verify the device is present
        wire_->beginTransmission(addr_);
        return (wire_->endTransmission() == 0);
    }

    // Set pin direction: OUTPUT = 0 in config register, INPUT = 1
    void pinMode1(uint8_t pin, uint8_t mode) {
        if (pin > 7) return;
        if (mode == OUTPUT) {
            configReg_ &= ~(1 << pin);
        } else {
            configReg_ |= (1 << pin);
        }
        writeReg(kRegConfig, configReg_);
    }

    // Write a single output pin (0 or 1)
    void write1(uint8_t pin, uint8_t val) {
        if (pin > 7) return;
        if (val) {
            outputReg_ |= (1 << pin);
        } else {
            outputReg_ &= ~(1 << pin);
        }
        writeReg(kRegOutput, outputReg_);
    }

private:
    static constexpr uint8_t kRegOutput = 0x01;
    static constexpr uint8_t kRegConfig = 0x03;  // 1=input, 0=output

    uint8_t  addr_;
    TwoWire* wire_      = nullptr;
    uint8_t  outputReg_ = 0xFF;  // power-on default: all high
    uint8_t  configReg_ = 0xFF;  // power-on default: all inputs
    
    void writeReg(uint8_t reg, uint8_t value) {
        wire_->beginTransmission(addr_);
        wire_->write(reg);
        wire_->write(value);
        wire_->endTransmission();
    }
};
