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

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#define JT_HAS_USB_MIDI 1

// UI
#include "PageManager.h"
#include "LedManager.h"
#include "DisplayRenderer.h"
#include "TouchInput.h"

// Patches
#include "PatchStore.h"
#include "PatchManager.h"

// Performance monitor
#include "PerfMonitor.h"

#include "NameEditor.h"

// ── Global instances ────────────────────────────────────────────────────────

static Angle8Unit      angle;
static Encoder8Unit    encoder;
static ByteButtonUnit  buttons;
static UartMidi        uartMidi;
static Adafruit_USBD_MIDI usbMidiTransport;
static TouchInput      touch;
static NameEditor      nameEditor;

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidiTransport, USBMIDI);

static PageManager     pages;
static LedManager      leds;
static DisplayRenderer display;
static PatchStore      patchStore;
static PatchManager    patchManager;

// Encoder driving the name-editor fallback while it's open (rotate = move the
// grid highlight, push = activate). Index 0 = the PTCH "SCROLL" wheel — the
// natural navigation encoder. Change freely.
static constexpr uint8_t ENC_EDITOR = 0;

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
    // UART MIDI handles CC > 127 internally (wraps in SysEx)
    uartMidi.sendCC(cc, value);
    // USB MIDI has the same 7-bit CC number limitation — skip extended CCs.
    // Extended params reach JUCE via the Teensy's SysEx echo path instead.
    if (cc <= 127) {
        USBMIDI.sendControlChange(cc, value, Config::MIDI_CHANNEL);
    }
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
    // USB MIDI must register BEFORE the USB stack starts
    usbMidiTransport.setStringDescriptor("JT-8000 Controller");
    USBMIDI.begin(MIDI_CHANNEL_OMNI);

    Serial.begin(115200);
    delay(1000);  // host needs time to enumerate

    // Suppress ESP-IDF I2C master NACK log spam. The M5 unit libraries do
    // speculative reads that NACK harmlessly — the working demo has this
    // exact same line. The data flows correctly (pot/encoder values prove
    // the bus works). Core 3.x logs every NACK at ERROR level; core 2.x
    // didn't, which is why the demo appeared clean.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);


    PerfMonitor::begin();

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

    if (touch.begin(&Wire)) {
        Serial.printf("[TOUCH]   Found @ 0x%02X\n", Config::TOUCH_ADDR);
    } else {
        Serial.println("[TOUCH]   NOT FOUND");
    }

    // ── 5. UART MIDI ────────────────────────────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.begin();
    uartMidi.setOnReceiveCC(onReceiveCC);
    Serial.printf("[UART-MIDI] TX=%u RX=%u @ %lu baud\n",
                  Config::UART_TX_PIN, Config::UART_RX_PIN, Config::UART_BAUD);
    #endif

    Serial.println("[USB-MIDI] Adafruit TinyUSB active");

    // ── 6. Patch store ──────────────────────────────────────────────────
    patchStore.begin();

    // ── 7. PageManager + LEDs ───────────────────────────────────────────
    pages.begin();
    pages.setSendCC(onSendCC);
    pages.setActionCallback(PatchManager::onAction);
    leds.begin(angle, encoder, buttons);

    // PatchManager wires PatchStore + PageManager + the CC send callback.
    // Must come after pages.begin() (needs a constructed PageManager to
    // hold a reference to) and after patchStore.begin() (needs LittleFS
    // already mounted so the slot-0 name lookup in begin() is valid).
    patchManager.begin(patchStore, pages, onSendCC);
    patchManager.setNameEditor(nameEditor);

    // ── 8. Initial display ──────────────────────────────────────────────
    if (display.isReady()) {
        display.drawPage(pages, patchManager);
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

    PerfMonitor::loopBegin();           // ← add as first line

    // ── 0. Name editor (modal) — owns input + display while open ────────
    if (nameEditor.isActive()) {
        // Touch drives the editor.
        touch.poll();
        if (touch.tapped()) {
            NameEditor::Action a =
                nameEditor.handleTouch(touch.tapX(), touch.tapY());
            touch.clearTap();
            if      (a == NameEditor::Action::COMMIT) patchManager.commitNameEdit();
            else if (a == NameEditor::Action::CANCEL) nameEditor.close();
        }

        // Encoder fallback (index ENC_EDITOR): rotate = move, push = select.
        encoder.poll();
        const int32_t d    = encoder.delta(ENC_EDITOR);
        const bool    push = encoder.pressed(ENC_EDITOR);
        if (push) encoder.clearPress(ENC_EDITOR);
        if (d != 0 || push) {
            NameEditor::Action a = nameEditor.handleEncoder(d, push);
            if      (a == NameEditor::Action::COMMIT) patchManager.commitNameEdit();
            else if (a == NameEditor::Action::CANCEL) nameEditor.close();
        }

        // Draw the overlay (rate-limited like the normal display block).
        if (now - lastDisplayMs >= Config::DISPLAY_FRAME_MS) {
            lastDisplayMs = now;
            if (display.isReady()) nameEditor.draw(display.gfx());
        }

        // When the editor just closed, force one full page redraw so the
        // underlying page (e.g. the PTCH list) reappears — closing doesn't
        // change page/sub/scene, so the normal redraw trigger won't fire.
        if (!nameEditor.isActive()) {
            prevPage = PageID::COUNT;   // sentinel ≠ any real page → forces redraw
        }

        PerfMonitor::loopEnd();
        return;  // skip normal processing while editing
    }

    // ── 1. Poll all hardware (every loop) ───────────────────────────────
    angle.poll();
    encoder.poll();
    buttons.poll();
    touch.poll();
    {
        const PageID pg     = pages.currentPage();
        const bool   hasSub = PageMap::hasSubPages(pg);
        const uint8_t subN  = PageMap::subPageCount(pg);

        // Tap edge: tabs first (work on every page); then PTCH rename-on-tap.
        if (touch.tapped()) {
            const uint16_t tx = touch.tapX();
            const uint16_t ty = touch.tapY();

            const TabHit hit = DisplayRenderer::hitTestTab(tx, ty, hasSub, subN);
            if (hit.kind == TabKind::PAGE) {
                pages.selectPage(static_cast<PageID>(hit.index + 1));
            } else if (hit.kind == TabKind::SUBTAB) {
                pages.selectSubPage(hit.index);
            } else if (pg == PageID::PTCH && ty >= 28 && ty < 292) {
                // Tap the PTCH list area (below header, above footer) opens the
                // rename editor for the highlighted slot. Encoder 4 also opens
                // it (wired inside PatchManager::onAction).
                patchManager.openRenameHighlighted();
            }
            touch.clearTap();
        }

        // Content-cell drag-edit, every frame from the live touch state. The
        // handler ignores ENV/SEQ/PTCH/HOME itself, so this is safe on any page.
        const uint8_t cell =
            DisplayRenderer::hitTestCell(touch.x(), touch.y(), hasSub);
        pages.handleContentTouch(cell, touch.y(), touch.touched());
    }

    // ── 2. Poll incoming MIDI (every loop) ──────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.poll();
    #endif

    if (USBMIDI.read()) {
            if (USBMIDI.getType() == midi::ControlChange) {
                uint8_t cc  = USBMIDI.getData1();
                uint8_t val = USBMIDI.getData2();
                uint8_t ch  = USBMIDI.getChannel();
                pages.onReceiveCC(cc, val);
                uartMidi.sendCC(cc, val, ch);
            }
        }

    // ── 3. Update PageManager (every loop) ──────────────────────────────
    pages.update(angle, encoder, buttons);

    // ── 3b. Update Patch Manager (every loop, cheap when PTCH isn't open) ─
    // Only does real work (banner timeout / save-arm timeout checks) —
    // the actual scroll/load/save/import actions arrive via the
    // ActionCallback registered above, fired synchronously from inside
    // pages.update() when an ACTION-type encoder moves or is pushed.
    patchManager.update();

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

                display.drawPage(pages, patchManager);
                prevPage     = pages.currentPage();
                prevSubPage  = pages.currentSubPage();
                prevPotScene = pages.potScene();
            } else {
                // Lightweight refresh — only redraws cells whose value changed
                display.refreshValues(pages, patchManager);
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
    PerfMonitor::loopEnd();             // ← add as last line
}
