// =============================================================================
// RowList.cpp — see RowList.h.
// =============================================================================
#include "RowList.h"

#include <math.h>

#include "HwPalette.h"

namespace JtView {
namespace {

using JT::Params::ParamDesc;

// Unified orange. One accent keeps the eye on the VALUE rather than on chrome.
constexpr uint16_t C_BG      = 0x0000;
constexpr uint16_t C_LABEL   = 0x9CD3;   // pale grey — labels recede
constexpr uint16_t C_VALUE   = 0xFFFF;   // white     — values lead
constexpr uint16_t C_ACCENT  = 0xFC00;   // orange    — focus
constexpr uint16_t C_BAR     = 0x4200;   // dark orange — the focused row's bar
constexpr uint16_t C_FOCUSBG = 0x2140;   // focused row background
constexpr uint16_t C_RULE    = 0x18E3;   // hairline between rows

// ── Hardware identity chip ──────────────────────────────────────────────────
// A small square left of the label, in the bound control's palette colour
// (HwPalette.h) — the same colour LedManager puts on that control's LED.
// Filled = turn/press it now; outline = bound, but behind a bank/mode switch.
// The label shifts right to make room ONLY when chips can exist at all, so
// nothing changes on rows that never had hardware.
constexpr int16_t kChipX    = 4;   // left inset of the chip
constexpr int16_t kChipSize = 8;   // 8x8: readable at arm's length, cheap
constexpr int16_t kLabelX   = kChipX + kChipSize + 6;   // label start (18)

} // anonymous namespace

void RowList::invalidate() {
    // NaN, not zero — zero is a legal value, and seeding with it would suppress
    // the first draw of any parameter that happens to sit at 0.
    for (uint8_t i = 0; i < JtNav::kMaxRows; ++i) lastVal_[i] = NAN;
    lastFocus_ = kNoRow;
}

void RowList::invalidateRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Same NaN trick as invalidate(), applied only where the rect overlaps.
    const int16_t rh = rowHeight();
    for (uint8_t i = 0; i < JtNav::kMaxRows; ++i) {
        int16_t rx, ry;
        rowRect(i, rx, ry);
        const bool hitX = (rx < static_cast<int16_t>(x + w)) && (x < static_cast<int16_t>(rx + kColW));
        const bool hitY = (ry < static_cast<int16_t>(y + h)) && (y < static_cast<int16_t>(ry + rh));
        if (hitX && hitY) lastVal_[i] = NAN;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout — fill the LEFT column top to bottom, then the right.
//
// Column-major, not row-major: a sub-tab's parameters arrive in table order,
// which is group order, so reading straight down one column keeps a group
// together. Row-major would interleave two unrelated groups side by side.
// ─────────────────────────────────────────────────────────────────────────────

void RowList::rowRect(uint8_t idx, int16_t& x, int16_t& y) const {
    const uint8_t n  = perColumn();
    const uint8_t col = static_cast<uint8_t>(idx / n);
    const uint8_t row = static_cast<uint8_t>(idx % n);
    x = static_cast<int16_t>(col * kColW);
    y = static_cast<int16_t>(originY() + row * rowHeight());
}

uint8_t RowList::rowAt(const JtNav::RowSet& rows, int16_t x, int16_t y) const {
    const int16_t y0 = originY();
    const int16_t rh = rowHeight();
    const uint8_t n  = perColumn();

    if (y < y0) return kNoRow;              // above the rows (graphic / tabs)

    const uint8_t col = (x < kColW) ? 0 : 1;
    const int16_t r   = static_cast<int16_t>((y - y0) / rh);
    if (r < 0 || r >= static_cast<int16_t>(n)) return kNoRow;

    const uint8_t idx = static_cast<uint8_t>(col * n + r);
    return (idx < rows.count) ? idx : kNoRow;
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint
// ─────────────────────────────────────────────────────────────────────────────

void RowList::drawAll(const JtNav::RowSet& rows, const JtParam::Store& store,
                      const uint8_t* rowHw, uint8_t focusRow) {
    if (!gfx_) return;

    // Clear ONLY the row band. On a graphical page the curve lives above
    // originY(), and clearing from kListY would erase it every frame.
    const int16_t y0 = originY();
    gfx_->fillRect(0, y0, kScreenW,
                   static_cast<int16_t>(kScreenH - y0), C_BG);

    gfx_->drawFastVLine(kColW, y0,
                        static_cast<int16_t>(kScreenH - y0), C_RULE);

    for (uint8_t i = 0; i < rows.count; ++i) {
        drawRow(rows, i, store, rowHw, i == focusRow);
        lastVal_[i] = store.get(rows.ordinal[i]);
    }
    lastFocus_ = focusRow;
}

bool RowList::drawDirty(const JtNav::RowSet& rows, const JtParam::Store& store,
                        const uint8_t* rowHw, uint8_t focusRow,
                        int16_t maxY, uint8_t budget) {
    if (!gfx_) return false;

    const int16_t rh = rowHeight();
    bool deferred      = false;
    bool focusDeferred = false;

    for (uint8_t i = 0; i < rows.count; ++i) {
        const float v = store.get(rows.ordinal[i]);

        const bool valChanged   = !(v == lastVal_[i]);   // NaN-safe: NaN != NaN
        const bool focusChanged = (i == focusRow) != (i == lastFocus_);
        if (!valChanged && !focusChanged) continue;

        int16_t rx, ry;
        rowRect(i, rx, ry);

        // Defer, don't drop: the stale cache entry keeps the row dirty, so it
        // paints on a later frame once the erase cursor passes it / budget
        // allows. This is what bounds the SPI time per frame.
        if (static_cast<int16_t>(ry + rh) > maxY || budget == 0) {
            deferred = true;
            if (focusChanged) focusDeferred = true;
            continue;
        }

        drawRow(rows, i, store, rowHw, i == focusRow);
        lastVal_[i] = v;
        --budget;
    }

    // Latch the focus cache only once every focus-affected row actually
    // painted; until then the old/new pair stays "changed" and completes on a
    // later frame. Latching early would strand a stale highlight on screen.
    if (!focusDeferred) lastFocus_ = focusRow;
    return deferred;
}

void RowList::drawRow(const JtNav::RowSet& rows, uint8_t idx,
                      const JtParam::Store& store, const uint8_t* rowHw,
                      bool focused) {
    const ParamDesc* d = JtParam::descAt(rows.ordinal[idx]);
    if (!d) return;

    const float t = store.get(rows.ordinal[idx]);

    int16_t x, y;
    rowRect(idx, x, y);
    const int16_t rh = rowHeight();

    const uint16_t bg = focused ? C_FOCUSBG : C_BG;
    gfx_->fillRect(x, y, kColW, rh, bg);

    // ── The bar: FOCUSED ROW ONLY ───────────────────────────────────────────
    // Sweep feedback belongs where the user is already looking. A bar on every
    // row would be 18 competing animations and would make the numbers — which
    // are the actual information — harder to pick out.
    if (focused) {
        if (d->bipolarUi) {
            // Bipolar: grow from the CENTRE, so "no modulation" reads as an
            // empty bar rather than a half-full one.
            const int16_t mid  = static_cast<int16_t>(kColW / 2);
            const int16_t span = static_cast<int16_t>((t - 0.5f) * kColW);
            if (span >= 0) gfx_->fillRect(mid, y, span, rh, C_BAR);
            else           gfx_->fillRect(static_cast<int16_t>(mid + span), y,
                                          static_cast<int16_t>(-span), rh, C_BAR);
        } else {
            const int16_t w = static_cast<int16_t>(t * static_cast<float>(kColW));
            if (w > 0) gfx_->fillRect(x, y, w, rh, C_BAR);
        }
    }

    gfx_->drawFastHLine(x, static_cast<int16_t>(y + rh - 1), kColW, C_RULE);

    // ── Hardware identity chip ──────────────────────────────────────────────
    // Drawn AFTER the focus bar so it stays visible on top of it. No cache of
    // its own: a tag only changes together with events that already
    // invalidate() the whole row set (rebind, bank flip, mode flip), so the
    // existing dirty path repaints it for free. Idle frames still cost zero.
    const uint8_t tag = rowHw ? rowHw[idx] : JtHw::kHwNone;
    if (tag != JtHw::kHwNone) {
        const uint16_t cc = JtHw::kSlotColour565[JtHw::hwIndex(tag)];
        // The chip's SHAPE is the control TYPE — square = pot, circle =
        // encoder, triangle = button — because one tab can bind the same
        // palette index on two different units (cyan pot AND cyan encoder),
        // and colour alone cannot tell them apart. Filled = reachable now;
        // outline = bound but behind its bank/mode switch.
        const bool    hollow = JtHw::hwHollow(tag);
        const int16_t half   = kChipSize / 2;
        const int16_t cx     = static_cast<int16_t>(x + kChipX + half);
        const int16_t cyc    = static_cast<int16_t>(y + rh / 2);
        switch (JtHw::hwType(tag)) {
            case JtHw::kHwPot:
                if (hollow) gfx_->drawRect(static_cast<int16_t>(cx - half),
                                           static_cast<int16_t>(cyc - half),
                                           kChipSize, kChipSize, cc);
                else        gfx_->fillRect(static_cast<int16_t>(cx - half),
                                           static_cast<int16_t>(cyc - half),
                                           kChipSize, kChipSize, cc);
                break;
            case JtHw::kHwEnc:
                if (hollow) gfx_->drawCircle(cx, cyc, half, cc);
                else        gfx_->fillCircle(cx, cyc, half, cc);
                break;
            default:   // kHwBtn
                if (hollow) gfx_->drawTriangle(
                        cx, static_cast<int16_t>(cyc - half),
                        static_cast<int16_t>(cx - half), static_cast<int16_t>(cyc + half),
                        static_cast<int16_t>(cx + half), static_cast<int16_t>(cyc + half), cc);
                else        gfx_->fillTriangle(
                        cx, static_cast<int16_t>(cyc - half),
                        static_cast<int16_t>(cx - half), static_cast<int16_t>(cyc + half),
                        static_cast<int16_t>(cx + half), static_cast<int16_t>(cyc + half), cc);
                break;
        }
    }

    // ── Text ────────────────────────────────────────────────────────────────
    // TRANSPARENT glyphs on the focused row, opaque elsewhere.
    //
    // This is not a style choice. On the focused row the text sits ON TOP of
    // the value bar, and the bar's colour VARIES along the row — filled to the
    // left of the value point, background to the right. Passing an opaque glyph
    // background would paint C_FOCUSBG rectangles behind each character and
    // punch visible holes through the bar wherever a letter overlapped it.
    //
    // Transparent glyphs need no background, so the bar shows through cleanly.
    // The trade is that a transparent draw does not erase what was under it —
    // which is fine, because drawRow() has already repainted the whole row rect.
    const bool overBar = focused;

    // ── Label, left ─────────────────────────────────────────────────────────
    gfx_->setTextSize(1);
    if (overBar) gfx_->setTextColor(C_ACCENT);
    else         gfx_->setTextColor(C_LABEL, bg);
    gfx_->setCursor(static_cast<int16_t>(x + kLabelX),
                    static_cast<int16_t>(y + rh / 2 - 4));
    gfx_->print(d->label);

    // ── Value, right-aligned, LARGE ─────────────────────────────────────────
    char buf[16];
    JtParam::format(*d, t, buf, sizeof buf);

    // Text size 2 == 12 px per glyph in the default GFX font.
    uint8_t n = 0;
    while (buf[n] && n < 15) ++n;
    const int16_t vw = static_cast<int16_t>(n * 12);

    gfx_->setTextSize(2);
    if (overBar) gfx_->setTextColor(C_VALUE);      // transparent, over the bar
    else         gfx_->setTextColor(C_VALUE, bg);  // opaque, erases cleanly
    gfx_->setCursor(static_cast<int16_t>(x + kColW - vw - 8),
                    static_cast<int16_t>(y + rh / 2 - 7));
    gfx_->print(buf);
}

} // namespace JtView
