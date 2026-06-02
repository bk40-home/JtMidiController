// =============================================================================
// PickupMode.cpp
// =============================================================================
#include "PickupMode.h"

void PickupMode::onPageChange(const uint8_t targets[kNumPots]) {
    for (uint8_t i = 0; i < kNumPots; ++i) {
        targets_[i] = targets[i];
        seeking_[i] = true;  // all pots start seeking on page change
    }
}

void PickupMode::setTarget(uint8_t potIdx, uint8_t targetCC) {
    if (potIdx >= kNumPots) return;
    targets_[potIdx] = targetCC;
    // Do not reset seeking_ here — the pot may already be picked up and
    // tracking normally. Only onPageChange resets to seeking.
}

bool PickupMode::process(uint8_t potIdx, uint8_t currentCC) {
    if (potIdx >= kNumPots) return false;

    // Already picked up — pass through all values
    if (!seeking_[potIdx]) return true;

    // Check if the pot has crossed into the pickup zone around the target.
    // The zone is ±kThreshold CC units wide. Once the pot enters the zone,
    // it picks up and stays picked up until the next page change.
    const int16_t diff = (int16_t)currentCC - (int16_t)targets_[potIdx];
    if (abs(diff) <= kThreshold) {
        seeking_[potIdx] = false;  // picked up
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
