// =============================================================================
// PickupMode.cpp
// =============================================================================
#include "PickupMode.h"

void PickupMode::onPageChange(const uint8_t targets[kNumPots]) {
    for (uint8_t i = 0; i < kNumPots; ++i) {
        targets_[i]     = targets[i];
        seeking_[i]     = true;     // all pots start seeking on page change
        seekStarted_[i] = false;    // seekStartCC_ will be latched lazily
        // seekStartCC_ left stale on purpose — only read after seekStarted_
        // becomes true, which happens on the next process() call.
    }
}

void PickupMode::setTarget(uint8_t potIdx, uint8_t targetCC) {
    if (potIdx >= kNumPots) return;
    targets_[potIdx] = targetCC;
    // Do not touch seeking_ / seekStarted_ here — the pot may already be
    // picked up and tracking normally. Only onPageChange resets to seeking.
}

bool PickupMode::process(uint8_t potIdx, uint8_t currentCC) {
    if (potIdx >= kNumPots) return false;

    // Already picked up — pass through every value
    if (!seeking_[potIdx]) return true;

    // First process() call after seeking started: latch the seek-start anchor.
    // Doing this lazily means onPageChange doesn't need access to the live pot
    // readings — caller stays simple.
    if (!seekStarted_[potIdx]) {
        seekStartCC_[potIdx] = currentCC;
        seekStarted_[potIdx] = true;
    }

    // ── Snap on decisive move ───────────────────────────────────────────────
    // If kSnapCC > 0 and the pot has moved that many units away from where it
    // was when seeking began, treat that as a deliberate "I want this value
    // now" gesture and pick up immediately — no need to sweep across the
    // target value. kSnapCC == 0 disables snap entirely.
    if (kSnapCC > 0) {
        const int16_t moved = (int16_t)currentCC - (int16_t)seekStartCC_[potIdx];
        if (abs(moved) >= (int16_t)kSnapCC) {
            seeking_[potIdx] = false;
            return true;
        }
    }

    // ── Conventional pickup ─────────────────────────────────────────────────
    // Pot is in the ±kThreshold zone around the target → pick up.
    const int16_t diff = (int16_t)currentCC - (int16_t)targets_[potIdx];
    if (abs(diff) <= (int16_t)kThreshold) {
        seeking_[potIdx] = false;
        return true;
    }

    // Still seeking — suppress this value (no CC output)
    return false;
}

bool PickupMode::isSeeking(uint8_t potIdx) const {
    return (potIdx < kNumPots) ? seeking_[potIdx] : false;
}

uint8_t PickupMode::targetValue(uint8_t potIdx) const {
    return (potIdx < kNumPots) ? targets_[potIdx] : 0;
}

int8_t PickupMode::seekDirection(uint8_t potIdx, uint8_t currentCC) const {
    if (potIdx >= kNumPots || !seeking_[potIdx]) return 0;
    if (currentCC < targets_[potIdx]) return +1;  // need to turn up
    if (currentCC > targets_[potIdx]) return -1;  // need to turn down
    return 0;
}
