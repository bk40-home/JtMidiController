// =============================================================================
// UartMidi.h — MIDI over UART link to Teensy 4.1
// =============================================================================
// Sends standard MIDI messages (CC, NoteOn, NoteOff, PitchBend, SysEx) to
// the Teensy synth engine over a UART connection at 115200 baud.
//
// Uses the same FortySevenEffects MIDI library as the Teensy's DIN MIDI port,
// ensuring protocol-level compatibility. The higher baud rate (vs 31250)
// reduces latency for CC bursts during page changes.
//
// WIRING (uses existing MicroDexed Serial1 hardware):
//   ESP32 GPIO 17 (TX) ─── Teensy pin 0 (RX1)
//   ESP32 GPIO 18 (RX) ─── Teensy pin 1 (TX1)
//   ESP32 GND          ─── Teensy GND
//
// The Teensy's midi1 on Serial1 already has all handlers registered.
// The only Teensy-side change: bump midi1 baud rate from 31250 → 115200.
// Standard DIN MIDI devices will no longer work on this port.
//
// RECEIVING:
//   The Teensy may send CCs back (e.g. after a patch load) so we also read
//   incoming MIDI. The onReceiveCC callback notifies the PageManager to
//   update its local CC state (for pickup mode, display, etc).
// =============================================================================
#pragma once

#include <Arduino.h>
#include <MIDI.h>
#include "Config.h"

// Callback type for incoming CCs from the Teensy
using UartCCCallback = void(*)(uint8_t channel, uint8_t cc, uint8_t value);

class UartMidi {
public:
    UartMidi() = default;

    // Initialise UART and MIDI library. Call once in setup().
    void begin();

    // Process any incoming MIDI from the Teensy. Call every loop().
    void poll();

    // ── Outgoing messages ───────────────────────────────────────────────────
    void sendCC(uint8_t cc, uint8_t value, uint8_t channel = Config::MIDI_CHANNEL);
    void sendNoteOn(uint8_t note, uint8_t velocity,
                    uint8_t channel = Config::MIDI_CHANNEL);
    void sendNoteOff(uint8_t note, uint8_t channel = Config::MIDI_CHANNEL);
    void sendPitchBend(int16_t value, uint8_t channel = Config::MIDI_CHANNEL);
    void sendSysEx(const uint8_t* data, size_t length);

    // ── Callback registration ───────────────────────────────────────────────
    // Register a callback for incoming CCs from the Teensy.
    // Used by PageManager to track Teensy-side parameter changes.
    void setOnReceiveCC(UartCCCallback cb) { onCC_ = cb; }

private:
    UartCCCallback onCC_ = nullptr;

    // Internal handlers for incoming MIDI
    static void handleCC(byte channel, byte cc, byte value);
    static void handleNoteOn(byte channel, byte note, byte velocity);
    static void handleNoteOff(byte channel, byte note, byte velocity);
};
