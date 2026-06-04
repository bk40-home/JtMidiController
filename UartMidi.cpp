// =============================================================================
// UartMidi.cpp
// =============================================================================
#include "UartMidi.h"

// ── MIDI instance on ESP32 UART1 ────────────────────────────────────────────
// HardwareSerial(1) selects UART1 on the ESP32-S3. Pin assignment happens
// in begin() via Serial1.begin(baud, config, rxPin, txPin).
static HardwareSerial TeensySerial(Config::UART_NUM);

// Custom MIDI settings: 115200 baud instead of standard 31250
struct UartMidiSettings : public midi::DefaultSettings {
    static const long BaudRate = Config::UART_BAUD;
};

// Create the MIDI instance bound to the UART
static MIDI_NAMESPACE::MidiInterface<
    MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>
>* midiTeensy = nullptr;

static MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>*
    serialTransport = nullptr;

// Static pointer so the callbacks can reach the singleton instance
static UartMidi* sInstance = nullptr;

// ── Initialisation ──────────────────────────────────────────────────────────

void UartMidi::begin() {
    sInstance = this;

    // Configure UART1 pins before MIDI library takes over
    TeensySerial.begin(Config::UART_BAUD, SERIAL_8N1,
                       Config::UART_RX_PIN, Config::UART_TX_PIN);

    // Create transport and MIDI interface
    serialTransport = new MIDI_NAMESPACE::SerialMIDI<
        HardwareSerial, UartMidiSettings>(TeensySerial);
    midiTeensy = new MIDI_NAMESPACE::MidiInterface<
        MIDI_NAMESPACE::SerialMIDI<HardwareSerial, UartMidiSettings>>(
            *serialTransport);

    // Listen on all channels (Teensy may respond on any channel)
    midiTeensy->begin(MIDI_CHANNEL_OMNI);
    midiTeensy->turnThruOff();  // no echo back to Teensy

    // Register incoming message handlers
    midiTeensy->setHandleControlChange(handleCC);
    midiTeensy->setHandleNoteOn(handleNoteOn);
    midiTeensy->setHandleNoteOff(handleNoteOff);

    Serial.println("[UART-MIDI] Initialised on UART1 @ 115200 baud");
}

// ── Polling ─────────────────────────────────────────────────────────────────

void UartMidi::poll() {
    if (!midiTeensy) return;
    // Read all available MIDI messages this iteration
    while (midiTeensy->read()) { }
}

// ── Outgoing messages ───────────────────────────────────────────────────────

void UartMidi::sendCC(uint8_t cc, uint8_t value, uint8_t channel) {
    if (!midiTeensy) return;

    if (cc <= 127) {
        // Standard MIDI CC — fits in the 7-bit CC number field
        midiTeensy->sendControlChange(cc, value, channel);
        return;
    }

    // ── Extended CC (128+) — wrap in JT-8000 SysEx ──────────────────────
    // CCs above 127 (envelope curves 147-155, perf params 140-146) cannot
    // travel as standard MIDI CC messages because the CC number field is
    // only 7 bits. We wrap them in a lightweight SysEx message that the
    // Teensy's SysExAdapter dispatches to the same handleControlChange()
    // path as normal CCs.
    //
    // Wire format (11 bytes, all body bytes 7-bit safe):
    //   F0 7D 4A 54 00 20 <ch> <cc_hi> <cc_lo> <val> F7
    //
    // cc_hi/cc_lo split mirrors SyxProtocol.h paramId encoding.
    // Keep these constants in sync with SyxProtocol.h on the Teensy side.
    static constexpr uint8_t kMfrId   = 0x7D;  // MMA non-commercial
    static constexpr uint8_t kSubIdJ  = 0x4A;  // 'J'
    static constexpr uint8_t kSubIdT  = 0x54;  // 'T'
    static constexpr uint8_t kDevId   = 0x00;  // default device
    static constexpr uint8_t kMsgType = 0x20;  // EXT_CC

    const uint8_t msg[11] = {
        0xF0,
        kMfrId, kSubIdJ, kSubIdT,
        kDevId,
        kMsgType,
        (uint8_t)(channel & 0x7F),
        (uint8_t)((cc >> 7) & 0x7F),   // cc_hi
        (uint8_t)(cc & 0x7F),           // cc_lo
        (uint8_t)(value & 0x7F),
        0xF7
    };
    midiTeensy->sendSysEx(11, msg, true);
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

void UartMidi::sendSysEx(const uint8_t* data, size_t length) {
    if (!midiTeensy) return;
    midiTeensy->sendSysEx(length, data, true);  // hasBeginEnd = true
}

// ── Incoming message handlers ───────────────────────────────────────────────
// Static callbacks — forward to the singleton's registered callback

void UartMidi::handleCC(byte channel, byte cc, byte value) {
    if (sInstance && sInstance->onCC_) {
        sInstance->onCC_(channel, cc, value);
    }
}

void UartMidi::handleNoteOn(byte channel, byte note, byte velocity) {
    // Future: forward to display for note indicator or voice activity
    (void)channel; (void)note; (void)velocity;
}

void UartMidi::handleNoteOff(byte channel, byte note, byte velocity) {
    (void)channel; (void)note; (void)velocity;
}
