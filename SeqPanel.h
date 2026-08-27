// =============================================================================
// SeqPanel.h — step sequencer bar grid (Phase E)
// =============================================================================
// PORTED from the Phase C DisplayRenderer, which Phase D deleted. Phase D drew
// the sequencer as a list of knob arcs, which is close to useless: a sequence is
// a SHAPE across 16 steps, and you read it by looking at the bars, not by
// reading sixteen percentages.
//
// THE 16 STEPS ARE NOW REAL PARAMETERS
//   This panel used to keep its own 16-entry cache, because the engine exposed
//   ONE seq.step_value addressed by a moving seq.step_select cursor — so the
//   store held "the last step written", never all 16. That cache was a display
//   fiction: it was invisible to the engine, absent from saved patches, and
//   unreachable by any other editor.
//
//   The firmware now carries explicit per-step parameters (seq.step_1..16,
//   seq.aux_step_1..16, arp.step_accent_1..16), so the pattern IS ordinary
//   store state. The caches are gone. The caller hands draw() the 16 ordinals
//   for the active lane and this panel reads them straight from the store —
//   one source of truth, so the bars cannot drift from the sound.
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

    // Draw the bar grid.
    //   ord16       — the 16 store ordinals for the ACTIVE lane, in step order.
    //                 The panel no longer decides which lane's params these
    //                 are; the caller does, which is why lane switching needs
    //                 no cache swap any more.
    //   activeCount — how many of the 16 are played (seq.steps or
    //                 arp.step_count, depending on the lane — again the
    //                 caller's call, not this panel's).
    //   playHead    — the currently sounding step, or 0xFF when stopped.
    void draw(const JtParam::Store& store, const uint16_t* ord16,
              uint8_t activeCount, uint8_t playHead, uint8_t focusStep);

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

    // lastDrawn_ is a REDRAW filter, not a cache of the pattern: it records
    // what is currently on the glass so an unchanged bar is not repainted.
    // The pattern itself lives in the parameter store.
    float   lastDrawn_[kSteps] = {};
    uint8_t lastHead_  = 0xFF;
    uint8_t lastFocus_ = 0xFF;
    uint8_t lastCount_ = 0xFF;

    void drawBar(uint8_t i, float v, uint8_t activeCount,
                 bool isHead, bool isFocus);
};

} // namespace JtView
