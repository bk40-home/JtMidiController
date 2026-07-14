// =============================================================================
// JtMidiController.ino — JT-8000 Hardware Controller (Phase D)
// =============================================================================
// ARDUINO IDE BOARD SETTINGS:
//   Board:            ESP32S3 Dev Module
//   Flash Size:       16MB
//   Partition Scheme: 16M Flash (3MB APP / 9.9MB FATFS)   <- FFat, not SPIFFS
//   PSRAM:            OPI PSRAM
//   USB CDC On Boot:  Enabled (CDC)
//   CPU Frequency:    240 MHz
//   Flash Mode:       QIO
//
// =============================================================================
// WHAT CHANGED IN PHASE D
//
//   The UI used to be HARDWARE-SHAPED: an 8-cell grid, because the Angle8 has
//   8 pots, with encoders back-filling whatever cells the pots left over.
//   Parameters competed for scarce cells and 43 of the 140 lost — the whole
//   step sequencer, all of Voice Mode, all of Velocity, the FX character
//   controls. They were not awkward to reach; they could not be reached.
//
//   It is now MODEL-SHAPED:
//
//       parameters -> sections -> view;  hardware binds to whatever is shown
//
//   One section = one screen (every section fits 480x320 with room to spare).
//   All 140 parameters are visible and touch-editable. The physical controls
//   bind by DERIVED POLICY, read from the generated table:
//
//       continuous -> pot      (scene A 1..8, spilling to scene B 9..16)
//       select     -> encoder  (turn = step options)
//       toggle     -> encoder switch (push = flip)
//
//   PageManager, PageMappings, DisplayRenderer, ParamFormat and ParamDefs are
//   GONE. The layout is a function of params.yaml.
//
// LOOP ARCHITECTURE (~1 ms):
//   1. Poll M5 units (I2C)
//   2. Poll MIDI in (UART + USB)
//   3. Modal (NameEditor / SelectPopup) OR ViewController update + touch
//   4. PatchManager update
//   5. LEDs (rate-limited)
//   6. Display (rate-limited; an idle frame draws NOTHING)
// =============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>

#include "Config.h"

// Hardware drivers
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"
#include "TouchInput.h"

// MIDI
#include "UartMidi.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#define JT_HAS_USB_MIDI 1

// Parameter model (Phase C) + view (Phase D)
#include "JtParamModel.h"
#include "ViewController.h"

// UI
#include "LedManager.h"
#include "NameEditor.h"
#include "SelectPopup.h"
#include "PatchOverlay.h"
#include "Display.h"

// Patches
#include "PatchStore.h"
#include "PatchManager.h"

#include "PerfMonitor.h"

// ── Global instances ────────────────────────────────────────────────────────

static Angle8Unit      angle;
static Encoder8Unit    encoder;
static ByteButtonUnit  buttons;
static TouchInput      touch;

static UartMidi        uartMidi;
static Adafruit_USBD_MIDI usbMidiTransport;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidiTransport, USBMIDI);

static ViewController  view;
static LedManager      leds;
static NameEditor      nameEditor;
static SelectPopup     selectPopup;
static PatchOverlay    patchOverlay;
static PatchStore      patchStore;
static PatchManager    patchManager;

// Display. ViewController owns the renderer; this is the GFX handle.
static Arduino_GFX*    gfx = nullptr;


// Encoder that drives a modal overlay while it is open (rotate = move the
// highlight, push = commit). Index 0 is the natural navigation wheel.
static constexpr uint8_t ENC_MODAL = 0;

// ── Timing gates ────────────────────────────────────────────────────────────
static uint32_t lastDisplayMs = 0;
static uint32_t lastLedMs     = 0;
static uint32_t lastDebugMs   = 0;

// ── Callback wiring ─────────────────────────────────────────────────────────

// The NRPN emitter's byte sink. JtParam::Emitter owns the protocol; UartMidi is
// now a dumb pipe, so this just forwards raw CC bytes.
//
// The USB mirror is deliberately RAW too: a DAW watching this port sees the
// same NRPN stream the Teensy sees, so it can record and replay parameter
// automation at full 14-bit resolution. The old code mirrored a 7-bit CC here,
// which threw the precision away.
static void onSendCC(uint8_t cc, uint8_t value, uint8_t channel) {
    uartMidi.sendCC(cc, value, channel);
    USBMIDI.sendControlChange(cc, value, channel);
}

