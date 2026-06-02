// =============================================================================
// JtMidiController.ino — JT-8000 Hardware Controller
// =============================================================================
// ARDUINO IDE BOARD SETTINGS:
//   Board:            ESP32S3 Dev Module
//   Flash Size:       16MB
//   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS) — or any with SPIFFS
//   PSRAM:            OPI PSRAM
//   USB CDC On Boot:  Enabled (CDC)
//   CPU Frequency:    240MHz
//   Flash Mode:       QIO
//
// If LittleFS fails with "partition spiffs could not be found", change
// Partition Scheme to one that includes a SPIFFS partition.
// =============================================================================
// Entry point for the ESP32-S3 controller firmware. Initialises all
// subsystems and runs the main loop.
//
// LOOP ARCHITECTURE:
//   Every iteration (~1 ms at 240 MHz):
//     1. Poll M5 units (I2C reads: ~3 ms total for all three units)
//     2. Poll UART MIDI (incoming from Teensy)
//     3. Update PageManager (routes hardware → CC → MIDI out)
//     4. Update LEDs (change-gated, runs at LED_UPDATE_MS cadence)
//     5. Update display (frame-rate limited to DISPLAY_FRAME_MS)
//
// STARTUP SEQUENCE:
//   1. Serial (debug console)
//   2. I2C bus (shared by all M5 units + TCA9554)
//   3. Display (SPI + TCA9554 reset)
//   4. M5 units (Angle8, Encoder8, ByteButton)
//   5. UART MIDI (link to Teensy)
//   6. Patch store (LittleFS)
//   7. PageManager + LEDs
//   8. Request full CC dump from Teensy (CC 0 = 127)
// =============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>
#include "Config.h"
#include "ParamDefs.h"

// Hardware drivers
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"

// MIDI
#include "UartMidi.h"

// USB MIDI — requires Arduino IDE: Tools → USB Mode → "USB-OTG (TinyUSB)"
// CDC serial still works alongside MIDI as a composite USB device.
#if __has_include("USB.h") && __has_include("USBMIDI.h")
    #include "USB.h"
    #include "USBMIDI.h"
    #define JT_HAS_USB_MIDI 1
#else
    #define JT_HAS_USB_MIDI 0
#endif

// UI
#include "PageManager.h"
#include "LedManager.h"
#include "DisplayRenderer.h"

// Patches
#include "PatchStore.h"

// ── Global instances ────────────────────────────────────────────────────────

static Angle8Unit      angle;
static Encoder8Unit    encoder;
static ByteButtonUnit  buttons;
static UartMidi        uartMidi;
#if JT_HAS_USB_MIDI
static USBMIDI         usbMidiDev("JT-8000 Controller");
#endif
static PageManager     pages;
static LedManager      leds;
static DisplayRenderer display;
static PatchStore      patchStore;

// ── Timing gates ────────────────────────────────────────────────────────────
static uint32_t lastDisplayMs = 0;
static uint32_t lastLedMs     = 0;
static uint32_t lastDebugMs   = 0;

// Track page state for display redraw
static PageID   prevPage    = PageID::HOME;
static uint8_t  prevSubPage = 0;
static Scene    prevPotScene = Scene::A;

// ── Callback wiring ─────────────────────────────────────────────────────────

// PageManager sends CCs through this callback → UART + USB MIDI
static void onSendCC(uint8_t cc, uint8_t value) {
    uartMidi.sendCC(cc, value);
    #if JT_HAS_USB_MIDI
    usbMidiDev.controlChange(cc, value, Config::MIDI_CHANNEL);
    #endif
}

// Incoming CCs from Teensy → update PageManager's local CC cache
static void onReceiveCC(uint8_t channel, uint8_t cc, uint8_t value) {
    (void)channel;
    pages.onReceiveCC(cc, value);
}

