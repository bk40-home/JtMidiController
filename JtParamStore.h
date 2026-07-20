// =============================================================================
// JtParamStore.h — the controller's canonical value store (Phase C)
// =============================================================================
// Replaces PageManager::ccState_[160] — a uint8_t array indexed by CC number.
//
// WHAT CHANGED AND WHY
//   Old: uint8_t ccState_[CC_STATE_SIZE], indexed by 7-bit CC number.
//        - Values were quantised to 128 steps at the UI, then handed to a
//          float/NRPN transport. The precision was thrown away before the wire
//          ever saw it.
//        - Params with no CC (121 of the 140) were unaddressable.
//        - Two params sharing a CC alias would alias in the cache.
//
//   New: float values_[kParamCount], indexed by table ORDINAL.
//        - Full float precision end to end. The UI is no longer the bottleneck.
//        - Every parameter is addressable, CC or not.
//        - Ordinal indexing is dense (140 slots, no holes) and O(1).
//
// STORAGE FORM IS NORMALISED (t, 0..1)
//   Not engineering units. Normalised is what NRPN carries and what PatchStore
//   saves, so keeping the store in the same form means no conversion on the two
//   paths that matter most for correctness. Engineering values are derived on
//   demand via JtParam::toEng() — see JtParamModel.h for why only one of the two
//   is allowed to be the source of truth.
//
// DIRTY TRACKING
//   set() marks an ordinal dirty. The owner drains dirty ordinals once per loop
//   and emits NRPN for each. This coalesces a fast pot sweep into one send per
//   loop rather than one per ADC sample, which is what kept the old paced-CC
//   timer necessary. A bitset over 140 ordinals is 18 bytes.
//
// THREADING
//   Control plane only. Written from loop(), read from loop() and the renderer.
//   No ISR access, so a plain (non-atomic) bitset is safe here — same rationale
//   as the firmware's _txDirty (deviation D2).
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "JtParamModel.h"

namespace JtParam {

class Store {
public:
    // Seed every parameter from its table default. Call once in setup().
    void begin();

    // ── Value access, by ORDINAL (hot path — O(1), no search) ───────────────
    // Callers that hold a ControlSlot already have the ordinal cached, so this
    // is the form the UI uses every frame.
    float get(uint16_t ordinal) const;

    // Set a normalised value. Clamps to 0..1. Marks dirty ONLY if the value
    // actually changed, so a pot sitting still emits nothing.
    // Returns true if the value changed.
    bool  set(uint16_t ordinal, float t);

    // Set WITHOUT marking dirty. For values arriving FROM the Teensy: echoing
    // them straight back would be a feedback loop.
    bool  setQuiet(uint16_t ordinal, float t);

    // ── Value access, by ParamID (cold path — does a lookup) ────────────────
    float getById(uint16_t paramId) const;
    bool  setById(uint16_t paramId, float t);
    bool  setQuietById(uint16_t paramId, float t);

    // ── Convenience: engineering units + option index ───────────────────────
    float   getEng(uint16_t ordinal) const;
    uint8_t getIndex(uint16_t ordinal) const;

    // ── Dirty set ───────────────────────────────────────────────────────────
    bool anyDirty() const { return dirtyCount_ > 0; }

    // Walk the dirty ordinals, clearing as it goes. Returns kNoOrdinal when
    // drained. Typical use:
    //
    //   uint16_t o;
    //   while ((o = store.takeDirty()) != kNoOrdinal) sendNrpn(o, store.get(o));
    //
    uint16_t takeDirty();

    // Mark every parameter dirty — forces a full re-send. Used after a patch
    // load, so the Teensy receives the whole patch.
    void markAllDirty();

    // ── Bulk (patch load) ───────────────────────────────────────────────────
    // Overwrite the whole store from a normalised array. `count` must equal
    // kParamCount or the call is rejected (returns false) — a size mismatch
    // means the caller handed us something that is not a v2 patch.
    bool loadAll(const float* src, size_t count, bool markDirty);

    // set() skips the dirty mark when the value is unchanged — correct for
    // knobs, WRONG for the sequencer's indexed pair (D-5): painting the same
    // VALUE onto a different STEP re-writes seq.step_value with an equal
    // float, and the send would silently be swallowed while the grid shows
    // the bar. This variant always marks dirty. Use it ONLY for indexed
    // writes whose meaning depends on a sibling address parameter.
    bool setForce(uint16_t ordinal, float t);

    // Read-only view for PatchStore::save().
    const float* raw() const { return values_; }
    size_t       size() const { return JT::Params::kParamCount; }

private:
    static constexpr size_t kN     = JT::Params::kParamCount;
    static constexpr size_t kWords = (kN + 31) / 32;

    float    values_[kN]      = {};
    uint32_t dirty_[kWords]   = {};
    uint16_t dirtyCount_      = 0;
    uint16_t scanCursor_      = 0;   // resume point for takeDirty()

    void markDirty_(uint16_t ordinal);
};

} // namespace JtParam
