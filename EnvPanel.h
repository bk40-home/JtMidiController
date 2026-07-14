// =============================================================================
// EnvPanel.h — envelope curve + 3-envelope overlay (Phase E)
// =============================================================================
// PORTED from the Phase C DisplayRenderer, which Phase D deleted and replaced
// with four knob arcs. That was a mistake: for an envelope, THE CURVE IS THE
// PARAMETER. Four arcs reading "38%, 62%, 70%, 45%" tell you nothing about the
// shape you are actually going to hear.
//
// The geometry below is the original, preserved deliberately — including the
// clamping, which was the fix for a real "lines across the screen" glitch under
// extreme parameter combinations. It has been re-expressed against the Phase C
// normalised-float model instead of the old 0..127 CC bytes.
//
// LAYOUT
//   Curve occupies the top ~150 px; the parameter rows sit underneath. You get
//   the SHAPE and the exact numbers (12 ms, 240 ms, 70%) at the same time —
//   which is the whole point, and what the knob grid destroyed.
//
// THE "ALL" TAB
//   Overlays all three envelopes on one set of axes, each in its own colour, so
//   you can see amp against filter against pitch. Also ported from the original.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "NavModel.h"
#include "JtParamStore.h"

namespace JtView {

// One envelope's shape, in NORMALISED units (0..1), pulled from the store.
// The renderer never touches ParamIDs — the caller resolves them, so the same
// drawing code serves amp / filter / pitch and the overlay.
struct EnvShape {
    float a, d, s, r;         // attack, decay, sustain, release  (0..1)
    float curveA, curveD, curveR;   // slope exponents, 0..1 normalised
    bool  valid;
};

class EnvPanel {
public:
    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; overValid_ = false; }

    // Read one envelope's shape out of the store. `section` is 4/5/6.
    static EnvShape shapeOf(uint8_t section, const JtParam::Store& store);

    // Single envelope: curve on top, values underneath.
    void draw(uint8_t section, const JtNav::RowSet& rows,
              const JtParam::Store& store, uint8_t focusRow);

    // The ALL tab: all three envelopes on one set of axes.
    void drawOverlay(const JtParam::Store& store);

    // Curve area geometry — the row list starts below this.
    static constexpr int16_t kCurveY = 46;    // top of the content area
    static constexpr int16_t kCurveH = 114;   // ENDS at 160, above the rows

    // The rows start at RowList::kGfxRowsY (164). The graphic MUST end above
    // that: an earlier revision had both starting at y=46, so the rows painted
    // straight over the curve and it looked like the graphic was missing.
    static constexpr int16_t kPadX   = 14;

private:
    Arduino_GFX* gfx_ = nullptr;
    bool  dirty_ = true;
    float last_[7] = {};      // last-drawn shape, so a static curve is not redrawn

    // Overlay ('ALL' tab) dirty cache — all three shapes, 7 values each. The
    // overlay used to repaint three curves EVERY frame while the tab was open
    // (~210 draw calls/frame for a static image); this makes it repaint only
    // when a shape actually moved, same rule as the single-curve path.
    float lastOver_[21] = {};
    bool  overValid_    = false;

    void drawCurve(const EnvShape& e, uint16_t colour, bool withGrid);
    static float exponentOf(float curveNorm);
};

} // namespace JtView
