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

    // Step values are bipolar around the centre (they modulate a destination),
    // so the bar grows up or down from the mid-line. A step at 0.5 is "no
    // modulation" and correctly draws as nothing.
    const int16_t mid = static_cast<int16_t>(kGridY + kGridH / 2);
    gfx_->drawFastHLine(x, mid, kBarW, C_CENTRE);

    const float   d = v - 0.5f;
    const int16_t h = static_cast<int16_t>(fabsf(d) * static_cast<float>(kGridH));
    if (h > 0) {
        if (d > 0.0f) gfx_->fillRect(x, static_cast<int16_t>(mid - h), kBarW, h, col);
        else          gfx_->fillRect(x, mid, kBarW, h, col);
    }

    // The playhead is drawn as a full-height outline rather than a fill: it must
    // be visible even on a step whose value is centred (and therefore has no bar
    // at all), or the sequencer would appear to skip steps.
    if (isHead)  gfx_->drawRect(x, kGridY, kBarW, kGridH, C_HEAD);
    if (isFocus) gfx_->drawRect(static_cast<int16_t>(x - 1),
                                static_cast<int16_t>(kGridY - 1),
                                static_cast<int16_t>(kBarW + 2),
                                static_cast<int16_t>(kGridH + 2), C_FOCUS);

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
