// =============================================================================
// ViewController.h — input routing, navigation and NRPN dispatch (Phase E)
// =============================================================================
// Replaces the Phase D ViewController, which drove a uniform grid of knob arcs
// across 17 flat sections with no touch navigation. Three things changed:
//
//   1. NAVIGATION. 8 pages with sub-tabs, ALL reachable by touch — tap the page
//      name for a drop-down, tap a sub-tab, swipe to page. The ByteButtons are
//      now shortcuts, not the only way in.
//
//   2. WIDGETS. The default page is a two-column VALUE LIST — label and number,
//      no knob. A knob is right when the data maps onto a circle; attack time
//      (1..11880 ms, log) does not, and an arc at "about 60%" is unreadable.
//      Only the FOCUSED row gets a value bar, so sweep feedback lands where you
//      are already looking.
//
//   3. GRAPHICAL PAGES. Envelopes get their curve back (plus the 3-way overlay),
//      the sequencer gets its bar grid, the filter gets a response curve. For
//      those, the graphic IS the parameter — a list of sixteen percentages is
//      not a sequence.
//
// UNCHANGED FROM PHASE C/D
//   JtParamModel, JtParamStore, JtNrpn, PickupMode, SelectPopup, PatchManager,
//   NameEditor. The model layer was right; only the view was wrong.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "NavModel.h"
#include "NavBar.h"
#include "RowList.h"
#include "EnvPanel.h"
#include "SeqPanel.h"
#include "FilterPanel.h"
#include "JtParamStore.h"
#include "JtNrpn.h"
#include "PickupMode.h"
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"
#include "TouchInput.h"
#include "HomePanel.h"

class ViewController {
public:
    void begin(JtParam::CcSink sink, Arduino_GFX* gfx);

    void update(Angle8Unit& angle, Encoder8Unit& encoder, ByteButtonUnit& buttons);
    void render();

    // Touch. Takes the whole unit, not one point: paging is a TWO-finger
    // swipe (point 1 is needed), and the release-frame coordinates are
    // zeroed by the driver so per-point plumbing invited the phantom-swipe
    // defect back. Handles navigation AND value editing.
    void handleTouch(const TouchInput& touch);

    bool handleInboundCC(uint8_t cc, uint8_t value);
    void requestResync() { nrpn_.requestResync(); }

    uint16_t takeSelectPopupRequest();

    // Engine status word from NRPN 0x3FFF: [mask:8|step:4|running:1].
    void applyStatus(uint16_t status14) {
        voiceMask_  = static_cast<uint8_t>((status14 >> 5) & 0xFF);
        playStep_   = static_cast<uint8_t>((status14 >> 1) & 0x0F);
        seqRunning_ = (status14 & 1u) != 0;
    }

    // The HOME dashboard shows which slot is loaded; PatchManager owns that
    // fact, the .ino ferries it here after PatchManager updates.
    void setPatchSlot(uint8_t slot) { patchSlot_ = slot; }

    // Long-press on any ByteButton parks this; the .ino collects it and opens
    // the PatchOverlay — same parked-request pattern as the SelectPopup, so
    // the modal open (and its tap-latch discard) lives in ONE place.
    bool takePatchOverlayRequest() {
        const bool r = patchOverlayReq_;
        patchOverlayReq_ = false;
        return r;
    }
    void     commitSelect(uint16_t paramId, uint8_t optionIndex);

    JtParam::Store&       store()       { return store_; }
    const JtParam::Store& store() const { return store_; }

    void setPage(uint8_t page);
    void setSubTab(uint8_t sub);
    uint8_t page()   const { return page_; }
    uint8_t subTab() const { return sub_; }

    // The patch name lives in the header, which NavBar owns — marking the
    // NavBar dirty repaints exactly that band. This used to force a FULL
    // screen redraw for a 12-character label.
    void setPatchName(const char* n) { patchName_ = n; nav_.invalidate(); }

    // Full repaint: header band + sliced content erase. Rare by design — boot,
    // and recovery after a FULL-SCREEN modal (NameEditor). Everything else
    // goes through invalidateContent() or repairRect().
    void forceRedraw();

