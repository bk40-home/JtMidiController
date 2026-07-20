// =============================================================================
// ViewController.cpp — see ViewController.h.
// =============================================================================
#include "ViewController.h"

using JT::Params::ParamDesc;
using JT::Params::Type;

namespace {
void nrpnTrampoline(uint16_t paramId, float t, void* ctx) {
    // 0x3FFF is the engine's STATUS word (voice mask / playhead / running),
    // not a parameter — reserved by ParamBroadcast on the firmware side.
    // The receiver hands us norm; the exact 14-bit payload is recovered by
    // rounding (float carries 14 bits losslessly), keeping the shared wire
    // class untouched.
    if (paramId == JtView::kStatusAddr) {
        static_cast<ViewController*>(ctx)->applyStatus(
            static_cast<uint16_t>(lroundf(t * 16383.0f)));
        return;
    }
    static_cast<ViewController*>(ctx)->store().setQuietById(paramId, t);
}
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::begin(JtParam::CcSink sink, Arduino_GFX* gfx) {
    gfx_ = gfx;

    store_.begin();
    nrpn_.begin(sink, /*channel=*/1);
    rx_.begin(&nrpnTrampoline, this);

    nav_.begin(gfx);
    list_.begin(gfx);
    env_.begin(gfx);
    seq_.begin(gfx);
    filt_.begin(gfx);

    page_ = 0;
    sub_  = 0;

    // Resolved ONCE: ordinalOf is a linear table walk, and the SEQ edit-row
    // sync below runs every loop frame.
    ordSeqSel_ = JtParam::ordinalOf(JT::Params::ID::SEQ_STEP_SELECT);
    ordSeqVal_ = JtParam::ordinalOf(JT::Params::ID::SEQ_STEP_VALUE);
    ordMasterVol_ = JtParam::ordinalOf(JT::Params::ID::MASTER_VOLUME);

    home_.begin(gfx);

    // The ONE deliberate full-screen blit, at boot, before the loop is
    // latency-sensitive. Everything after this is partial by construction.
    if (gfx_) gfx_->fillScreen(0x0000);

    refreshRows();
    forceRedraw();
}

void ViewController::forceRedraw() {
    nav_.invalidate();
    invalidateContent();
}

void ViewController::invalidateContent() {
    contentPending_ = true;
    eraseY_         = JtView::NavBar::kContentY;
    list_.invalidate();
    env_.invalidate();
    seq_.invalidate();
    filt_.invalidate();
    home_.invalidate();
}

void ViewController::repairRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!gfx_) return;

    // One bounded fill erases what the overlay left in ground no widget owns
    // (margins, the gap below the last row). Bounded matters: the page menu is
    // 160x240 (~15 ms of SPI), the popup similar — one-off costs that size,
    // versus >100 ms for the full-screen wash this replaces.
    gfx_->fillRect(x, y, w, h, 0x0000);

    if (y < JtView::NavBar::kContentY) nav_.invalidate();

    if (static_cast<int16_t>(y + h) > JtView::NavBar::kContentY) {
        list_.invalidateRect(x, y, w, h);

        // The graphic band (46..160) is shared by all three panels; whichever
        // is on the current page repaints on its next draw() pass.
        if (y < static_cast<int16_t>(JtView::EnvPanel::kCurveY + JtView::EnvPanel::kCurveH)
            && static_cast<int16_t>(y + h) > JtView::EnvPanel::kCurveY) {
            env_.invalidate();
            seq_.invalidate();
            filt_.invalidate();
        }
    }
}

bool ViewController::isOverlayTab() const {
    const JtNav::Page& pg = JtNav::page(page_);
    return pg.hasOverlayTab && sub_ >= pg.subCount;
}

