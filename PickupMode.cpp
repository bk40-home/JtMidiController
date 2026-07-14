// =============================================================================
// PickupMode.cpp — see PickupMode.h.
// =============================================================================
#include "PickupMode.h"

#include <math.h>

void PickupMode::onPageChange(const float targets[kNumPots]) {
    for (uint8_t i = 0; i < kNumPots; ++i) {
        targets_[i]     = targets[i];
        seeking_[i]     = true;    // every pot re-seeks on a page change
        seekStarted_[i] = false;   // anchor latched lazily in process()
        // seekStart_[i] deliberately left stale — it is only read once
        // seekStarted_[i] is true, which cannot happen before process() sets it.
    }
}

void PickupMode::setTarget(uint8_t potIdx, float target) {
    if (potIdx >= kNumPots) return;
    targets_[potIdx] = target;
    // seeking_/seekStarted_ untouched on purpose: a pot that has already picked
    // up must keep tracking. Only onPageChange() puts pots back into seeking.
}

bool PickupMode::process(uint8_t potIdx, float current) {
    if (potIdx >= kNumPots) return false;

    // Already picked up — everything passes through.
    if (!seeking_[potIdx]) return true;

    // Latch the seek anchor on the first poll after seeking began.
    if (!seekStarted_[potIdx]) {
        seekStart_[potIdx]   = current;
        seekStarted_[potIdx] = true;
    }

    // ── Snap on a decisive move ─────────────────────────────────────────────
    // kSnap is constexpr, so when PICKUP_SNAP_CC is 0 the compiler removes this
    // whole block — no runtime cost for disabling snap.
    if (kSnap > 0.0f) {
        if (fabsf(current - seekStart_[potIdx]) >= kSnap) {
            seeking_[potIdx] = false;
            return true;
        }
    }

    // ── Conventional pickup: has the pot reached/crossed the target? ────────
    if (fabsf(current - targets_[potIdx]) <= kThreshold) {
        seeking_[potIdx] = false;
        return true;
    }

    // Crossing detection. The pot can jump past the target between two polls
    // without ever landing inside the threshold window (a fast twist moves
    // several steps per poll). Compare which SIDE of the target the pot was on
    // at the anchor versus now: a sign change means it crossed.
    const float wasAbove = seekStart_[potIdx] - targets_[potIdx];
    const float isAbove  = current            - targets_[potIdx];
    if ((wasAbove < 0.0f && isAbove > 0.0f) ||
        (wasAbove > 0.0f && isAbove < 0.0f)) {
        seeking_[potIdx] = false;
        return true;
    }

    return false;   // still seeking — suppress the value
}

bool PickupMode::isSeeking(uint8_t potIdx) const {
    return (potIdx < kNumPots) ? seeking_[potIdx] : false;
}

float PickupMode::targetValue(uint8_t potIdx) const {
    return (potIdx < kNumPots) ? targets_[potIdx] : 0.0f;
}

int8_t PickupMode::seekDirection(uint8_t potIdx, float current) const {
    if (potIdx >= kNumPots) return 0;
    const float diff = targets_[potIdx] - current;
    if (diff >  kThreshold) return  1;   // turn up
    if (diff < -kThreshold) return -1;   // turn down
    return 0;
}
