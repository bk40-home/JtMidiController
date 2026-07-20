// =============================================================================
// JtParamStore.cpp — see JtParamStore.h for the contract.
// =============================================================================
#include "JtParamStore.h"

namespace JtParam {

using JT::Params::kParams;
using JT::Params::kParamCount;

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void Store::begin() {
    for (size_t i = 0; i < kParamCount; ++i) {
        values_[i] = defaultNorm(kParams[i]);
    }
    // Defaults are not "changes" — nothing to push to the Teensy yet. The
    // resync request at boot is what synchronises the two sides.
    for (size_t w = 0; w < kWords; ++w) dirty_[w] = 0;
    dirtyCount_ = 0;
    scanCursor_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dirty set
// ─────────────────────────────────────────────────────────────────────────────

void Store::markDirty_(uint16_t ordinal) {
    const uint32_t bit = 1u << (ordinal & 31);
    uint32_t& word = dirty_[ordinal >> 5];
    if (!(word & bit)) {          // count each ordinal once, not once per write
        word |= bit;
        ++dirtyCount_;
    }
}

uint16_t Store::takeDirty() {
    if (dirtyCount_ == 0) return kNoOrdinal;

    // Resume from where the last call left off, so draining N dirty ordinals is
    // O(kParamCount) total across the whole drain, not O(N * kParamCount).
    for (uint16_t i = 0; i < kN; ++i) {
        const uint16_t o = static_cast<uint16_t>((scanCursor_ + i) % kN);
        const uint32_t bit = 1u << (o & 31);
        uint32_t& word = dirty_[o >> 5];
        if (word & bit) {
            word &= ~bit;
            --dirtyCount_;
            scanCursor_ = static_cast<uint16_t>((o + 1) % kN);
            return o;
        }
    }

    // dirtyCount_ said there was work but the bitset disagrees — impossible
    // unless the two fell out of step. Resynchronise rather than spin.
    dirtyCount_ = 0;
    return kNoOrdinal;
}

void Store::markAllDirty() {
    for (uint16_t o = 0; o < kN; ++o) markDirty_(o);
}

// ─────────────────────────────────────────────────────────────────────────────
// Access by ordinal
// ─────────────────────────────────────────────────────────────────────────────

float Store::get(uint16_t ordinal) const {
    return (ordinal < kN) ? values_[ordinal] : 0.0f;
}

bool Store::set(uint16_t ordinal, float t) {
    if (ordinal >= kN) return false;
    if (!(t == t)) return false;                  // reject NaN at the door
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Exact compare is right here: values only change when something writes a
    // genuinely different float. An epsilon would swallow the last bit of a
    // slow encoder sweep.
    if (values_[ordinal] == t) return false;

    values_[ordinal] = t;
    markDirty_(ordinal);
    return true;
}

bool Store::setForce(uint16_t ordinal, float t) {
    if (ordinal >= kN) return false;
    if (!(t == t)) return false;                  // reject NaN at the door
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    values_[ordinal] = t;
    markDirty_(ordinal);                          // even when unchanged — D-5
    return true;
}

bool Store::setQuiet(uint16_t ordinal, float t) {
    if (ordinal >= kN) return false;
    if (!(t == t)) return false;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (values_[ordinal] == t) return false;
    values_[ordinal] = t;        // deliberately NOT marked dirty — see header
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Access by ParamID — convenience for the MIDI-in path, which knows the id but
// not the ordinal. One O(n) lookup per inbound NRPN is fine at control rate.
// ─────────────────────────────────────────────────────────────────────────────

float Store::getById(uint16_t paramId) const {
    return get(ordinalOf(paramId));
}

bool Store::setById(uint16_t paramId, float t) {
    return set(ordinalOf(paramId), t);
}

bool Store::setQuietById(uint16_t paramId, float t) {
    return setQuiet(ordinalOf(paramId), t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Derived views
// ─────────────────────────────────────────────────────────────────────────────

float Store::getEng(uint16_t ordinal) const {
    const ParamDesc* d = descAt(ordinal);
    return d ? toEng(*d, values_[ordinal]) : 0.0f;
}

uint8_t Store::getIndex(uint16_t ordinal) const {
    const ParamDesc* d = descAt(ordinal);
    return d ? normToIndex(*d, values_[ordinal]) : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bulk load
// ─────────────────────────────────────────────────────────────────────────────

bool Store::loadAll(const float* src, size_t count, bool markDirty) {
    // A count mismatch means this is not a v2 patch for THIS table — refuse it
    // rather than load a shifted, silently-wrong set of values.
    if (!src || count != kParamCount) return false;

    for (size_t i = 0; i < kParamCount; ++i) {
        float t = src[i];
        if (!(t == t)) t = defaultNorm(kParams[i]);  // NaN in file -> default
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        values_[i] = t;
    }

    if (markDirty) markAllDirty();   // push the whole patch to the Teensy
    return true;
}

} // namespace JtParam
