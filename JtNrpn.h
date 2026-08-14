// =============================================================================
// JtNrpn.h — NRPN encode / decode for the JT-8000 control plane (Phase C)
// =============================================================================
// WIRE FORMAT
//   NRPN number == ParamID, by construction (design brief §5.2):
//       CC 99  NRPN MSB  = paramId >> 7      (section)
//       CC 98  NRPN LSB  = paramId & 0x7F    (index within section)
//       CC 6   Data MSB  = value >> 7        (14-bit normalised value)
//       CC 38  Data LSB  = value & 0x7F
//
//   The 14-bit data word carries the NORMALISED value t (0..1) scaled to
//   0..16383. That is ~4 decimal digits of resolution — 128x finer than the
//   7-bit CC path it replaces, and enough that a filter cutoff sweep has no
//   audible stepping.
//
// WHY NOT SEND ENGINEERING UNITS
//   Because both ends already agree on the curve (it is in the generated
//   table). Sending t and letting each end apply its own toEng() means the
//   curve is defined in exactly one place — params.yaml. Sending Hz would mean
//   the sender picks the curve, and a curve change in the yaml would silently
//   break every stored patch.
//
// THE BRIDGE IS GONE
//   Phase C removes JtBridgeTable.h from the ESP32 send path entirely. The UI
//   now holds ParamIDs, so there is nothing to translate: no v1CC -> ParamID
//   hop, no 139-entry lookup, no CCs that alias. The bridge remains only for
//   inbound legacy CC from a DAW, where a v1 CC still has to be mapped in.
//
// EVERY SEND IS SELF-CONTAINED — NO ADDRESS CACHING
//   Each parameter send is 6 CCs: the NRPN address, the 14-bit data, and then
//   an RPN-null park (CC101=127, CC100=127).
//
//   An earlier revision cached the NRPN address and sent only CC6/CC38 on a
//   repeat, halving the traffic. That is WRONG against this firmware, and
//   silently so. MidiParamTransport::handleControlChange treats CC101/CC100 as
//   `Selected::Rpn` — the park DESELECTS the NRPN. A following data-only send
//   is then swallowed by `if (_selected == Selected::Rpn) return true;`, so a
//   pot sweep would move once and then freeze.
//
//   The park is not optional: without it a stray CC6 from any other source
//   lands on whatever parameter was last addressed. Robustness wins — 6 CCs is
//   18 bytes, about 180 us at 1 Mbaud, and a full 140-param dump is still only
//   ~25 ms.
//
//   If you ever reinstate caching, you must also remove the park, and then any
//   stray CC6 on the link corrupts a parameter. Do not.
// =============================================================================
#pragma once

#include <stdint.h>

#include "JtParamModel.h"

namespace JtParam {

// 14-bit data resolution. t (0..1) maps to 0..kNrpnMax inclusive.
static constexpr uint16_t kNrpnMax = 16383;

// ── Value <-> 14-bit wire word ───────────────────────────────────────────────
// Rounds to nearest, so t == 1.0f encodes to exactly 16383 (not 16382) and a
// decode/encode round-trip is stable.
uint16_t normTo14(float t);
float    normFrom14(uint16_t v14);

// ── Reserved NRPN addresses ──────────────────────────────────────────────────
// Not parameters — control-plane commands. These sit in a section index that
// params.yaml never allocates, so they cannot collide with a real ParamID.
//
//   kResyncRequest : "send me every parameter". The Teensy answers with a full
//                    NRPN dump. Phase B' replaced the old CC0 handshake with
//                    this. Sent by the ESP32 at boot.
static constexpr uint16_t kResyncRequest = 0x3F00;

// ── Layer addressing (Performance mode) ──────────────────────────────────────
// Bit 13 of an NRPN number means "layer B". ParamIDs are (section << 7) | index
// with sections 0..17, so bits 13..11 are free by construction and an older
// controller that never sets the bit addresses layer A — the right default —
// with no special case at either end.
//
// The receive CHANNEL is NOT used for this. In the ordinary Layer setup both
// layers are assigned the same channel so one keyboard plays both, at which
// point a channel-addressed edit could no longer name a layer at all.
//
// Note the asymmetry with the resync command: kResyncRequest is 126 << 7, which
// already HAS bit 13 set. It must therefore be matched on the raw address
// BEFORE the layer bit is stripped, or every resync turns into a layer-B write
// to some other parameter.
static constexpr uint16_t kLayerBit = 0x2000;
static constexpr uint16_t kIdMask   = 0x1FFF;

// ── Emitter ──────────────────────────────────────────────────────────────────
// Sink is a raw CC writer — whatever the caller's MIDI layer provides.
// Kept as a function pointer rather than a std::function: no heap, no vtable.
using CcSink = void (*)(uint8_t cc, uint8_t value, uint8_t channel);

class Emitter {
public:
    void begin(CcSink sink, uint8_t channel = 1) {
        sink_    = sink;
        channel_ = channel;
    }

    // Send one parameter as NRPN. `t` is normalised 0..1.
    // `layer` 0 = A, 1 = B — encoded in the address, see kLayerBit.
    void sendParam(uint16_t paramId, float t, uint8_t layer = 0);

    // Send a reserved command (no data payload beyond the value).
    void sendCommand(uint16_t address, uint16_t value14 = 0);

    // Ask the Teensy for a full state dump OF ONE LAYER. The layer rides in the
    // command's data payload, which the address itself has no room for and
    // which was previously ignored — so this costs nothing extra on the wire.
    //
    // Must be re-sent whenever the panel switches which layer it edits: this
    // controller holds ONE value per parameter (it shows one layer at a time),
    // so the displayed values are stale the instant the target changes.
    void requestResync(uint8_t layer = 0) { sendCommand(kResyncRequest, layer); }

private:
    CcSink  sink_    = nullptr;
    uint8_t channel_ = 1;
};

// ── Receiver ─────────────────────────────────────────────────────────────────
// Feeds raw inbound CC bytes; calls onParam() when a complete NRPN 4-tuple has
// arrived. Tolerant of the common orderings: address-then-data, and repeated
// data with a sticky address (a DAW sweeping one NRPN sends 99/98 once, then
// 6/38 repeatedly — the address must persist).
class Receiver {
public:
    using ParamCallback = void (*)(uint16_t paramId, float t, uint8_t layer, void* ctx);

    void begin(ParamCallback cb, void* ctx = nullptr) { cb_ = cb; ctx_ = ctx; }

    // Feed one CC. Returns true if this byte was consumed as part of an NRPN
    // sequence (so the caller does NOT also treat it as a plain CC).
    bool handleCC(uint8_t cc, uint8_t value);

    void reset() { msb_ = lsb_ = dataMsb_ = 0xFF; }

private:
    ParamCallback cb_  = nullptr;
    void*         ctx_ = nullptr;

    uint8_t msb_     = 0xFF;   // 0xFF == not yet received
    uint8_t lsb_     = 0xFF;
    uint8_t dataMsb_ = 0xFF;
};

} // namespace JtParam