// ═════════════════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
    // ── 1. Debug console ────────────────────────────────────────────────
    Serial.begin(115200);
    delay(500);

    // Suppress ESP-IDF I2C master NACK log spam. The M5 unit libraries do
    // speculative reads that NACK harmlessly — the working demo has this
    // exact same line. The data flows correctly (pot/encoder values prove
    // the bus works). Core 3.x logs every NACK at ERROR level; core 2.x
    // didn't, which is why the demo appeared clean.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    Serial.println("\n========================================");
    Serial.println("  JT-8000 Hardware Controller");
    Serial.println("  Waveshare ESP32-S3-Touch-LCD-3.5");
    Serial.println("========================================\n");

    // ── 2. I2C bus ──────────────────────────────────────────────────────
    Wire.begin(Config::I2C_SDA, Config::I2C_SCL);
    Wire.setClock(Config::I2C_CLOCK_HZ);
    delay(50);  // let bus settle after init
    Serial.printf("[I2C] SDA=%u SCL=%u @ %lu Hz\n",
                  Config::I2C_SDA, Config::I2C_SCL, Config::I2C_CLOCK_HZ);

    // I2C scan — shows which devices are actually on the bus
    Serial.println("[I2C] Scanning bus...");
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C]   Found device @ 0x%02X", addr);
            if (addr == Config::TCA_ADDR)      Serial.print(" (TCA9554)");
            if (addr == Config::ADDR_ENCODER8)  Serial.print(" (8-Encoder)");
            if (addr == Config::ADDR_ANGLE8)    Serial.print(" (8-Angle)");
            if (addr == Config::ADDR_BYTEBTN)   Serial.print(" (ByteButton)");
            Serial.println();
        }
    }

    // ── 3. Display ──────────────────────────────────────────────────────
    if (display.begin()) {
        Serial.println("[DISPLAY] OK");
    } else {
        Serial.println("[DISPLAY] FAILED — continuing without display");
    }

    // ── 4. M5 units ─────────────────────────────────────────────────────
    // IMPORTANT: Do NOT pass SDA/SCL/freq to any unit begin().
    // Wire is already initialized above. Passing those args causes the
    // M5 library to call Wire.begin() again internally, which in Arduino
    // core 3.x destroys the I2C master bus handle (ESP_ERR_INVALID_STATE).

    if (angle.begin(&Wire)) {
        Serial.printf("[ANGLE8]  Found @ 0x%02X\n", Config::ADDR_ANGLE8);
    } else {
        Serial.println("[ANGLE8]  NOT FOUND");
    }

    if (encoder.begin(&Wire)) {
        Serial.printf("[ENC8]    Found @ 0x%02X\n", Config::ADDR_ENCODER8);
    } else {
        Serial.println("[ENC8]    NOT FOUND");
    }

    if (buttons.begin(&Wire, Config::I2C_SDA, Config::I2C_SCL)) {
        Serial.printf("[BYTEBTN] Found @ 0x%02X\n", Config::ADDR_BYTEBTN);
    } else {
        Serial.println("[BYTEBTN] NOT FOUND");
    }

    // ── 5. UART MIDI ────────────────────────────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.begin();
    uartMidi.setOnReceiveCC(onReceiveCC);
    Serial.printf("[UART-MIDI] TX=%u RX=%u @ %lu baud\n",
                  Config::UART_TX_PIN, Config::UART_RX_PIN, Config::UART_BAUD);
    #endif

    // ── 5b. USB MIDI ────────────────────────────────────────────────────
    #if JT_HAS_USB_MIDI
    usbMidiDev.begin();
    USB.begin();
    Serial.println("[USB-MIDI] Enabled — device: JT-8000 Controller");
    #endif

    // ── 6. Patch store ──────────────────────────────────────────────────
    patchStore.begin();

    // ── 7. PageManager + LEDs ───────────────────────────────────────────
    pages.begin();
    pages.setSendCC(onSendCC);
    leds.begin(angle, encoder, buttons);

    // ── 8. Initial display ──────────────────────────────────────────────
    if (display.isReady()) {
        display.drawPage(pages);
    }

    // ── 9. Request CC dump from Teensy ──────────────────────────────────
    // CC 0 (bank select) with value 127 is the dump request convention.
    // The Teensy responds by sending all patchable CCs back over UART.
    #if JT_ENABLE_UART_MIDI
    delay(200);  // let Teensy finish booting
    uartMidi.sendCC(0, 127);
    Serial.println("[INIT] Sent CC dump request to Teensy");
    #endif

    Serial.println("\n[INIT] Controller ready.\n");
}