uint8_t ViewController::currentSection() const {
    if (isOverlayTab()) return 0xFF;
    const JtNav::Page& pg = JtNav::page(page_);
    const uint8_t s = (sub_ < pg.subCount) ? sub_ : 0;
    return pg.subs[s].section;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rows
//
// refreshRows() is called whenever the SET of visible rows could have changed —
// which is not only on a page/tab change, but every time a parameter that a
// visible_when rule depends on is written. Miss that second case and the screen
// keeps showing VA TYPE after you have switched to the OBXa engine.
// ─────────────────────────────────────────────────────────────────────────────

bool ViewController::refreshRows() {
    // Choose the row layout FIRST: a graphical page shares the content area
    // with its curve, so its rows are shorter and start lower. Getting this
    // wrong makes the rows paint straight over the graphic.
    const JtNav::PageKind k = JtNav::page(page_).kind;
    const bool graphical = (k == JtNav::PageKind::Envelope && !isOverlayTab())
                        || (k == JtNav::PageKind::Sequencer)
                        || (k == JtNav::PageKind::Filter);
    list_.setLayout(graphical);

    const uint8_t sec = currentSection();

    JtNav::RowSet fresh{};
    if (sec == 0xFF) {
        fresh.count = 0;            // ENV overlay: no rows, just three curves
    } else {
        JtNav::collectRows(sec, store_, fresh);
    }

    // Compare by CONTENT. Same set -> nothing to invalidate; keep the pots
    // tracking their rows' values and report "unchanged" so the caller does
    // not flush the screen.
    bool same = (fresh.count == rows_.count);
    for (uint8_t i = 0; same && i < fresh.count; ++i) {
        same = (fresh.ordinal[i] == rows_.ordinal[i]);
    }
    if (same) {
        retargetPickup();
        return false;
    }

    rows_ = fresh;
    if (focusRow_ >= rows_.count) focusRow_ = JtView::RowList::kNoRow;

    // The row cache is keyed by SLOT, so when a conditional row appears or
    // vanishes, slot N now means a different parameter and every cached value
    // below it is stale.
    list_.invalidate();

    // New visible set -> new hardware bindings, THEN new pickup targets: the
    // targets are read through the pot binding, so order matters here.
    rebindControls();
    reseedPickup();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Type-based binding — Phase F2.
//
// One pass over the visible rows, in order. Each row lands on the FIRST free
// slot of the control its table entry names. Overflow policy: pots have two
// banks (three tabs exceed 8 continuous rows; none exceeds 16); encoders and
// buttons never overflow the hardware (table maxima: 6 and 2), the guards are
// belt-and-braces for future yaml edits.
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::rebindControls() {
    for (uint8_t i = 0; i < kPotBindSlots; ++i) potBind_[i] = JtView::RowList::kNoRow;
    for (uint8_t i = 0; i < kEncBindSlots; ++i) encBind_[i] = JtView::RowList::kNoRow;
    for (uint8_t i = 0; i < kBtnBindSlots; ++i) btnBind_[i] = JtView::RowList::kNoRow;

    uint8_t nPot = 0, nEnc = 0, nBtn = 0;

    for (uint8_t i = 0; i < rows_.count; ++i) {
        const ParamDesc* d = JtParam::descAt(rows_.ordinal[i]);
        if (!d) continue;

        switch (d->control) {
            case JT::Params::Control::Pot:
                if (nPot < kPotBindSlots) potBind_[nPot++] = i;
                break;
            case JT::Params::Control::Encoder:
                if (nEnc < kEncBindSlots) encBind_[nEnc++] = i;
                break;
            case JT::Params::Control::Switch:
                if (nBtn < kBtnBindSlots) btnBind_[nBtn++] = i;
                break;
        }
    }
}

uint8_t ViewController::potBoundRow(uint8_t i) const {
    if (i >= PickupMode::kNumPots) return JtView::RowList::kNoRow;
    const uint8_t slot = static_cast<uint8_t>((potBank_ ? 8 : 0) + i);
    return potBind_[slot];
}

// Update pot targets to the rows' CURRENT values without resetting pickup.
// setTarget deliberately does not re-engage seeking — a pot that has picked up
// stays live (see PickupMode.h). The old per-write reseed put every pot back
// into seeking on every store write, which could drop a pot out mid-sweep.
void ViewController::retargetPickup() {
    for (uint8_t i = 0; i < PickupMode::kNumPots; ++i) {
        const uint8_t row = potBoundRow(i);
        if (row == JtView::RowList::kNoRow) continue;
        pickup_.setTarget(i, store_.get(rows_.ordinal[row]));
    }
}

void ViewController::reseedPickup() {
    float targets[PickupMode::kNumPots];
    for (uint8_t i = 0; i < PickupMode::kNumPots; ++i) {
        const uint8_t row = potBoundRow(i);   // binding + ACTIVE bank
        targets[i] = (row != JtView::RowList::kNoRow)
                   ? store_.get(rows_.ordinal[row]) : 0.0f;
    }
    pickup_.onPageChange(targets);
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::setPage(uint8_t p) {
    if (p >= JtNav::kPageCount || p == page_) return;
    page_ = p;
    sub_  = 0;
    focusRow_ = JtView::RowList::kNoRow;
    refreshRows();
    // Content only. The NavBar sees page_ != lastPage_ and repaints itself.
    invalidateContent();
}

void ViewController::setSubTab(uint8_t s) {
    const JtNav::Page& pg = JtNav::page(page_);
    const uint8_t n = static_cast<uint8_t>(pg.subCount + (pg.hasOverlayTab ? 1 : 0));
    if (s >= n || s == sub_) return;
    sub_ = s;
    focusRow_ = JtView::RowList::kNoRow;
    refreshRows();
    invalidateContent();
}

// ─────────────────────────────────────────────────────────────────────────────
// Per loop
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::update(Angle8Unit& angle, Encoder8Unit& encoder,
                            ByteButtonUnit& buttons) {
    // The Encoder8 scene switch selects the ByteButtons' role: OFF = page
    // shortcuts, ON = this tab's toggles. Read once per update so the mode is
    // consistent across handleButtons and the LED pass this frame.
    btnToggleMode_ = encoder.isPresent() && encoder.switchOn();

    handleButtons(buttons);
    handlePots(angle);
    handleEncoders(encoder);

    // A write may have flipped a visible_when dependency (filter engine, osc
    // wave). Rebuild, or the screen would keep drawing rows that no longer
    // apply. Cheap when nothing changed; the content is flushed ONLY when the
    // visible set actually changed — a vanished row leaves pixels behind that
    // no remaining widget owns, so the band erase has to reclaim them.
    if (store_.anyDirty()) {
        if (refreshRows()) invalidateContent();
    }

    syncSeqEditRows();

    flushDirty();
}

void ViewController::syncSeqEditRows() {
    if (JtNav::page(page_).kind != JtNav::PageKind::Sequencer) {
        seqSelStep_ = 0xFF;   // re-sync on the next visit
        return;
    }

    const uint8_t sel = static_cast<uint8_t>(
        lroundf(store_.get(ordSeqSel_) * 15.0f));

    if (sel != seqSelStep_) {
        // Selecting a step is a READ, not a write: load its value into the
        // VAL row quietly (no dirty mark, no NRPN — the engine already moved
        // its edit cursor when the SEL write itself was flushed). Stamping
        // the PREVIOUS step's value onto the new one here would corrupt the
        // pattern just by browsing it.
        store_.setQuiet(ordSeqVal_, seq_.step(sel));
        // setQuiet marks nothing dirty, so the normal per-write retarget
        // never runs — refresh the pot targets here or the pot bound to the
        // VAL row would still be seeking the PREVIOUS step's value.
        retargetPickup();
        seqSelStep_ = sel;
        return;
    }

    // Same step, VAL moved (pot, drag, encoder push/rotate, or an inbound
    // NRPN edit): write it through to the grid so the bar follows the
    // fine-tune live. The engine hears it via the normal dirty flush.
    const float v = store_.get(ordSeqVal_);
    if (!(v == seq_.step(sel))) seq_.setStep(sel, v);
}

void ViewController::flushDirty() {
    uint16_t o;
    while ((o = store_.takeDirty()) != JtParam::kNoOrdinal) {
        const ParamDesc* d = JtParam::descAt(o);
        if (d) nrpn_.sendParam(d->id, store_.get(o));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::render() {
    if (!gfx_) return;

    nav_.draw(page_, sub_, patchName_, 0, 8);   // self-guarded

    if (nav_.isMenuOpen()) {
        nav_.drawPageMenu(page_);   // self-guarded: paints once per open
        return;                     // the menu covers the content
    }

    // ── Sliced content erase ────────────────────────────────────────────────
    // One band per frame, then only widgets fully inside cleared ground may
    // paint. A page change completes over ~4 frames; input polling runs
    // between every band. See Config::UI_ERASE_BAND_PX for why.
    if (contentPending_) {
        int16_t h = Config::UI_ERASE_BAND_PX;
        if (static_cast<int16_t>(eraseY_ + h) > JtView::kScreenH) {
            h = static_cast<int16_t>(JtView::kScreenH - eraseY_);
        }
        gfx_->fillRect(0, eraseY_, JtView::kScreenW, h, 0x0000);
        eraseY_ = static_cast<int16_t>(eraseY_ + h);
        if (eraseY_ >= JtView::kScreenH) contentPending_ = false;
    }

    // maxY gates rows to cleared ground; the graphic band (46..160) draws only
    // once the cursor has passed it. Nothing paints over un-erased pixels.
    const int16_t maxY = contentPending_ ? eraseY_ : JtView::kScreenH;
    const bool gfxReady =
        maxY >= static_cast<int16_t>(JtView::EnvPanel::kCurveY
                                     + JtView::EnvPanel::kCurveH);
    uint8_t budget = Config::UI_ROW_BUDGET;

    const JtNav::Page& pg = JtNav::page(page_);

    switch (pg.kind) {
        case JtNav::PageKind::Envelope:
            if (isOverlayTab()) {
                if (gfxReady) env_.drawOverlay(store_);
            } else {
                if (gfxReady) env_.draw(currentSection(), rows_, store_, focusRow_);
                list_.drawDirty(rows_, store_, focusRow_, maxY, budget);
            }
            break;

        case JtNav::PageKind::Sequencer:
            // The highlighted bar is the SELECTED STEP (SEL row / last tap),
            // not the focused list row. The playhead is the ENGINE's, from
            // the status feed — 0xFF (no head) while stopped, so a stopped
            // sequencer never shows a stuck outline.
            if (gfxReady) seq_.draw(store_,
                                    seqRunning_ ? playStep_ : 0xFF,
                                    seqSelStep_);
            list_.drawDirty(rows_, store_, focusRow_, maxY, budget);
            break;

        case JtNav::PageKind::Home: {
            // HOME owns the whole content area — wait for the erase to
            // finish rather than gating on the graphic band like the
            // half-height panels.
            if (maxY >= JtView::kScreenH) {
                JtView::HomePanel::State st;
                st.name    = patchName_;
                st.slot    = patchSlot_;
                st.volume  = store_.get(ordMasterVol_);
                st.mask    = voiceMask_;
                st.step    = playStep_;
                st.running = seqRunning_;
                // 1.5 s: the engine heartbeats status once a second, so a
                // live wire stays solidly lit and a dead one goes dark
                // within a glance.
                st.link    = (millis() - lastRxMs_) < 1500;
                home_.draw(st);
            }
            break;
        }

        case JtNav::PageKind::Filter:
            if (gfxReady) filt_.draw(store_);
            list_.drawDirty(rows_, store_, focusRow_, maxY, budget);
            break;

        case JtNav::PageKind::List:
        default:
            list_.drawDirty(rows_, store_, focusRow_, maxY, budget);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buttons — shortcuts to the 8 pages
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::handleButtons(ByteButtonUnit& buttons) {
    for (uint8_t i = 0; i < ByteButtonUnit::kNumButtons; ++i) {
        // ── Long-press: patch overlay, ANY button, EITHER mode ──────────────
        // Checked before the short-press, Phase C pattern: the same rising
        // edge that started this hold also latched a short-press, which must
        // be swallowed here or releasing after the long-press would ALSO
        // fire a page switch / toggle.
        if (buttons.longPressed(i)) {
            buttons.clearLongPress(i);
            buttons.clearPress(i);
            patchOverlayReq_ = true;
            return;
        }

        if (!buttons.pressed(i)) continue;

        // ── Short press acts on RELEASE, not on the down edge ───────────────
        // Acting at press-down would flip the page (or a toggle) the instant
        // a finger lands, so a hold could never BECOME a long-press without
        // first firing its short action out from under it. pressed() is the
        // down-edge latch; buttonHeld() false means the finger has gone —
        // together they are the release of a hold that never went long.
        if (buttons.buttonHeld(i)) continue;
        buttons.clearPress(i);

        if (btnToggleMode_) {
            // Scene switch ON: the buttons ARE this tab's toggles. Table
            // maximum is 2 Switch rows per tab, so most buttons are unbound
            // here — and dark, so a dead press surprises nobody.
            const uint8_t row = btnBoundRow(i);
            if (row != JtView::RowList::kNoRow) {
                const uint16_t o = rows_.ordinal[row];
                const ParamDesc* d = JtParam::descAt(o);
                if (d) {
                    store_.set(o, JtParam::step(*d, store_.get(o), 1));
                    focusRow_ = row;
                }
            }
        } else {
            // Buttons map to the eight EDITING pages (1..8) — HOME is page 0,
            // reached by swipe or menu. This keeps every button exactly where
            // muscle memory learned it before HOME existed.
            setPage(static_cast<uint8_t>(i + 1));
        }
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pots — bound to the visible rows, in order, gated by pickup
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::handlePots(Angle8Unit& angle) {
    if (!angle.isPresent()) return;

    // The Angle8 unit's own switch selects the pot BANK: OFF = the first 8
    // continuous rows on this tab, ON = rows 9-16. A bank flip is a page
    // change as far as pickup is concerned — the knobs physically sit where
    // the OLD bank left them, so every pot must re-seek its NEW row's value
    // or flipping the switch would slam up to 8 parameters at once.
    const bool bank = angle.switchOn();
    if (bank != potBank_) {
        potBank_ = bank;
        reseedPickup();
    }

    for (uint8_t i = 0; i < Angle8Unit::kNumPots; ++i) {
        const uint8_t row = potBoundRow(i);
        if (row == JtView::RowList::kNoRow) continue;   // unbound: LED is dark
        if (!angle.ccChanged(i)) continue;

        const float t = potNorm(angle.ccValue(i));
        angle.clearChanged(i);

        // Suppress until the pot crosses the stored value, or changing page
        // would snap eight parameters to wherever the knobs happen to sit.
        if (!pickup_.process(i, t)) continue;

        store_.set(rows_.ordinal[row], t);
        focusRow_ = row;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoders — rows 8..15, plus push
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::handleEncoders(Encoder8Unit& encoder) {
    if (!encoder.isPresent()) return;

    for (uint8_t i = 0; i < Encoder8Unit::kNumEncoders; ++i) {
        // Encoders take the Control::Encoder rows of this tab, in visible
        // order — selects and stepped values, per the generated table.
        const uint8_t row = encBoundRow(i);

        // DRAIN the press latch even when unbound. Skipping it left presses
        // latched on unbound encoders (and on everything but ENC_MODAL while
        // a modal was open); the stale press then fired the moment a page
        // change bound that slot to a select — the "popup opens itself on
        // some page changes" defect.
        if (row == JtView::RowList::kNoRow) {
            if (encoder.pressed(i)) encoder.clearPress(i);
            continue;                                   // unbound: LED is dark
        }

        const uint16_t o = rows_.ordinal[row];
        const ParamDesc* d = JtParam::descAt(o);
        if (!d) continue;

        if (encoder.pressed(i)) {
            encoder.clearPress(i);
            if (d->type == Type::Select) {
                // Push = open the option list; rotate steps it directly. The
                // encoder route parks the SAME request the touch route does,
                // so the .ino's collect-and-open (and its tap-latch discard)
                // serves both.
                selectPopupReqId_ = d->id;
            } else if (d->type == Type::Toggle) {
                store_.set(o, JtParam::step(*d, store_.get(o), 1));
            } else {
                store_.set(o, JtParam::defaultNorm(*d));  // push = reset
            }
            focusRow_ = row;
        }

        const int32_t delta = encoder.delta(i);
        if (delta == 0) continue;

        store_.set(o, JtParam::step(*d, store_.get(o), delta));
        focusRow_ = row;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Touch — navigation AND editing
// ─────────────────────────────────────────────────────────────────────────────

void ViewController::handleTouch(const TouchInput& touch) {
    const bool    touched = touch.touched();
    const int16_t x       = static_cast<int16_t>(touch.x());
    const int16_t y       = static_cast<int16_t>(touch.y());

    // ── Two-finger paging gesture ───────────────────────────────────────────
    // Fires DURING the gesture, on crossing the travel threshold — never on
    // release, because the driver zeroes its coordinates the frame the last
    // finger lifts (TouchInput.cpp), which is exactly the corpse-coordinate
    // trap that made the old single-touch swipe read plain taps as page
    // flips. While a two-finger gesture owns the input, single-touch editing
    // is suspended; it resumes only after ALL fingers lift, so the second
    // finger landing mid-drag cannot leave a half-finished edit.
    // Gesture is suppressed while the page menu is open: paging UNDER an open
    // menu would leave the menu showing one page and the content another.
    const uint8_t pts = nav_.isMenuOpen() ? 0 : touch.pointCount();
    if (pts >= 2) {
        if (!twoFinger_) {
            twoFinger_      = true;
            twoFingerFired_ = false;
            twoFingerX0_    = x;
            touchRow_       = JtView::RowList::kNoRow;   // abort any drag
            touchPrev_      = false;
        } else if (!twoFingerFired_) {
            const int dx = static_cast<int>(x) - static_cast<int>(twoFingerX0_);
            const int adx = (dx < 0) ? -dx : dx;
            if (adx >= Config::TWO_FINGER_SWIPE_PX) {
                twoFingerFired_ = true;   // one flip per gesture
                if (dx > 0) {
                    setPage(static_cast<uint8_t>((page_ + JtNav::kPageCount - 1)
                                                 % JtNav::kPageCount));
                } else {
                    setPage(static_cast<uint8_t>((page_ + 1) % JtNav::kPageCount));
                }
            }
        }
        return;
    }
    if (twoFinger_) {
        // Down to one or zero contacts: swallow everything until a clean
        // release, so the trailing finger cannot tap or drag anything.
        if (pts == 0) twoFinger_ = false;
        return;
    }

    // ── Page menu is modal while open ───────────────────────────────────────
    if (nav_.isMenuOpen()) {
        if (touched && !touchPrev_) {
            touchPrev_ = true;
        } else if (!touched && touchPrev_) {
            touchPrev_ = false;
            const uint8_t p = nav_.menuPick(x, y);
            nav_.closePageMenu();
            if (p != 0xFF) setPage(p);
            // Repair ONLY the rect the menu covered (it overlaps the sub-tab
            // strip and the left column). If a page was picked, the content
            // erase is already pending and this just fixes the tab strip.
            repairRect(JtView::NavBar::kMenuX, JtView::NavBar::kMenuY,
                       JtView::NavBar::kMenuW, JtView::NavBar::kMenuH);
        }
        return;
    }

    // ── Press ───────────────────────────────────────────────────────────────
    if (touched && !touchPrev_) {
        touchPrev_   = true;
        touchMoved_  = false;
        dragEngaged_ = false;
        touchStartX_ = x;
        touchStartY_ = y;
        touchRow_    = JtView::RowList::kNoRow;

        // HOME volume bar: the finger owns it from the landing frame. It is
        // an ABSOLUTE control — the bar goes where the finger is — so a tap
        // sets it and a drag rides it. No dead-zone, no pickup: those exist
        // to protect RELATIVE edits from jitter, and an absolute fader wants
        // the opposite (land anywhere, be there).
        homeVolDrag_ = (JtNav::page(page_).kind == JtNav::PageKind::Home)
                    && JtView::HomePanel::volumeHit(x, y);
        if (homeVolDrag_) {
            store_.set(ordMasterVol_, JtView::HomePanel::volumeFromX(x));
            touchMoved_ = true;   // never becomes a tap action on release
            return;
        }

        uint8_t out = 0;
        const JtView::NavBar::Hit h = nav_.hitTest(page_, x, y, out);

        if (h == JtView::NavBar::Hit::Content) {
            const uint8_t r = list_.rowAt(rows_, x, y);
            if (r != JtView::RowList::kNoRow) {
                touchRow_    = r;
                focusRow_    = r;
                touchStartV_ = store_.get(rows_.ordinal[r]);
            }
        }
        return;
    }

    // ── HOME volume ride ────────────────────────────────────────────────────
    if (touched && homeVolDrag_) {
        store_.set(ordMasterVol_, JtView::HomePanel::volumeFromX(x));
        return;
    }
    if (!touched && homeVolDrag_) {
        homeVolDrag_ = false;
        touchPrev_   = false;   // consume the release; nothing else to do
        return;
    }

    // ── Drag ────────────────────────────────────────────────────────────────
    if (touched && touchRow_ != JtView::RowList::kNoRow) {
        const uint16_t o = rows_.ordinal[touchRow_];
        const ParamDesc* d = JtParam::descAt(o);
        if (!d) return;

        // Screen Y grows downward, so dragging UP raises the value.
        const int rawDy = static_cast<int>(touchStartY_) - static_cast<int>(y);
        const int absDy = (rawDy < 0) ? -rawDy : rawDy;

        // No writes until the dead-zone breaks. The FT6336 jitters a pixel or
        // two on a stationary finger; without this, every TAP nudged the
        // value it landed on (defect D2 — the old thresholds only gated the
        // release action, not the writes). Once broken it stays broken for
        // this touch, and the drag distance is offset by the dead-zone so
        // engagement is continuous — no jump at the threshold.
        if (!dragEngaged_ && absDy >= Config::DRAG_DEADZONE_PX) {
            dragEngaged_ = true;
            touchMoved_  = true;    // an engaged drag is never a tap
        }
        if (!dragEngaged_) return;

        const int dy = (rawDy > 0) ? rawDy - Config::DRAG_DEADZONE_PX
                                   : rawDy + Config::DRAG_DEADZONE_PX;

        if (d->type == Type::Select) {
            // Computed as an ABSOLUTE target from the grab baseline, not
            // accumulated per frame — accumulation drifts.
            const int want = static_cast<int>(JtParam::normToIndex(*d, touchStartV_))
                           + (dy / 12);
            const int cur  = static_cast<int>(JtParam::normToIndex(*d, store_.get(o)));
            if (want != cur) store_.set(o, JtParam::step(*d, store_.get(o), want - cur));

        } else if (d->type != Type::Toggle) {
            store_.set(o, touchStartV_
                        + (static_cast<float>(dy) / 1.5f) / 128.0f);
        }
        // Toggles: never dragged; the engaged-drag flag already cancels the tap.
        return;
    }

    // ── Release ─────────────────────────────────────────────────────────────
    if (!touched && touchPrev_) {
        touchPrev_ = false;

        // Single-touch swipe is GONE, deliberately. The driver zeroes its
        // coordinates the frame the finger lifts (TouchInput.cpp:85), so any
        // release-frame geometry here computes against (0,0) — that is what
        // made a plain tap in most of the upper-right half of the screen
        // register as a 200-plus-pixel leftward swipe and page FORWARD.
        // Paging by touch is now the two-finger gesture at the top of this
        // function, which measures travel between live frames and never
        // touches release coordinates. Everything below uses the LANDING
        // position, which is latched at the down edge and always valid.
        uint8_t out = 0;
        const JtView::NavBar::Hit h = nav_.hitTest(page_, touchStartX_,
                                                   touchStartY_, out);

        if (h == JtView::NavBar::Hit::PageMenu) {
            nav_.openPageMenu();

        } else if (h == JtView::NavBar::Hit::SubTab) {
            setSubTab(out);

        } else if (JtNav::page(page_).kind == JtNav::PageKind::Sequencer
                   && !touchMoved_
                   && JtView::SeqPanel::stepAt(touchStartX_, touchStartY_)
                      != 0xFF) {
            // ── Tap-grid step entry (standing spec: TAP, not drag) ──────────
            // The tapped bar is the step, the tapped HEIGHT is its value.
            // Wire order matters and is fixed by the engine's addressing:
            // step_select FIRST (the address), then step_value (the data).
            const uint8_t step = JtView::SeqPanel::stepAt(touchStartX_,
                                                          touchStartY_);
            const float   v    = JtView::SeqPanel::valueFromY(touchStartY_);

            // step index 0..15 -> norm on the 1..16 Int lattice: (step)/15.
            store_.setById(JT::Params::ID::SEQ_STEP_SELECT,
                           static_cast<float>(step) / 15.0f);

            // FORCED write (D-5): painting the same value onto a DIFFERENT
            // step re-writes step_value with an equal float; the plain set()
            // would skip the dirty mark and the engine would never hear it,
            // while the grid happily showed the bar.
            store_.setForce(JtParam::ordinalOf(JT::Params::ID::SEQ_STEP_VALUE),
                            v);

            // The panel's 16-entry cache is the UI's view of the pattern (the
            // store only ever holds the LAST step written — see SeqPanel.h).
            seq_.setStep(step, v);

        } else if (touchRow_ != JtView::RowList::kNoRow && !touchMoved_) {
            // A tap that never moved.
            const uint16_t o = rows_.ordinal[touchRow_];
            const ParamDesc* d = JtParam::descAt(o);
            if (d) {
                if (d->type == Type::Toggle) {
                    store_.set(o, JtParam::step(*d, store_.get(o), 1));
                } else if (d->type == Type::Select) {
                    // Park the popup request; the .ino collects it.
                    selectPopupReqId_ = d->id;
                }
            }
        }

        touchRow_ = JtView::RowList::kNoRow;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Popup + inbound MIDI
// ─────────────────────────────────────────────────────────────────────────────

uint16_t ViewController::takeSelectPopupRequest() {
    const uint16_t id = selectPopupReqId_;
    selectPopupReqId_ = 0;
    return id;
}

void ViewController::commitSelect(uint16_t paramId, uint8_t optionIndex) {
    const ParamDesc* d = JtParam::descOf(paramId);
    if (!d || d->type != Type::Select) return;
    store_.setById(paramId, JtParam::indexToNorm(*d, optionIndex));

    // A select may be a visible_when dependency (filter engine, osc wave), so
    // the row set can change the instant this commits.
    if (refreshRows()) invalidateContent();
}

bool ViewController::handleInboundCC(uint8_t cc, uint8_t value) {
    // ANY inbound CC proves the wire is alive — that is what the LINK dot
    // shows. Set before parsing: even traffic we do not consume counts.
    lastRxMs_ = millis();

    const bool consumed = rx_.handleCC(cc, value);
    if (consumed) {
        // The Teensy may have changed a visibility dependency. refreshRows()
        // retargets the pot pickups itself on the unchanged path; the full
        // reseed happens on the changed path. Either way the pots track.
        if (refreshRows()) invalidateContent();
    }
    return consumed;
}