    // Repair a rect an overlay covered (page menu, select popup): one bounded
    // background fill, then ONLY the widgets intersecting it repaint. This is
    // the Phase C "route the refresh into the cheapest repaint path" rule,
    // re-established.
    void repairRect(int16_t x, int16_t y, int16_t w, int16_t h);

    bool pickupSeeking(uint8_t i) const { return pickup_.isSeeking(i); }

    // ── The rows currently on screen ────────────────────────────────────────
    uint8_t rowCount() const { return rows_.count; }

    // ── Type-based hardware binding (Phase F2) ──────────────────────────────
    // The binding is no longer positional. Each visible row goes to the
    // control its TYPE suits, from ParamDesc::control in the generated table:
    //
    //   Control::Pot     -> Angle8 pots, in visible order, TWO BANKS of 8
    //                       selected by the Angle8 unit's own switch. Three
    //                       tabs overflow 8 pots (Osc1/Osc2 = 11, FX = 10);
    //                       banking covers them, and on tabs that fit in one
    //                       bank the switch's second position is simply dark.
    //   Control::Encoder -> Encoder8 encoders, in visible order. Rotate steps;
    //                       PUSH on a Select opens the SelectPopup (the touch
    //                       route and the encoder route converge on the same
    //                       parked request).
    //   Control::Switch  -> ByteButtons, in visible order — but ONLY while the
    //                       Encoder8 scene switch is ON. OFF, the buttons keep
    //                       their page-shortcut role. The LEDs follow the mode
    //                       (page palette vs toggle state), so the mode is
    //                       always visible on the hardware itself.
    //
    // LedManager reads these to decide which controls are live, so an unlit
    // control genuinely does nothing rather than doing something unseen.
    // 0xFF = unbound.
    uint8_t potBoundRow(uint8_t i) const;
    uint8_t encBoundRow(uint8_t i) const {
        return (i < kEncBindSlots) ? encBind_[i] : JtView::RowList::kNoRow;
    }
    uint8_t btnBoundRow(uint8_t i) const {
        return (i < kBtnBindSlots) ? btnBind_[i] : JtView::RowList::kNoRow;
    }
    bool potBank()           const { return potBank_; }
    bool buttonsToggleMode() const { return btnToggleMode_; }

    const JT::Params::ParamDesc* rowDesc(uint8_t row) const {
        return (row < rows_.count) ? JtParam::descAt(rows_.ordinal[row]) : nullptr;
    }

    float rowValue(uint8_t row) const {
        return (row < rows_.count) ? store_.get(rows_.ordinal[row]) : 0.0f;
    }

    // The section currently displayed, or 0xFF on the ENV overlay tab (which
    // shows all three at once and therefore has no single section).
    uint8_t currentSection() const;

private:
    JtParam::Store    store_;
    JtParam::Emitter  nrpn_;
    JtParam::Receiver rx_;
    PickupMode        pickup_;

    JtView::NavBar      nav_;
    JtView::RowList     list_;
    JtView::EnvPanel    env_;
    JtView::SeqPanel    seq_;
    JtView::HomePanel   home_;
    JtView::FilterPanel filt_;

    Arduino_GFX* gfx_ = nullptr;

    uint8_t page_ = 0;
    uint8_t sub_  = 0;

    // The rows currently on screen, AFTER visible_when filtering. Rebuilt when
    // the page, the sub-tab, or any parameter a visibility rule depends on
    // changes — see refreshRows().
    JtNav::RowSet rows_{};

    // ── Binding maps: physical control slot -> row index (0xFF unbound) ─────
    // Rebuilt by rebindControls() whenever the visible set changes. Row
    // INDICES, not ordinals: everything downstream (focus, LEDs, rowDesc)
    // speaks row indices, and the maps die with the row set anyway.
    static constexpr uint8_t kPotBindSlots = 16;  // 8 pots x 2 banks
    static constexpr uint8_t kEncBindSlots = 8;
    static constexpr uint8_t kBtnBindSlots = 8;
    uint8_t potBind_[kPotBindSlots] = {};
    uint8_t encBind_[kEncBindSlots] = {};
    uint8_t btnBind_[kBtnBindSlots] = {};
    bool    potBank_       = false;   // Angle8 switch: false = rows 1-8
    bool    btnToggleMode_ = false;   // Encoder8 scene switch, read per update