// Every inbound CC, from either port, goes to the ViewController, which hands
// it to JtParam::Receiver.
//
// Note there is NO NRPN-cluster filter here any more. There used to be one,
// because the raw NRPN CCs (6/38/96-101) collide with real v1 parameter numbers
// and would corrupt the CC-indexed cache. The store is keyed by ParamID now, so
// there is no cache to corrupt — and the Receiver is the only thing that knows
// which CCs are protocol. Filtering here would break it.
static void onReceiveCC(uint8_t channel, uint8_t cc, uint8_t value) {
    (void)channel;
    view.handleInboundCC(cc, value);
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== JT-8000 Controller (Phase D) ===");

    PerfMonitor::begin();

    // ── 1. I2C ──────────────────────────────────────────────────────────
    Wire.begin(Config::I2C_SDA, Config::I2C_SCL);
    Wire.setClock(Config::I2C_CLOCK_HZ);

    // ── 2. Display ──────────────────────────────────────────────────────
    gfx = jtDisplayBegin();
    if (!gfx) Serial.println("[TFT]     INIT FAILED");

    // ── 3. M5 units ─────────────────────────────────────────────────────
    Serial.println(angle.begin(&Wire)   ? "[ANGLE8]  ok" : "[ANGLE8]  NOT FOUND");
    Serial.println(encoder.begin(&Wire) ? "[ENC8]    ok" : "[ENC8]    NOT FOUND");
    Serial.println(buttons.begin(&Wire, Config::I2C_SDA, Config::I2C_SCL)
                                        ? "[BYTEBTN] ok" : "[BYTEBTN] NOT FOUND");
    Serial.println(touch.begin(&Wire)   ? "[TOUCH]   ok" : "[TOUCH]   NOT FOUND");

    // ── 4. MIDI ─────────────────────────────────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.begin();
    uartMidi.setOnReceiveCC(onReceiveCC);
    #endif
    Serial.println("[USB-MIDI] Adafruit TinyUSB active");

    // ── 5. Patch store (FFat) ───────────────────────────────────────────
    patchStore.begin();

    // ── 6. View + LEDs ──────────────────────────────────────────────────
    view.begin(onSendCC, gfx);
    leds.begin(angle, encoder, buttons);

    // Must come after both view.begin() (needs a constructed store to
    // reference) and patchStore.begin() (needs FFat mounted for the slot-0
    // name lookup inside).
    patchManager.begin(patchStore, view);
    patchManager.setNameEditor(nameEditor);
    patchOverlay.begin(patchManager);

    // ── 7. Resync ───────────────────────────────────────────────────────
    // Reserved NRPN 0x3F00: the Teensy replies with EVERY parameter. This is
    // the ONLY thing that populates the store with the synth's real state —
    // begin() only seeds table defaults.
    #if JT_ENABLE_UART_MIDI
    delay(200);                  // let the Teensy finish booting
    view.requestResync();
    Serial.println("[INIT] NRPN resync requested");
    #endif

    Serial.printf("[INIT] ready — %u parameters, %u sections\n",
                  static_cast<unsigned>(JT::Params::kParamCount),
                  static_cast<unsigned>(JT::Params::kSectionCount));
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    PerfMonitor::loopBegin();
    const uint32_t now = millis();

    // ── 1. Hardware ─────────────────────────────────────────────────────
    angle.poll();
    encoder.poll();
    buttons.poll();
    touch.poll();

    // ── 2. MIDI in ──────────────────────────────────────────────────────
    #if JT_ENABLE_UART_MIDI
    uartMidi.poll();
    #endif

    if (USBMIDI.read() && USBMIDI.getType() == midi::ControlChange) {
        const uint8_t cc  = USBMIDI.getData1();
        const uint8_t val = USBMIDI.getData2();
        const uint8_t ch  = USBMIDI.getChannel();

        // Update our own view of it, AND pass it through to the Teensy (which
        // parses NRPN natively on this port). Both ends stay in step.
        view.handleInboundCC(cc, val);
        uartMidi.sendCC(cc, val, ch);
    }

    // ── 3. Modal overlays take the input, or the view does ───────────────
    if (nameEditor.isActive()) {
        if (touch.tapped()) {
            const NameEditor::Action a =
                nameEditor.handleTouch(touch.tapX(), touch.tapY());
            touch.clearTap();
            if      (a == NameEditor::Action::COMMIT) patchManager.commitNameEdit();
            else if (a == NameEditor::Action::CANCEL) nameEditor.close();
        }
        const int32_t d    = encoder.delta(ENC_MODAL);
        const bool    push = encoder.pressed(ENC_MODAL);
        if (push) encoder.clearPress(ENC_MODAL);
        if (d != 0 || push) {
            const NameEditor::Action a = nameEditor.handleEncoder(d, push);
            if      (a == NameEditor::Action::COMMIT) patchManager.commitNameEdit();
            else if (a == NameEditor::Action::CANCEL) nameEditor.close();
        }
        if (gfx) nameEditor.draw(gfx);

        // The keyboard paints the FULL screen, so its close is the one case
        // that still needs a full repaint — header band and content both.
        // (CANCEL previously repainted nothing and left the keyboard on
        // screen; COMMIT only worked by accident via setPatchName.)
        if (!nameEditor.isActive()) view.forceRedraw();

    } else if (patchOverlay.isActive()) {
        if (touch.tapped()) {
            patchOverlay.handleTouch(static_cast<int16_t>(touch.tapX()),
                                     static_cast<int16_t>(touch.tapY()));
            touch.clearTap();
        }
        const int32_t d    = encoder.delta(ENC_MODAL);
        const bool    push = encoder.pressed(ENC_MODAL);
        if (push) encoder.clearPress(ENC_MODAL);
        if (d != 0 || push) patchOverlay.handleEncoder(d, push);

        // PatchManager owns the arm timeout and banner expiry — it must keep
        // ticking while the overlay is the active surface.
        patchManager.update();

        if (gfx) patchOverlay.draw(gfx);

        // Floating panel, same close-repair as SelectPopup: only the covered
        // rect is repainted. (A RENAME tap closes the overlay and opens the
        // full-screen NameEditor; this repair still runs, and the editor's
        // own exit repair handles the rest.)
        if (!patchOverlay.isActive()) {
            view.repairRect(patchOverlay.coverX(), patchOverlay.coverY(),
                            patchOverlay.coverW(), patchOverlay.coverH());
        }

    } else if (selectPopup.isActive()) {
        if (touch.tapped()) {
            const SelectPopup::Action a =
                selectPopup.handleTouch(touch.tapX(), touch.tapY());
            touch.clearTap();
            if (a == SelectPopup::Action::COMMIT) {
                view.commitSelect(selectPopup.resultParamId(),
                                  selectPopup.resultIndex());
                selectPopup.close();
            } else if (a == SelectPopup::Action::CANCEL) {
                selectPopup.close();
            }
        }
        const int32_t d    = encoder.delta(ENC_MODAL);
        const bool    push = encoder.pressed(ENC_MODAL);
        if (push) encoder.clearPress(ENC_MODAL);
        if (d != 0 || push) {
            const SelectPopup::Action a = selectPopup.handleEncoder(d, push);
            if (a == SelectPopup::Action::COMMIT) {
                view.commitSelect(selectPopup.resultParamId(),
                                  selectPopup.resultIndex());
                selectPopup.close();
            }
        }
        if (gfx) selectPopup.draw(gfx);

        // The popup is a FLOATING PANEL now — it never touched anything
        // outside its own rect, so on close only that rect is repaired.
        // This replaces a full-screen repaint (>100 ms of blocked SPI).
        if (!selectPopup.isActive()) {
            view.repairRect(selectPopup.coverX(), selectPopup.coverY(),
                            selectPopup.coverW(), selectPopup.coverH());
        }

    } else {
        // ── Normal path ─────────────────────────────────────────────────
        view.update(angle, encoder, buttons);
        view.handleTouch(touch);   // whole unit: two-finger paging needs point 1

        // Long-press on any ByteButton parks a PatchOverlay request; collect
        // it here — same pattern (and same tap-latch discard rationale) as
        // the SelectPopup below.
        if (view.takePatchOverlayRequest()) {
            patchOverlay.open();
            touch.clearTap();
        }

        // THIS is the line that was missing for the whole of Phase C.
        // SelectPopup was fully implemented and NEVER INSTANTIATED — the
        // request was parked and nothing ever collected it, which is exactly
        // why tapping a string value did nothing at all.
        const uint16_t req = view.takeSelectPopupRequest();
        if (req) {
            selectPopup.open(req, view.store().getById(req));
            // Discard any latched tap the moment a modal opens. The latch set
            // by the tap that OPENED the popup (or any earlier unconsumed tap
            // — the normal path never reads the latch) would otherwise be
            // delivered to the popup on the next iteration as if it happened
            // while it was open: landing inside the option list committed an
            // arbitrary option, outside cancelled — either way close() ran
            // before the first draw(). That was the "popup never appears"
            // defect. A modal must only see taps latched AFTER it opened.
            touch.clearTap();
        }

        patchManager.update();
    }

    // ── 4. LEDs (rate-limited) ──────────────────────────────────────────
    if (now - lastLedMs >= Config::LED_UPDATE_MS) {
        lastLedMs = now;
        leds.update(view, angle, encoder, buttons);
    }

    // ── 5. Display (rate-limited) ───────────────────────────────────────
    // render() paints only the cells whose value changed. An idle frame issues
    // ZERO draw calls, so this costs nothing when nothing is moving.
    if (now - lastDisplayMs >= Config::DISPLAY_FRAME_MS) {
        lastDisplayMs = now;
        if (!nameEditor.isActive() && !selectPopup.isActive()) view.render();
    }

    // ── 6. Debug (once a second) ────────────────────────────────────────
    if (now - lastDebugMs >= Config::SERIAL_DEBUG_MS) {
        lastDebugMs = now;

        Serial.printf("[VIEW] page=%u sub=%u rows=%u  rx=%lu tx=%lu\n",
                      static_cast<unsigned>(view.page()),
                      static_cast<unsigned>(view.subTab()),
                      static_cast<unsigned>(view.rowCount()),
                      static_cast<unsigned long>(uartMidi.rxCount()),
                      static_cast<unsigned long>(uartMidi.txCount()));
    }

    PerfMonitor::loopEnd();
}
