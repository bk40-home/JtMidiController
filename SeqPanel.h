// =============================================================================
// SeqPanel.h — step sequencer bar grid (Phase E)
// =============================================================================
// PORTED from the Phase C DisplayRenderer, which Phase D deleted. Phase D drew
// the sequencer as a list of knob arcs, which is close to useless: a sequence is
// a SHAPE across 16 steps, and you read it by looking at the bars, not by
// reading sixteen percentages.
//
// THE 16 STEPS ARE NOT IN THE PARAMETER STORE
//   The engine exposes ONE seq.step_value, addressed by seq.step_select — write
//   the select, then the value. So the store holds only "the last step written",
//   never all 16. This panel therefore keeps its own 16-entry cache, which is
//   the UI's view of the pattern.
//
//   That cache is authoritative for DISPLAY only. The engine is authoritative
//   for sound. They are kept in step by writing both on every edit, and by
//   re-reading on a patch load.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "NavModel.h"
#include "JtParamStore.h"

namespace JtView {

class SeqPanel {
public:
    static constexpr uint8_t kSteps = 16;

    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; }

    // The UI's view of the 16 step values (normalised). See the header note on
    // why these cannot simply be read from the store.
    void  setStep(uint8_t step, float v);
    float step(uint8_t step) const;

    // Overwrite all 16 — used after a patch load.
    void setAllSteps(const float* v16);

    // Draw the bar grid. `playHead` is the currently sounding step, or 0xFF when
    // the sequencer is stopped.
    void draw(const JtParam::Store& store, uint8_t playHead, uint8_t focusStep);

    // Which step is under this touch point, or 0xFF.
    static uint8_t stepAt(int16_t x, int16_t y);

    static constexpr int16_t kGridY = 46;    // top of the content area
    static constexpr int16_t kGridH = 114;   // ENDS at 160, above the rows

    // The rows start at RowList::kGfxRowsY (164). The graphic MUST end above
    // that: an earlier revision had both starting at y=46, so the rows painted
    // straight over the curve and it looked like the graphic was missing.

private:
    Arduino_GFX* gfx_ = nullptr;
    bool    dirty_ = true;
    float   steps_[kSteps] = {};

    float   lastDrawn_[kSteps] = {};
    uint8_t lastHead_  = 0xFF;
    uint8_t lastFocus_ = 0xFF;
    uint8_t lastCount_ = 0xFF;

    void drawBar(uint8_t i, float v, uint8_t activeCount,
                 bool isHead, bool isFocus);
};

} // namespace JtView