    uint8_t focusRow_    = JtView::RowList::kNoRow;
    const char* patchName_ = "INIT";

    // ── Sliced content erase ────────────────────────────────────────────────
    // When the content region is invalidated, it is NOT cleared in one blit —
    // that is >100 ms of blocked SPI and the root cause of the pot-lag /
    // missed-endpoint symptom. Instead render() clears one band per frame and
    // lets widgets paint only into ground already cleared (the maxY gate).
    bool    contentPending_ = false;   // an erase is in progress
    int16_t eraseY_         = 0;       // first line NOT yet cleared

    uint16_t selectPopupReqId_ = 0;
    bool     patchOverlayReq_  = false;

    // Touch state.
    bool     touchPrev_   = false;
    bool     touchMoved_  = false;
    uint8_t  touchRow_    = JtView::RowList::kNoRow;
    int16_t  touchStartX_ = 0;
    int16_t  touchStartY_ = 0;
    float    touchStartV_ = 0.0f;
    bool     dragEngaged_ = false;   // dead-zone broken; writes are live

    // Two-finger paging gesture.
    bool     twoFinger_      = false;   // a 2-contact gesture owns the input
    bool     twoFingerFired_ = false;   // fired once; ignore further travel
    int16_t  twoFingerX0_    = 0;       // primary-contact x at gesture start

    // ── Engine status feed (NRPN 0x3FFF) + HOME state ───────────────────────
    // Written by applyStatus() from the rx trampoline; read by render() for
    // the HOME dashboard and the SEQ playhead. lastRxMs_ ticks on ANY inbound
    // CC — the LINK dot is "the wire is alive", not "a param changed".
    uint8_t  voiceMask_  = 0;
    uint8_t  playStep_   = 0;
    bool     seqRunning_ = false;
    uint32_t lastRxMs_   = 0;
    uint8_t  patchSlot_  = 0xFF;
    uint16_t ordMasterVol_ = 0xFFFF;   // resolved once in begin()
    bool     homeVolDrag_  = false;    // finger owns the volume bar

    // ── SEQ edit-cursor sync (SEL row <-> grid <-> VAL row) ─────────────────
    // SEL selects a step: the grid highlights it and the VAL row LOADS its
    // value (display only, nothing sent — the engine keeps its own edit
    // cursor from the SEL write itself). VAL then fine-tunes: any change,
    // from pot, drag, encoder or inbound NRPN, writes through to the grid.
    uint8_t  seqSelStep_ = 0xFF;        // 0xFF = not yet synced on this page
    uint16_t ordSeqSel_  = 0xFFFF;      // ordinals resolved once in begin();
    uint16_t ordSeqVal_  = 0xFFFF;      // the sync runs every loop frame

    void handleButtons(ByteButtonUnit& buttons);
    void handlePots(Angle8Unit& angle);
    void handleEncoders(Encoder8Unit& encoder);
    void flushDirty();

    // Invalidate the content region (everything below the tab strip) and start
    // the sliced erase. Does NOT touch the NavBar — it detects its own changes.
    void invalidateContent();

    // Keep the SEL/VAL rows and the grid telling one story — see the member
    // note above. Called once per update(), cheap when nothing changed.
    void syncSeqEditRows();

    // Recollect the visible rows. Returns true only if the visible SET changed
    // — compared by CONTENT, not count, because a visible_when flip can swap
    // one row for another without changing the count, and the slot-keyed row
    // cache would then draw stale values against the wrong labels.
    //
    // Cheap when nothing changed (one table walk, no invalidation), which
    // matters: this runs on EVERY store write. The old version invalidated the
    // whole row cache unconditionally, so every pot tick repainted every row —
    // a hidden full-page repaint riding on every value change.
    bool refreshRows();

    // Rebuild the three binding maps from ParamDesc::control, visible order.
    void rebindControls();

    void reseedPickup();
    void retargetPickup();

    bool isOverlayTab() const;

    static float potNorm(uint8_t raw) { return static_cast<float>(raw) / 127.0f; }
};
