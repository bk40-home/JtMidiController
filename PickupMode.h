// =============================================================================
// PickupMode.h — pot pickup handling (Phase C: normalised float)
// =============================================================================
// When the user switches page, a pot's physical position no longer matches the
// stored value for the new mapping. Without pickup, touching the pot would jump
// the parameter to wherever the pot happens to be sitting.
//
// PICKUP BEHAVIOUR
//   1. On page/scene change every pot enters "seeking".
//   2. A seeking pot emits NOTHING until it crosses its stored target.
//   3. Once crossed it "picks up" and tracks normally.
//   4. The display shows a direction arrow + target while seeking.
//
// Encoders never need pickup — they send relative deltas, not absolute position.
//
// PHASE C CHANGE — VALUES ARE NORMALISED FLOATS (0..1), NOT CC BYTES
//   The pot hardware still reports 0..127; that conversion now happens at the
//   hardware boundary in PageManager (potNorm()), so everything above the
//   boundary speaks one unit. Thresholds that used to be in CC units are now
//   fractions of full scale:
//
//     kThreshold  Config::PICKUP_THRESHOLD / 127.0f
//     kSnap       Config::PICKUP_SNAP_CC   / 127.0f
//
//   Expressing them as fractions of the OLD CC scale keeps the feel identical
//   to what the panel had before — the pot still has ~128 physical steps, so a
//   threshold of "2 CC" and one of "2/127 of full scale" are the same distance.
//   Only the arithmetic moved.
//
// SNAP ON DECISIVE MOVE
//   At ~1 kHz polling even a fast twist is sub-one-step per poll, so per-poll
//   delta detection is unreliable. Instead the first process() call after
//   seeking begins latches the pot's position as an anchor. If the pot later
//   moves kSnap away from that anchor, it picks up immediately regardless of
//   the target — that is what a user means by a decisive move. kSnap == 0
//   disables snap (pure pickup).
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"

class PickupMode {
public:
    static constexpr uint8_t kNumPots = 8;

    // Thresholds as fractions of full scale — see the header note on why these
    // are still derived from the old CC-unit constants.
    static constexpr float kThreshold =
        static_cast<float>(Config::PICKUP_THRESHOLD) / 127.0f;
    static constexpr float kSnap =
        static_cast<float>(Config::PICKUP_SNAP_CC) / 127.0f;

    PickupMode() = default;

    // ── Page / scene change ─────────────────────────────────────────────────
    // Puts every pot into seeking with the supplied normalised targets. The
    // seek anchor is latched lazily on the first process() call, because the
    // live pot readings are not available at the moment the page changes.
    void onPageChange(const float targets[kNumPots]);

    // Update one pot's target — e.g. the Teensy pushed a new value for a
    // parameter that a pot on this page is bound to. Does NOT reset seeking:
    // a pot that has already picked up keeps tracking.
    void setTarget(uint8_t potIdx, float target);

    // ── Per-poll ────────────────────────────────────────────────────────────
    // Call every poll with the pot's current normalised position.
    // Returns true if the pot has picked up and its value should be emitted.
    bool process(uint8_t potIdx, float current);

    // ── Display state ───────────────────────────────────────────────────────
    bool  isSeeking(uint8_t potIdx) const;
    float targetValue(uint8_t potIdx) const;

    // +1 = turn up to reach target, -1 = turn down, 0 = at target.
    int8_t seekDirection(uint8_t potIdx, float current) const;

private:
    float targets_[kNumPots]     = {};
    float seekStart_[kNumPots]   = {};   // anchor, valid once seekStarted_
    bool  seeking_[kNumPots]     = {};
    bool  seekStarted_[kNumPots] = {};
};
