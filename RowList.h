// =============================================================================
// RowList.h — the default page: a two-column value list (Phase E)
// =============================================================================
// THE VALUE IS THE INFORMATION.
//
// Phase D drew every continuous parameter as a knob arc. That was wrong, and
// not merely to taste: a knob is the right control when the DATA maps onto a
// circle. Attack time is 1..11880 ms on a log curve. Cutoff is 20 Hz..20 kHz.
// Neither maps onto a circle, and an arc sitting at "about 60%" tells you
// nothing you can act on. You need to see 240 ms.
//
// (This is the standard guidance, not a preference. Nielsen Norman Group's
// named failure case for skeuomorphism is exactly this — a knob used for a
// parameter whose values do not map onto a circle. And the JP-8000 this
// emulates is celebrated by its players for showing NUMERIC VALUES, not just
// knob-per-function.)
//
// So: 2 columns, 30 px rows, LABEL .......... VALUE, and the value rendered
// large enough to read at arm's length. No knobs. No bars.
//
// EXCEPT ON THE FOCUSED ROW
//   The focused row — and only the focused row — gets a value bar filling
//   behind the text. You get sweep feedback exactly where you are already
//   looking, and nothing anywhere else competing for attention.
//
// FITS WITHOUT SCROLLING
//   Worst list sub-tab is Effects at 14 params -> 2 columns x 7 rows. The
//   layout has room for 18. Nothing scrolls, ever.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "NavModel.h"
#include "JtParamStore.h"

namespace JtView {

// ── Geometry ────────────────────────────────────────────────────────────────
static constexpr int16_t kScreenW  = 480;
static constexpr int16_t kScreenH  = 320;
static constexpr int16_t kHeaderH  = 24;   // page name, patch, voice dots
static constexpr int16_t kTabH     = 22;   // sub-tab strip
static constexpr int16_t kListY    = kHeaderH + kTabH;   // 46: top of content
static constexpr int16_t kColW     = kScreenW / 2;

// A LIST page gives the rows the whole content area: roomy 30 px rows, 9 per
// column, 18 slots. Worst list sub-tab (Effects) needs 14.
static constexpr int16_t kRowH       = 30;
static constexpr uint8_t kRowsPerCol = (kScreenH - kListY) / kRowH;   // 9

// A GRAPHICAL page (envelope / sequencer / filter) has to share the content
// area with its graphic. The rows are pushed BELOW it and tightened to 26 px,
// 6 per column = 12 slots — which is exactly what the worst graphical page
// (SEQ, 12 rows) needs, and leaves 118 px for the graphic above.
//
// An earlier revision had the rows and the graphic BOTH starting at y=46, so
// the rows painted straight over the curve. That is the bug this split fixes:
// the origin is now a per-page property, not a constant.
static constexpr int16_t kGfxRowH       = 26;
static constexpr uint8_t kGfxRowsPerCol = 6;
static constexpr int16_t kGfxRowsY =
    static_cast<int16_t>(kScreenH - kGfxRowsPerCol * kGfxRowH);   // 164

class RowList {
public:
    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }

    // Choose the layout. Call on every page change, BEFORE drawing.
    //   graphical == false : rows own the content area   (30 px, 9/col)
    //   graphical == true  : rows sit below a graphic    (26 px, 6/col)
    void setLayout(bool graphical) {
        if (graphical == graphical_) return;
        graphical_ = graphical;
        invalidate();
    }
    bool graphical() const { return graphical_; }

    int16_t originY()   const { return graphical_ ? kGfxRowsY      : kListY; }
    int16_t rowHeight() const { return graphical_ ? kGfxRowH       : kRowH; }
    uint8_t perColumn() const { return graphical_ ? kGfxRowsPerCol : kRowsPerCol; }

    // Full repaint of the row area.
    //
    // rowHw (here and in drawDirty/drawRow): one packed tag per row, built by
    // ViewController::rebindControls() — which physical control drives the
    // row, as a palette index + a HOLLOW bit (see HwPalette.h). Drawn as a
    // small colour chip left of the label, in the SAME colour LedManager puts
    // on the control's LED: match chip to knob by eye, no counting. May be
    // nullptr (no chips). The tags carry no per-frame state, so they add
    // nothing to the dirty test — a tag only changes together with a row-set,
    // bank or mode change, all of which already invalidate().
    void drawAll(const JtNav::RowSet& rows, const JtParam::Store& store,
                 const uint8_t* rowHw, uint8_t focusRow);

    // Repaint only the rows whose value (or focus) changed. The per-frame path:
    // an idle frame issues ZERO draw calls.
    //
    // BOUNDED per call: at most `budget` rows are painted, and only rows lying
    // entirely above `maxY` (the sliced-erase cursor — a row must never paint
    // over ground that has not been cleared yet). A row that is skipped keeps
    // its stale cache entry ON PURPOSE, so it still reads as dirty next frame
    // and completes then. Returns true while any dirty row remains deferred.
    bool drawDirty(const JtNav::RowSet& rows, const JtParam::Store& store,
                   const uint8_t* rowHw, uint8_t focusRow,
                   int16_t maxY = kScreenH, uint8_t budget = 255);

    // Forget the cache. MUST be called on any sub-tab or page change, and after
    // any change to which rows are VISIBLE — the cache is keyed by row slot, so
    // when a conditional row appears or vanishes, slot N means a different
    // parameter and stale values would be drawn against the wrong labels.
    void invalidate();

    // Forget ONLY the slots intersecting a rect — the close-repair path for
    // overlays (page menu, select popup): the owner erases the covered rect,
    // and this makes exactly the rows under it repaint, nothing else.
    void invalidateRect(int16_t x, int16_t y, int16_t w, int16_t h);

    // Which row is under this touch point, or kNoRow.
    // NOT static any more: the answer depends on the current layout.
    static constexpr uint8_t kNoRow = 0xFF;
    uint8_t rowAt(const JtNav::RowSet& rows, int16_t x, int16_t y) const;

private:
    Arduino_GFX* gfx_ = nullptr;

    float   lastVal_[JtNav::kMaxRows] = {};
    uint8_t lastFocus_ = kNoRow;

    bool graphical_ = false;

    void drawRow(const JtNav::RowSet& rows, uint8_t idx,
                 const JtParam::Store& store, const uint8_t* rowHw,
                 bool focused);

    void rowRect(uint8_t idx, int16_t& x, int16_t& y) const;
};

} // namespace JtView
