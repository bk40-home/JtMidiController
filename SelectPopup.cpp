// =============================================================================
// SelectPopup.cpp — implementation. See SelectPopup.h.
// =============================================================================
#include "SelectPopup.h"

using JT::Params::ParamDesc;
using JT::Params::Type;

// ---------------------------------------------------------------------------
// open — the option list comes straight from the GENERATED table. There is no
// second copy to drift, which is what made the old ParamFormat version decode
// LFO waveforms against 4 options when the engine had 6.
// ---------------------------------------------------------------------------
void SelectPopup::open(uint16_t paramId, float t) {
    const ParamDesc* d = JtParam::descOf(paramId);

    // Only SELECTs have an option list. A stray call on a continuous parameter
    // must not open an empty box.
    if (!d || d->type != Type::Select || d->optionCount == 0 || !d->options) {
        active_ = false;
        return;
    }

    paramId_   = paramId;
    desc_      = d;
    count_     = d->optionCount;
    highlight_ = JtParam::normToIndex(*d, t);
    if (highlight_ >= count_) highlight_ = static_cast<uint8_t>(count_ - 1);

    // Start the window so the current option is visible, centred where possible.
    scroll_ = (highlight_ >= VIS_ROWS)
            ? static_cast<uint8_t>(highlight_ - VIS_ROWS / 2) : 0;
    clampScroll();

    prevHighlight_ = 0xFF;   // force every row to paint
    prevScroll_    = 0xFF;
    fullRedraw_    = true;
    active_        = true;
}

void SelectPopup::clampScroll() {
    if (count_ <= VIS_ROWS) { scroll_ = 0; return; }
    if (highlight_ < scroll_) {
        scroll_ = highlight_;
    } else if (highlight_ >= scroll_ + VIS_ROWS) {
        scroll_ = static_cast<uint8_t>(highlight_ - VIS_ROWS + 1);
    }
    const uint8_t maxScroll = static_cast<uint8_t>(count_ - VIS_ROWS);
    if (scroll_ > maxScroll) scroll_ = maxScroll;
}

uint16_t SelectPopup::panelY() const {
    const uint8_t  rows = (count_ < VIS_ROWS) ? count_ : VIS_ROWS;
    const uint16_t h    = static_cast<uint16_t>(TITLE_H + rows * ROW_H);
    return (SCREEN_H > h) ? static_cast<uint16_t>((SCREEN_H - h) / 2) : 0;
}

int16_t SelectPopup::coverH() const {
    const uint8_t rows = (count_ < VIS_ROWS) ? count_ : VIS_ROWS;
    return static_cast<int16_t>(TITLE_H + rows * ROW_H);
}

// ---------------------------------------------------------------------------
// handleTouch — a tap on a row commits it; a tap outside the panel cancels.
// ---------------------------------------------------------------------------
SelectPopup::Action SelectPopup::handleTouch(uint16_t x, uint16_t y) {
    if (!active_) return Action::NONE;

    const uint16_t px   = panelX();
    const uint16_t py   = panelY();
    const uint8_t  rows = (count_ < VIS_ROWS) ? count_ : VIS_ROWS;
    const uint16_t listBottom = static_cast<uint16_t>(listTop() + rows * ROW_H);

    if (x < px || x >= px + PANEL_W || y < py || y >= listBottom) {
        return Action::CANCEL;
    }
    if (y < listTop()) return Action::NONE;   // title strip: harmless

    const uint8_t row = static_cast<uint8_t>((y - listTop()) / ROW_H);
    const uint8_t idx = static_cast<uint8_t>(scroll_ + row);
    if (idx >= count_) return Action::NONE;

    highlight_ = idx;
    return Action::COMMIT;
}

// ---------------------------------------------------------------------------
// handleEncoder — rotate to move the highlight, push to commit.
// Deliberately CLAMPS rather than wrapping: in a visible list, running off the
// end and reappearing at the top is disorienting. (The encoder acting directly
// on a parameter DOES wrap — see JtParam::step. Different context, different
// right answer.)
// ---------------------------------------------------------------------------
SelectPopup::Action SelectPopup::handleEncoder(int32_t delta, bool push) {
    if (!active_) return Action::NONE;

    if (delta != 0) {
        int32_t n = static_cast<int32_t>(highlight_) + delta;
        if (n < 0) n = 0;
        if (n >= static_cast<int32_t>(count_)) n = count_ - 1;
        highlight_ = static_cast<uint8_t>(n);
        clampScroll();
    }
    if (push) return Action::COMMIT;
    return Action::NONE;
}

// ---------------------------------------------------------------------------
// draw — full paint on the first frame, then only the rows whose highlight
// state changed (or every visible row if the scroll window moved).
// ---------------------------------------------------------------------------
void SelectPopup::draw(Arduino_GFX* gfx) {
    if (!active_ || !gfx || !desc_) return;

    const uint16_t px   = panelX();
    const uint16_t py   = panelY();
    const uint8_t  rows = (count_ < VIS_ROWS) ? count_ : VIS_ROWS;

    // Latched BEFORE the chrome block clears fullRedraw_: the row loop below
    // must paint EVERY visible row on the first frame. The previous code
    // relied on prevHighlight_=0xFF to force that, but the loop's skip test
    // compares highlight STATE — and a non-highlighted row trivially matches
    // "was not highlighted, still is not", so every row except the current
    // one was skipped and stayed unpainted. That is the "only the selected
    // option renders" defect.
    const bool paintAll = fullRedraw_;

    if (fullRedraw_) {
        // Panel + border ONLY. A full-screen wash here would force the owner
        // into a full-screen repair on close; a floating panel costs one small
        // rect both ways.
        gfx->fillRect(px, py, PANEL_W,
                      static_cast<int16_t>(TITLE_H + rows * ROW_H), COL_PANEL);
        gfx->drawRect(px, py, PANEL_W,
                      static_cast<int16_t>(TITLE_H + rows * ROW_H), COL_ACCENT);

        gfx->fillRect(px, py, PANEL_W, TITLE_H, COL_TITLE);
        gfx->setTextColor(COL_ACCENT);
        gfx->setTextSize(2);
        gfx->setCursor(px + 10, py + 9);
        gfx->print(desc_->label ? desc_->label : "SELECT");

        fullRedraw_    = false;
        prevScroll_    = scroll_;
        prevHighlight_ = 0xFF;   // force all rows below
    }

    const bool scrolled = (scroll_ != prevScroll_);
    for (uint8_t r = 0; r < rows; ++r) {
        const uint8_t idx = static_cast<uint8_t>(scroll_ + r);
        if (idx >= count_) break;

        const bool isHi  = (idx == highlight_);
        const bool wasHi = (idx == prevHighlight_);
        if (!paintAll && !scrolled && isHi == wasHi) continue;   // unchanged

        const uint16_t ry    = static_cast<uint16_t>(listTop() + r * ROW_H);
        const uint16_t rowBg = isHi ? COL_ACCENT : COL_PANEL;
        const uint16_t rowFg = isHi ? COL_PANEL  : COL_TEXT;

        gfx->fillRect(px + 2, ry, PANEL_W - 4, ROW_H, rowBg);
        gfx->setTextSize(2);
        // Explicit fg,bg: single-arg setTextColor leaves glyph backgrounds
        // transparent on this board's GFX, which renders them unreadable.
        gfx->setTextColor(rowFg, rowBg);
        gfx->setCursor(px + 12, ry + 7);
        gfx->print(desc_->options[idx] ? desc_->options[idx] : "?");
    }

    prevHighlight_ = highlight_;
    prevScroll_    = scroll_;
}
