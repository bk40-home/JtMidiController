// =============================================================================
// Config.h — JT-8000 Controller central configuration
// =============================================================================
// All hardware pin assignments, timing constants, and feature flags live here.
// Change wiring or tuning in ONE place — nothing is hard-coded elsewhere.
// =============================================================================
#pragma once

#include <Arduino.h>

namespace Config {

// ─────────────────────────────────────────────────────────────────────────────
// I2C bus — shared by display expander + all M5 units via 1-to-3 Grove hub
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t  I2C_SDA       = 8;
static constexpr uint8_t  I2C_SCL       = 7;
// 50 kHz: proven working in the Waveshare M5UnitsDemo with all three units
// + TCA9554 expander on the same bus. Do not increase without retesting.
static constexpr uint32_t I2C_CLOCK_HZ  = 50000;

// ─────────────────────────────────────────────────────────────────────────────
// UART to Teensy — MIDI protocol at 115200 baud over Serial1
//
// WIRING:
//   ESP32 GPIO 17 (TX) ─── Teensy pin 0 (RX1)
//   ESP32 GPIO 18 (RX) ─── Teensy pin 1 (TX1)
//   ESP32 GND          ─── Teensy GND
//
// Uses the existing MicroDexed Serial1 hardware — same pins that carry DIN
// MIDI. The Teensy midi1 instance already has all handlers registered; the
// only firmware change is bumping the baud rate from 31250 to 115200.
// Standard DIN MIDI devices will no longer work on this port.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t  UART_TX_PIN   = 17;
static constexpr uint8_t  UART_RX_PIN   = 18;
static constexpr uint32_t UART_BAUD     = 31250;
// ESP32-S3 UART peripheral index (0 = debug console, 1 = Teensy link)
static constexpr uint8_t  UART_NUM      = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Display — ST7796S 480×320 SPI on Waveshare ESP32-S3-Touch-LCD-3.5
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int8_t   DISP_DC       = 3;
static constexpr int8_t   DISP_CS       = -1;   // directly wired (no CS needed)
static constexpr int8_t   DISP_RST      = -1;   // reset via TCA9554 expander
static constexpr int8_t   DISP_SCLK     = 5;
static constexpr int8_t   DISP_MOSI     = 1;
static constexpr int8_t   DISP_MISO     = 2;
static constexpr int8_t   DISP_BL       = 6;    // backlight enable
static constexpr uint16_t DISP_WIDTH    = 320;
static constexpr uint16_t DISP_HEIGHT   = 480;

// TCA9554 I/O expander (display reset control)
static constexpr uint8_t  TCA_ADDR      = 0x20;
static constexpr uint8_t  TCA_RST_PIN   = 1;    // expander pin driving LCD reset

// ─────────────────────────────────────────────────────────────────────────────
// I2C device addresses — M5 units
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t  ADDR_ANGLE8   = 0x43;
static constexpr uint8_t  ADDR_ENCODER8 = 0x41;
static constexpr uint8_t  ADDR_BYTEBTN  = 0x47;

// ─────────────────────────────────────────────────────────────────────────────
// Timing — main loop task cadences
// ─────────────────────────────────────────────────────────────────────────────
// I2C polling runs every loop for low latency. These gates control display
// refresh, serial debug, and LED update rates.
static constexpr uint32_t DISPLAY_FRAME_MS  = 33;   // ~30 fps display refresh
static constexpr uint32_t SERIAL_DEBUG_MS   = 1000;  // debug print cadence
static constexpr uint32_t LED_UPDATE_MS     = 200;   // LED refresh (~5 Hz, was 20ms)
// LED writes at 50kHz I2C take ~2ms each × 25 LEDs = 50ms per update.
// At 20ms interval, the bus was permanently saturated with LED writes,
// blocking pot/encoder reads. 200ms gives ~80% bus time to polling.

// ─────────────────────────────────────────────────────────────────────────────
// Pot filtering — ResponsiveAnalogRead tuning
// ─────────────────────────────────────────────────────────────────────────────
// ADC resolution from Angle8 unit (12-bit, 0..4095)
static constexpr uint16_t POT_ADC_MAX       = 4095;
static constexpr uint16_t POT_ADC_BITS      = 12;
// Snap multiplier: lower = smoother but laggier (0.01 is very smooth)
static constexpr float    POT_SNAP_MULT     = 0.01f;
// Sleep: when the value stops changing, reduce noise by tightening the filter
static constexpr bool     POT_SLEEP_ENABLE  = true;
// Pickup threshold: how close the pot must be to the target CC before it
// "picks up" and starts sending. In CC units (0..127). 2 = tight, 5 = loose.
static constexpr uint8_t  PICKUP_THRESHOLD  = 3;
// Snap on decisive move: when seeking, if the pot moves ≥ this many CC units
// away from its reading at seek-start, bypass pickup and snap to the pot's
// current value. 0 = disabled (pure pickup behaviour).
static constexpr uint8_t  PICKUP_SNAP_CC      = 8;

// ─────────────────────────────────────────────────────────────────────────────
// MIDI channel — default channel for outgoing CC/notes to Teensy
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t  MIDI_CHANNEL      = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Patch storage
// ─────────────────────────────────────────────────────────────────────────────
// LittleFS partition on ESP32 flash. Patches stored as binary blobs.
static constexpr uint8_t  MAX_PATCHES       = 128;
static constexpr uint16_t CC_STATE_SIZE     = 160;   // match Teensy ccState[] size

static constexpr uint32_t PERF_REPORT_MS    = 2000;  // perf+heap report cadence



// ─────────────────────────────────────────────────────────────────────────────
// Feature flags — compile-time enable/disable
// ─────────────────────────────────────────────────────────────────────────────
#ifndef JT_ENABLE_UART_MIDI
    #define JT_ENABLE_UART_MIDI 1
#endif
#ifndef JT_ENABLE_BLE_MIDI
    #define JT_ENABLE_BLE_MIDI  0
#endif
#ifndef JT_ENABLE_USB_MIDI
    #define JT_ENABLE_USB_MIDI  0
#endif

} // namespace Config
