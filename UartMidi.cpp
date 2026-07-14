// =============================================================================
// UartMidi.cpp — raw MIDI byte pipe. See UartMidi.h.
// =============================================================================
// (c) 2026 Kris Bishop — MIT licensed.
// =============================================================================
#include "UartMidi.h"

// -----------------------------------------------------------------------------
// UART + MIDI parser plumbing. Unchanged from Phase C: HardwareSerial(1),
// FortySevenEffects over a SerialMIDI transport, singleton trampolines.
// Only the protocol layer above it was removed.
// -----------------------------------------------------------------------------
static HardwareSerial TeensySerial(Config::UART_NUM);

struct UartMidiSettings : public midi::DefaultSettings {
    static const long BaudRate = Config::UART_BAUD;   // 1 Mbaud — see Config.h
};

static MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>*
    serialTransport = nullptr;
static MIDI_NAMESPACE::MidiInterface<
    MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>>*
    midiTeensy = nullptr;

static UartMidi* sInstance = nullptr;

void UartMidi::begin() {
    sInstance = this;

    // Explicit pin mapping — the S3's UART1 defaults are not ours.
    TeensySerial.begin(Config::UART_BAUD, SERIAL_8N1,
                       Config::UART_RX_PIN, Config::UART_TX_PIN);

    serialTransport = new MIDI_NAMESPACE::SerialMIDI<
        HardwareSerial, UartMidiSettings>(TeensySerial);
    midiTeensy = new MIDI_NAMESPACE::MidiInterface<
        MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>>(
            *serialTransport);

    midiTeensy->setHandleControlChange(handleCC);
    midiTeensy->setHandleNoteOn(handleNoteOn);
    midiTeensy->setHandleNoteOff(handleNoteOff);
    midiTeensy->begin(MIDI_CHANNEL_OMNI);

    // CRITICAL: thru would bounce the Teensy's broadcast straight back at it —
    // an echo loop its origin-suppression cannot see, because the reflection
    // looks like *us* sending. Same rule on both ends.
    midiTeensy->turnThruOff();

    Serial.printf("[UART-MIDI] byte pipe on UART1 @ %lu baud\r\n",
                  (unsigned long)Config::UART_BAUD);
}

void UartMidi::poll() {
    if (!midiTeensy) return;
    while (midiTeensy->read()) { }
}

// ── Outgoing ────────────────────────────────────────────────────────────────

void UartMidi::sendCC(uint8_t cc, uint8_t value, uint8_t channel) {
    if (!midiTeensy) return;
    midiTeensy->sendControlChange(cc, value, channel);
    ++tx_;
}

void UartMidi::sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
    if (!midiTeensy) return;
    midiTeensy->sendNoteOn(note, velocity, channel);
}

void UartMidi::sendNoteOff(uint8_t note, uint8_t channel) {
    if (!midiTeensy) return;
    midiTeensy->sendNoteOff(note, 0, channel);
}

void UartMidi::sendPitchBend(int16_t value, uint8_t channel) {
    if (!midiTeensy) return;
    midiTeensy->sendPitchBend(value, channel);
}

// ── Inbound ─────────────────────────────────────────────────────────────────
//
// Everything is forwarded RAW. This class does not know which CCs are NRPN and
// must not guess: the NRPN cluster numbers collide with real v1 parameter CCs
// (v1 CC 98 was reverb hipass), so only JtParam::Receiver — which tracks the
// protocol state — can tell a stray CC 98 from an NRPN LSB.

void UartMidi::handleCC(byte channel, byte cc, byte value) {
    if (!sInstance) return;
    ++sInstance->rx_;
    if (sInstance->onCC_) {
        sInstance->onCC_(static_cast<uint8_t>(channel),
                         static_cast<uint8_t>(cc),
                         static_cast<uint8_t>(value));
    }
}

void UartMidi::handleNoteOn(byte, byte, byte)  { /* controller sends only */ }
void UartMidi::handleNoteOff(byte, byte, byte) { /* controller sends only */ }
