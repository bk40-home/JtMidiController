// =============================================================================
// SeqPanel.cpp — see SeqPanel.h.
// =============================================================================
#include "SeqPanel.h"

#include <math.h>

namespace JtView {
namespace {

namespace ID = JT::Params::ID;   // ID is a NAMESPACE of constants, not a type
using JT::Params::ParamDesc;

constexpr uint16_t C_BG      = 0x0000;
constexpr uint16_t C_GRID    = 0x2124;
constexpr uint16_t C_BAR     = 0xFC00;   // orange — an active step
constexpr uint16_t C_BAR_OFF = 0x4A49;   // grey   — beyond seq.steps
constexpr uint16_t C_HEAD    = 0xFFFF;   // white  — the sounding step
constexpr uint16_t C_FOCUS   = 0xFFE0;   // yellow — the step being edited
constexpr uint16_t C_CENTRE  = 0x4208;   // the zero line for bipolar steps

constexpr int16_t kPadX  = 8;
constexpr int16_t kGap   = 2;
// 16 bars across 480 - 2*8 = 464 px, with a 2 px gap: 27 px each.
constexpr int16_t kBarW  = (480 - 2 * kPadX - 15 * kGap) / SeqPanel::kSteps;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// The UI's step cache
// ─────────────────────────────────────────────────────────────────────────────

void SeqPanel::setStep(uint8_t s, float v) {
    if (s >= kSteps) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    steps_[s] = v;
}

float SeqPanel::step(uint8_t s) const {
    return (s < kSteps) ? steps_[s] : 0.0f;
}

void SeqPanel::setAllSteps(const float* v16) {
    if (!v16) return;
    for (uint8_t i = 0; i < kSteps; ++i) setStep(i, v16[i]);
    dirty_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit test
// ─────────────────────────────────────────────────────────────────────────────

uint8_t SeqPanel::stepAt(int16_t x, int16_t y) {
    if (y < kGridY || y >= kGridY + kGridH) return 0xFF;
    if (x < kPadX) return 0xFF;

    const int16_t rel = static_cast<int16_t>(x - kPadX);
    const int16_t i   = static_cast<int16_t>(rel / (kBarW + kGap));
    return (i >= 0 && i < kSteps) ? static_cast<uint8_t>(i) : 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

float SeqPanel::valueFromY(int16_t y) {
    // Unipolar, mirroring drawBar exactly: bottom edge = 0, top edge = 1.
    // (The first tap-grid pass used the bipolar mid-line mapping, which made
    // half-height taps produce near-empty bars — hard to edit by eye.)
    const float B = static_cast<float>(kGridY + kGridH);
    float v = (B - static_cast<float>(y)) / static_cast<float>(kGridH);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

void SeqPanel::drawBar(uint8_t i, float v, uint8_t activeCount,
                       bool isHead, bool isFocus) {
    const int16_t x = static_cast<int16_t>(kPadX + i * (kBarW + kGap));
    const int16_t B = static_cast<int16_t>(kGridY + kGridH);

    gfx_->fillRect(x, kGridY, kBarW, kGridH, C_BG);

    // A step beyond seq.steps still EXISTS and keeps its value — it simply is
    // not played. Drawn grey rather than hidden, so shortening the pattern does
    // not look like the steps were destroyed.
    const bool active = (i < activeCount);
    const uint16_t col = active ? C_BAR : C_BAR_OFF;

    // UNIPOLAR: the bar grows from the baseline; its height IS the value —
    // what the finger taps is what it gets. (The bipolar mid-line drawing was
    // faithful to "0.5 = no modulation" but made the grid hard to edit by
    // eye: half-value taps produced near-empty bars.)
    const int16_t h = static_cast<int16_t>(v * static_cast<float>(kGridH));
    if (h > 0) {
        gfx_->fillRect(x, static_cast<int16_t>(B - h), kBarW, h, col);
    }

    // The mid-line stays as a REFERENCE: on bipolar destinations 0.5 is "no
    // modulation", and a half-height bar sitting exactly on the line reads
    // that way at a glance. Drawn over the bar so it shows either way.
    const int16_t mid = static_cast<int16_t>(kGridY + kGridH / 2);
    gfx_->drawFastHLine(x, mid, kBarW, C_CENTRE);

    // Both outlines sit INSIDE the bar rect. The focus outline used to be
    // drawn one pixel OUTSIDE it — outside the erase above — so leaving a
    // step stranded a yellow halo on screen that nothing ever cleared.
    if (isHead)  gfx_->drawRect(x, kGridY, kBarW, kGridH, C_HEAD);
    if (isFocus) gfx_->drawRect(x, kGridY, kBarW, kGridH, C_FOCUS);

    gfx_->drawFastHLine(x, B, kBarW, C_GRID);
}

void SeqPanel::draw(const JtParam::Store& store, uint8_t playHead,
                    uint8_t focusStep) {
    if (!gfx_) return;

    // seq.steps is continuous 0..1; it means "how many of the 16 are played".
    const ParamDesc* sd = JtParam::descOf(ID::SEQ_STEPS);
    uint8_t count = kSteps;
    if (sd) {
        const float eng = JtParam::toEng(*sd, store.getById(ID::SEQ_STEPS));
        count = static_cast<uint8_t>(eng + 0.5f);
        if (count < 1)      count = 1;
        if (count > kSteps) count = kSteps;
    }

    for (uint8_t i = 0; i < kSteps; ++i) {
        const bool headMoved  = (i == playHead)  != (i == lastHead_);
        const bool focusMoved = (i == focusStep) != (i == lastFocus_);
        const bool valMoved   = !(steps_[i] == lastDrawn_[i]);
        const bool countMoved = (count != lastCount_);

        if (dirty_ || valMoved || headMoved || focusMoved || countMoved) {
            drawBar(i, steps_[i], count, i == playHead, i == focusStep);
            lastDrawn_[i] = steps_[i];
        }
    }

    lastHead_  = playHead;
    lastFocus_ = focusStep;
    lastCount_ = count;
    dirty_     = false;
}

} // namespace JtView
