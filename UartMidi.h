// =============================================================================
// UartMidi.h — raw MIDI byte pipe to the JT-8000 v2 firmware (Phase D)
// =============================================================================
// A DUMB PIPE. It moves CC/note/bend bytes between the ESP32 and the Teensy at
// Config::UART_BAUD (1 Mbaud) and does nothing else.
//
// WHAT WAS REMOVED, AND WHY IT MATTERED
//   The Phase C version owned the NRPN protocol: it took a 7-bit v1 CC number,
//   translated it through JtBridgeTable.h, and embedded the value as v7 * 129.
//   Its own header admitted the cost — "the full-resolution internal model
//   arrives with the Phase D UI rewrite, not here."
//
//   That is now. Keeping it would have been fatal to the point of the rewrite:
//
//     1. THE 7-BIT BOTTLENECK. sendParam() took a uint8_t. Every value was
//        re-quantised to 128 steps at this seam, throwing away the float
//        precision the whole Phase C model exists to preserve.
//
//     2. INBOUND v2 PARAMS WERE DISCARDED. applyNrpn() mapped the ParamID back
//        to a v1 CC and DROPPED anything with no v1 slot (master.volume, and
//        every future parameter). With a ParamID-keyed UI there is no v1 slot
//        to map to, and nothing to drop.
//
//   The protocol now lives in JtNrpn.{h,cpp}, keyed by ParamID, carrying full
//   14-bit normalised values in both directions. This class just moves bytes.
//
// WIRING (unchanged):
//   ESP32 GPIO 17 (TX) — Teensy pin 0 (RX1)
//   ESP32 GPIO 18 (RX) — Teensy pin 1 (TX1)
//   GND — GND
//
// (c) 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <MIDI.h>
#include "Config.h"

// Every inbound CC, raw and unfiltered. The owner hands these to
// JtParam::Receiver, which decodes the NRPN cluster (CC 6/38/98/99/100/101).
//
// Deliberately NOT filtered here: the NRPN CCs collide with real v1 parameter
// numbers (v1 CC 98 was reverb hipass), so only a component that knows the
// protocol can tell them apart. That component is JtNrpn, not this one.
using UartCCCallback = void (*)(uint8_t channel, uint8_t cc, uint8_t value);

class UartMidi {
public:
    UartMidi() = default;

    void begin();

    // Drain inbound MIDI. Call every loop().
    void poll();

    // ── Outgoing ────────────────────────────────────────────────────────────
    // Raw CC. This is the sink JtParam::Emitter writes its NRPN bytes to.
    void sendCC(uint8_t cc, uint8_t value, uint8_t channel = Config::MIDI_CHANNEL);

    void sendNoteOn(uint8_t note, uint8_t velocity,
                    uint8_t channel = Config::MIDI_CHANNEL);
    void sendNoteOff(uint8_t note, uint8_t channel = Config::MIDI_CHANNEL);
    void sendPitchBend(int16_t value, uint8_t channel = Config::MIDI_CHANNEL);

    // ── Inbound ─────────────────────────────────────────────────────────────
    void setOnReceiveCC(UartCCCallback cb) { onCC_ = cb; }

    uint32_t rxCount() const { return rx_; }
    uint32_t txCount() const { return tx_; }

private:
    static void handleCC(byte channel, byte cc, byte value);
    static void handleNoteOn(byte channel, byte note, byte velocity);
    static void handleNoteOff(byte channel, byte note, byte velocity);

    UartCCCallback onCC_ = nullptr;
    uint32_t rx_ = 0;
    uint32_t tx_ = 0;
};