// ═════════════════════════════════════════════════════════════════════════════
// LOOP
// ═════════════════════════════════════════════════════════════════════════════

void loop() {
    const uint32_t now = millis();

    // ── 1. Poll all hardware (every loop) ───────────────────────────────
    angle.poll();
    encoder.poll();
    buttons.poll();

    // ── 2. Poll incoming MIDI (every loop) ──────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.poll();
    #endif

    // Poll USB MIDI for incoming CCs from host (updates display/cache)
    #if JT_HAS_USB_MIDI
    {
        midiEventPacket_t pkt;
        while (usbMidiDev.readPacket(&pkt)) {
            uint8_t cin = pkt.header & 0x0F;
            if (cin == 0x0B) {  // Control Change
                uint8_t ch  = (pkt.byte1 & 0x0F) + 1;
                uint8_t cc  = pkt.byte2;
                uint8_t val = pkt.byte3;
                pages.onReceiveCC(cc, val);
                // Also forward to Teensy via UART
                uartMidi.sendCC(cc, val, ch);
            }
        }
    }
    #endif

    // ── 3. Update PageManager (every loop) ──────────────────────────────
    pages.update(angle, encoder, buttons);

    // ── 4. Update LEDs (rate-limited) ──────────────────────────────────
    if (now - lastLedMs >= Config::LED_UPDATE_MS) {
        lastLedMs = now;
        leds.update(pages, angle, encoder, buttons);
    }

    // ── 5. Update display (rate-limited) ────────────────────────────────
    if (now - lastDisplayMs >= Config::DISPLAY_FRAME_MS) {
        lastDisplayMs = now;

        if (display.isReady()) {
            // Full redraw on page/scene/subpage change
            if (pages.currentPage() != prevPage ||
                pages.currentSubPage() != prevSubPage ||
                pages.potScene() != prevPotScene) {

                display.drawPage(pages);
                prevPage     = pages.currentPage();
                prevSubPage  = pages.currentSubPage();
                prevPotScene = pages.potScene();
            } else {
                // Lightweight refresh — only redraws cells whose value changed
                display.refreshValues(pages);
            }
        }
    }

    // ── 6. Debug output (every second) ──────────────────────────────────
    if (now - lastDebugMs >= Config::SERIAL_DEBUG_MS) {
        lastDebugMs = now;

        if (angle.isPresent()) {
            Serial.print("[POT] raw:");
            for (uint8_t i = 0; i < 8; ++i) {
                Serial.printf(" %4u", angle.rawPot(i));
            }
            Serial.print("  cc:");
            for (uint8_t i = 0; i < 8; ++i) {
                Serial.printf(" %3u", angle.ccValue(i));
            }
            Serial.printf("  sw:%s\n", angle.switchOn() ? "B" : "A");
        } else {
            Serial.println("[POT] not present");
        }

        if (encoder.isPresent()) {
            Serial.print("[ENC] delta:");
            for (uint8_t i = 0; i < 8; ++i) {
                Serial.printf(" %+3ld", (long)encoder.delta(i));
            }
            Serial.printf("  sw:%s\n", encoder.switchOn() ? "B" : "A");
        } else {
            Serial.println("[ENC] not present");
        }

        Serial.printf("[PAGE] %s sub=%u\n",
                      pages.activeMapping().tab, pages.currentSubPage());
    }
}
