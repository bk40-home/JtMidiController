// =============================================================================
// PickupMode.h — Pot pickup handling
// =============================================================================
// When the user switches pages, pot physical position may not match the stored
// CC value for the new mapping. Without pickup mode, moving the pot would
// cause an immediate jump to wherever the pot physically is.
//
// PICKUP BEHAVIOUR:
//   1. On page change, each pot enters "seeking" state
//   2. The pot does NOT send CC until it crosses the stored target value
//   3. Once crossed, the pot "picks up" and sends normally
//   4. The display shows a direction arrow (↑↓) and target value while seeking
//
// This applies to all pot-mapped CCs. Encoders do not need pickup — they
// send relative deltas, not absolute positions.
//
// PICKUP THRESHOLD:
//   The pot picks up when it crosses within ±PICKUP_THRESHOLD of the target.
//   A tight threshold (2-3) feels precise; a loose one (5+) is easier to grab.
//
// SNAP ON DECISIVE MOVE (Config::PICKUP_SNAP_CC):
//   At ~1 kHz polling even a fast user twist is sub-1-CC per poll, so
//   per-poll-delta detection is unreliable. Instead, on the first process()
//   call after seeking begins, the current pot reading is latched as the
//   seek-start anchor. If the pot subsequently moves ≥ PICKUP_SNAP_CC units
//   away from that anchor, the pot picks up immediately regardless of the
//   target value — that's what the user means by a "decisive move".
//   PICKUP_SNAP_CC = 0 disables snap entirely (pure pickup behaviour).
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"

class PickupMode {
public:
    static constexpr uint8_t kNumPots   = 8;
    static constexpr uint8_t kThreshold = Config::PICKUP_THRESHOLD;
    // 0 = snap disabled; otherwise the CC-unit distance from the seek-start
    // anchor that counts as a "decisive move" and forces pickup.
    static constexpr uint8_t kSnapCC    = Config::PICKUP_SNAP_CC;

    PickupMode() = default;

    // ── Page change handling ────────────────────────────────────────────────
    // Call when the page or scene changes. Sets all pots to "seeking" state
    // with the given target CC values. The seek-start anchor for each pot is
    // latched lazily on the first process() call (we don't have the pot
    // reading at the moment of the page change).
    void onPageChange(const uint8_t targets[kNumPots]);

    // Set target for a single pot (e.g. when Teensy sends a CC update)
    void setTarget(uint8_t potIdx, uint8_t targetCC);

    // ── Per-poll processing ─────────────────────────────────────────────────
    // Call for each pot every poll cycle with the pot's current CC value.
    // Returns true if the pot has picked up and the value should be sent.
    // If false, the value should be suppressed (pot is still seeking).
    bool process(uint8_t potIdx, uint8_t currentCC);

    // ── State queries (for display) ─────────────────────────────────────────
    // True if pot is still seeking (hasn't picked up yet)
    bool isSeeking(uint8_t potIdx) const;
    // Target CC value the pot is seeking toward
    uint8_t targetValue(uint8_t potIdx) const;
    // Direction to move: +1 = turn up, -1 = turn down, 0 = at target
    int8_t seekDirection(uint8_t potIdx, uint8_t currentCC) const;

private:
    uint8_t targets_[kNumPots]      = {};   // target CC values
    uint8_t seekStartCC_[kNumPots]  = {};   // pot reading when seeking began
    bool    seeking_[kNumPots]      = {};   // true = pot hasn't picked up yet
    bool    seekStarted_[kNumPots]  = {};   // seekStartCC_ latched for this seek
};
