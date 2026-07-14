// =============================================================================
// FilterPanel.h — filter response curve (Phase E)
// =============================================================================
// A schematic magnitude response, redrawn from the LIVE mode. Not a measured
// transfer function — this is a UI hint, not an analyser. What it must get right
// is the SHAPE (is this a lowpass? a notch? how steep?) and where the cutoff
// sits, so you can see at a glance what the filter is doing.
//
// The parameter rows underneath are drawn by RowList, and they are what carry
// the exact numbers.
//
// DYNAMIC ROWS
//   The filter's parameter set genuinely changes with the engine — the VA bank's
//   17 types mean nothing under the OBXa ladder, and the OBXa multimode controls
//   mean nothing under VA. That is handled DECLARATIVELY by visible_when in
//   params.yaml, not by an if-tree here (see NavModel::isVisible). This panel
//   only has to draw the curve.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "NavModel.h"
#include "JtParamStore.h"

namespace JtView {

class FilterPanel {
public:
    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; }

    void draw(const JtParam::Store& store);

    static constexpr int16_t kCurveY = 46;    // top of the content area
    static constexpr int16_t kCurveH = 114;   // ENDS at 160, above the rows

    // The rows start at RowList::kGfxRowsY (164). The graphic MUST end above
    // that: an earlier revision had both starting at y=46, so the rows painted
    // straight over the curve and it looked like the graphic was missing.

private:
    Arduino_GFX* gfx_ = nullptr;
    bool  dirty_ = true;
    float lastCut_ = -1.0f, lastRes_ = -1.0f;
    uint8_t lastEngine_ = 0xFF, lastMode_ = 0xFF, lastVaType_ = 0xFF;

    // The response SHAPE, reduced to what a schematic curve needs.
    struct Shape {
        bool  lowBand;    // passes DC
        bool  highBand;   // passes Nyquist
        bool  notch;      // a null at cutoff rather than a peak
        uint8_t poles;    // steepness: 1, 2, 3 or 4
    };
    static Shape shapeOf(const JtParam::Store& store);
};

} // namespace JtView
