// =============================================================================
// JtNrpn.cpp — see JtNrpn.h for the wire format and rationale.
// =============================================================================
#include "JtNrpn.h"

namespace JtParam {
namespace {

// Standard MIDI CC numbers for the NRPN transaction. These are exactly the CCs
// params.yaml forbids a parameter from binding to (generator RESERVED_CCS), so
// a real parameter can never collide with the transport.
constexpr uint8_t CC_NRPN_MSB = 99;
constexpr uint8_t CC_NRPN_LSB = 98;
constexpr uint8_t CC_DATA_MSB = 6;
constexpr uint8_t CC_DATA_LSB = 38;
constexpr uint8_t CC_RPN_MSB  = 101;   // park: deselects NRPN after each edit
constexpr uint8_t CC_RPN_LSB  = 100;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Value <-> 14-bit
// ─────────────────────────────────────────────────────────────────────────────

uint16_t normTo14(float t) {
    if (!(t == t)) return 0;                 // NaN -> 0
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return kNrpnMax;
    // Round to nearest so the round-trip normFrom14(normTo14(t)) is stable to
    // within half a LSB, and t == 1.0f lands exactly on kNrpnMax.
    return static_cast<uint16_t>(t * static_cast<float>(kNrpnMax) + 0.5f);
}

float normFrom14(uint16_t v14) {
    if (v14 > kNrpnMax) v14 = kNrpnMax;
    return static_cast<float>(v14) / static_cast<float>(kNrpnMax);
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter
// ─────────────────────────────────────────────────────────────────────────────

void Emitter::sendParam(uint16_t paramId, float t, uint8_t layer) {
    if (!sink_) return;

    // Layer B is the same address with bit 13 set (see kLayerBit). The firmware
    // folds a layer-B write to a SHARED parameter (fx.*, seq.*) back onto its
    // single slot, so this end never has to know which parameters bank.
    const uint16_t addr = static_cast<uint16_t>(
        (paramId & kIdMask) | (layer ? kLayerBit : 0u));

    const uint8_t  msb = JT::Params::nrpnMsb(addr);
    const uint8_t  lsb = JT::Params::nrpnLsb(addr);
    const uint16_t v   = normTo14(t);

    // Address.
    sink_(CC_NRPN_MSB, msb, channel_);
    sink_(CC_NRPN_LSB, lsb, channel_);

    // Data. The firmware applies CC6 IMMEDIATELY as a coarse 7-bit value and
    // then CC38 refines it to 14 bits, so the parameter briefly passes through
    // a 7-bit approximation of its final value. Harmless (both land inside one
    // control-rate block) and it is what lets an MSB-only controller work at
    // all — but it is why the two must always be sent as a pair, in order.
    sink_(CC_DATA_MSB, static_cast<uint8_t>((v >> 7) & 0x7F), channel_);
    sink_(CC_DATA_LSB, static_cast<uint8_t>(v & 0x7F),        channel_);

    // Park. Deselects NRPN so a stray CC6 from any other source cannot land on
    // this parameter. See the header: this is why the address is NOT cached.
    sink_(CC_RPN_MSB, 127, channel_);
    sink_(CC_RPN_LSB, 127, channel_);
}

void Emitter::sendCommand(uint16_t address, uint16_t value14) {
    if (!sink_) return;

    sink_(CC_NRPN_MSB, static_cast<uint8_t>((address >> 7) & 0x7F), channel_);
    sink_(CC_NRPN_LSB, static_cast<uint8_t>(address & 0x7F),        channel_);
    sink_(CC_DATA_MSB, static_cast<uint8_t>((value14 >> 7) & 0x7F), channel_);
    sink_(CC_DATA_LSB, static_cast<uint8_t>(value14 & 0x7F),        channel_);
    sink_(CC_RPN_MSB, 127, channel_);
    sink_(CC_RPN_LSB, 127, channel_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Receiver
//
// The address is STICKY: a sender that sweeps one parameter emits 99/98 once
// and then 6/38 repeatedly. Clearing the address after each complete message
// would drop every value after the first.
// ─────────────────────────────────────────────────────────────────────────────

bool Receiver::handleCC(uint8_t cc, uint8_t value) {
    switch (cc) {
        case CC_NRPN_MSB:
            msb_ = value & 0x7F;
            dataMsb_ = 0xFF;      // new address invalidates any half-built value
            return true;

        case CC_NRPN_LSB:
            lsb_ = value & 0x7F;
            dataMsb_ = 0xFF;
            return true;

        case CC_DATA_MSB:
            // Hold it — the value is not complete until the LSB lands.
            dataMsb_ = value & 0x7F;
            return true;

        case CC_DATA_LSB: {
            // Complete only if we have a full address AND a held MSB.
            if (msb_ == 0xFF || lsb_ == 0xFF || dataMsb_ == 0xFF) return true;

            const uint16_t raw = JT::Params::idFromNrpn(msb_, lsb_);
            const uint16_t v   = static_cast<uint16_t>(
                                     (static_cast<uint16_t>(dataMsb_) << 7) |
                                     (value & 0x7F));

            // Strip the layer out of the address before it is treated as a
            // ParamID — an unmasked id matches nothing in the table and the
            // whole of layer B would look like unknown traffic.
            const uint8_t  layer = (raw & kLayerBit) ? 1u : 0u;
            const uint16_t id    = static_cast<uint16_t>(raw & kIdMask);

            if (cb_) cb_(id, normFrom14(v), layer, ctx_);

            // Address stays latched (sticky) for the next value on this param.
            // Only the data MSB is consumed.
            dataMsb_ = 0xFF;
            return true;
        }

        default:
            return false;   // not NRPN traffic — caller handles it as plain CC
    }
}

} // namespace JtParam
