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

    // Modulation/pattern lanes share this one grid.  Gate + Aux are the
    // firmware's two mod lanes (StageB/C/D).  Arp (Phase 9) is a THIRD flip
    // state: the same 16-bar grid, but it edits the arpeggiator's per-step
    // ACCENT (arp.step_accent) — the on/off and ratchet lanes appear as list
    // rows below, not on the grid (low-risk first pass; a bespoke tri-lane
    // grid is a later job).  Only one is shown/edited at a time; the seq page's
    // own button cycles Gate -> Aux -> Arp -> Gate.  Clock, playhead, focus and
    // step count are shared; switching only swaps which 16-value cache the grid
    // reads and which accent colour it draws.
    enum class Lane : uint8_t { Gate = 0, Aux = 1, Arp = 2 };

    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; }

    // Active lane.  Switching forces a full redraw (the bars and their colour
    // both change).  Returns the lane after toggling, for LED/label sync.
    void  setLane(Lane l) { if (l != lane_) { lane_ = l; invalidate(); } }
    Lane  lane() const { return lane_; }
    // Cycle Gate -> Aux -> Arp -> Gate.  Returns the new lane for LED/label sync.
    Lane  toggleLane()
    {
        const uint8_t n = (static_cast<uint8_t>(lane_) + 1u) % 3u;
        setLane(static_cast<Lane>(n));
        return lane_;
    }

    // The UI's view of the 16 step values (normalised).  These act on the
    // ACTIVE lane (see setLane).  See the header note on why these cannot
    // simply be read from the store.
    void  setStep(uint8_t step, float v);
    float step(uint8_t step) const;

    // Overwrite all 16 on the ACTIVE lane — used for the visible lane.
    void setAllSteps(const float* v16);

    // Lane-explicit variants — the patch-load path refreshes BOTH lanes'
    // caches regardless of which is currently shown.
    void  setStepFor(Lane l, uint8_t step, float v);
    void  setAllStepsFor(Lane l, const float* v16);
    float stepFor(Lane l, uint8_t step) const;

    // Draw the bar grid. `playHead` is the currently sounding step, or 0xFF when
    // the sequencer is stopped.
    void draw(const JtParam::Store& store, uint8_t playHead, uint8_t focusStep);

    // Which step is under this touch point, or 0xFF.
    static uint8_t stepAt(int16_t x, int16_t y);

    // Tap-grid entry (standing spec: TAP, not drag): the tapped HEIGHT is the
    // step value. Exact inverse of the bar drawing — bottom edge 0.0, top
    // edge 1.0 — so a step reads back exactly where the finger put it. The
    // mid-line is a visual reference only (0.5 = no modulation on bipolar
    // destinations).
    static float valueFromY(int16_t y);

    static constexpr int16_t kGridY = 46;    // top of the content area
    static constexpr int16_t kGridH = 114;   // ENDS at 160, above the rows

    // The rows start at RowList::kGfxRowsY (164). The graphic MUST end above
    // that: an earlier revision had both starting at y=46, so the rows painted
    // straight over the curve and it looked like the graphic was missing.

private:
    Arduino_GFX* gfx_ = nullptr;
    bool    dirty_ = true;
    Lane    lane_  = Lane::Gate;
    float   steps_[kSteps]    = {};   // gate lane cache
    float   auxSteps_[kSteps] = {};   // aux lane cache (Stage B/C/D)
    float   arpSteps_[kSteps] = {};   // arp accent-lane cache (Phase 9)

    // Active-lane cache accessor — keeps the draw/edit code lane-agnostic.
    float*       active()       { return lane_ == Lane::Aux ? auxSteps_
                                       : lane_ == Lane::Arp ? arpSteps_ : steps_; }
    const float* active() const { return lane_ == Lane::Aux ? auxSteps_
                                       : lane_ == Lane::Arp ? arpSteps_ : steps_; }

    // 3-way cache selector for the lane-explicit setters (patch-load path).
    float*       cacheFor(Lane l);

    float   lastDrawn_[kSteps] = {};
    uint8_t lastHead_  = 0xFF;
    uint8_t lastFocus_ = 0xFF;
    uint8_t lastCount_ = 0xFF;

    void drawBar(uint8_t i, float v, uint8_t activeCount,
                 bool isHead, bool isFocus);
};

} // namespace JtView
