// =============================================================================
// FilterPanel.cpp — see FilterPanel.h.
// =============================================================================
#include "FilterPanel.h"
#include "RowList.h"

#include <math.h>
#include <string.h>

namespace JtView {
namespace {

namespace ID = JT::Params::ID;
using JT::Params::ParamDesc;

constexpr uint16_t C_BG     = 0x0000;
constexpr uint16_t C_GRID   = 0x2124;
constexpr uint16_t C_CURVE  = 0xFC00;   // orange
constexpr uint16_t C_MARK   = 0x8410;
constexpr uint16_t C_LABEL  = 0x9CD3;

constexpr int16_t kPadX = 14;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// What shape is the filter actually in?
//
// Decided from the OPTION NAME, not from a hard-coded index. The names come from
// the generated table, which comes from params.yaml — so adding a filter type to
// the yaml gives it a sensible curve here without touching this file, as long as
// the name follows the existing convention (LP/HP/BP/N + pole count).
// ─────────────────────────────────────────────────────────────────────────────

FilterPanel::Shape FilterPanel::shapeOf(const JtParam::Store& store) {
    Shape s{ true, false, false, 4 };   // default: 4-pole lowpass

    const ParamDesc* eng = JtParam::descOf(ID::FILTER_ENGINE);
    if (!eng) return s;

    const uint8_t e = JtParam::normToIndex(*eng, store.getById(ID::FILTER_ENGINE));

    // Which select actually describes the response depends on the engine — this
    // is the same split that visible_when encodes for the rows.
    const ParamDesc* d = nullptr;
    uint16_t id = 0;
    if (e == 0) { d = JtParam::descOf(ID::FILTER_MODE);    id = ID::FILTER_MODE; }
    else        { d = JtParam::descOf(ID::FILTER_VA_TYPE); id = ID::FILTER_VA_TYPE; }
    if (!d || !d->options) return s;

    const uint8_t i = JtParam::normToIndex(*d, store.getById(id));
    if (i >= d->optionCount) return s;
    const char* nm = d->options[i];
    if (!nm) return s;

    s.lowBand = s.highBand = s.notch = false;

    if      (strstr(nm, "NOTCH") || strstr(nm, "N2")) { s.notch = true;  s.lowBand = s.highBand = true; }
    else if (strstr(nm, "BP"))                        { /* band: neither end passes */ }
    else if (strstr(nm, "HP"))                        { s.highBand = true; }
    else if (strstr(nm, "AP"))                        { s.lowBand = s.highBand = true; }
    else                                              { s.lowBand = true; }   // LP and everything else

    // Pole count from the trailing digit, e.g. "SVF LP2", "Moog LP4", "4-Pole".
    s.poles = 2;
    if      (strstr(nm, "4")) s.poles = 4;
    else if (strstr(nm, "3")) s.poles = 3;
    else if (strstr(nm, "1")) s.poles = 1;
    else if (strstr(nm, "2")) s.poles = 2;

    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

void FilterPanel::draw(const JtParam::Store& store) {
    if (!gfx_) return;

    const float cut = store.getById(ID::FILTER_CUTOFF);
    const float res = store.getById(ID::FILTER_RESONANCE);

    const ParamDesc* eD = JtParam::descOf(ID::FILTER_ENGINE);
    const ParamDesc* mD = JtParam::descOf(ID::FILTER_MODE);
    const ParamDesc* vD = JtParam::descOf(ID::FILTER_VA_TYPE);
    const uint8_t eI = eD ? JtParam::normToIndex(*eD, store.getById(ID::FILTER_ENGINE)) : 0;
    const uint8_t mI = mD ? JtParam::normToIndex(*mD, store.getById(ID::FILTER_MODE)) : 0;
    const uint8_t vI = vD ? JtParam::normToIndex(*vD, store.getById(ID::FILTER_VA_TYPE)) : 0;

    // Only repaint when something the CURVE depends on has moved.
    if (!dirty_ && cut == lastCut_ && res == lastRes_
        && eI == lastEngine_ && mI == lastMode_ && vI == lastVaType_) return;

    lastCut_ = cut; lastRes_ = res;
    lastEngine_ = eI; lastMode_ = mI; lastVaType_ = vI;
    dirty_ = false;

    const Shape sh = shapeOf(store);

    const int16_t L = kPadX;
    const int16_t R = static_cast<int16_t>(kScreenW - kPadX);
    const int16_t W = static_cast<int16_t>(R - L);
    const int16_t T = kCurveY;
    const int16_t B = static_cast<int16_t>(kCurveY + kCurveH);

    // Clear ONLY the graphic band — the rows live directly below it.
    gfx_->fillRect(0, kCurveY, kScreenW, kCurveH, C_BG);
    gfx_->drawFastHLine(L, B, W, C_GRID);
    gfx_->drawFastVLine(L, T, static_cast<int16_t>(B - T), C_GRID);

    // The cutoff sits on a LOG frequency axis, and `cut` is already the
    // normalised position along that log sweep — so it maps straight to x with
    // no conversion. That is a happy consequence of storing normalised values.
    const int16_t xc = static_cast<int16_t>(L + cut * static_cast<float>(W));
    for (int16_t y = T; y < B; y += 6) gfx_->drawFastVLine(xc, y, 3, C_MARK);

    // Passband height, resonant peak, and the skirt.
    const int16_t yPass = static_cast<int16_t>(T + kCurveH * 0.30f);
    const int16_t yPeak = static_cast<int16_t>(
        yPass - res * static_cast<float>(kCurveH) * 0.26f);
    const int16_t yNull = static_cast<int16_t>(B - 4);

    // Steeper filters fall away faster. The 20 px per pole is schematic, not
    // measured — it just has to make a 4-pole visibly steeper than a 2-pole.
    const int16_t skirt = static_cast<int16_t>(sh.poles * 20);

    int16_t px = L;
    int16_t py = sh.lowBand ? yPass : yNull;

    for (int16_t x = L; x <= R; x += 3) {
        const int16_t dx = static_cast<int16_t>(x - xc);
        int16_t y;

        if (sh.notch) {
            // A null AT cutoff, passband either side.
            const int16_t a = static_cast<int16_t>(dx < 0 ? -dx : dx);
            y = (a < 14) ? static_cast<int16_t>(yNull - a * 3) : yPass;
            if (y < yPass) y = yPass;
        } else if (sh.lowBand && sh.highBand) {
            y = yPass;                                   // allpass: flat
        } else if (sh.lowBand) {                         // LOWPASS
            if (dx < -6)      y = yPass;
            else if (dx < 6)  y = yPeak;                 // resonant peak
            else              y = static_cast<int16_t>(
                                    yPeak + (dx - 6) * kCurveH / skirt);
        } else if (sh.highBand) {                        // HIGHPASS
            if (dx > 6)       y = yPass;
            else if (dx > -6) y = yPeak;
            else              y = static_cast<int16_t>(
                                    yPeak + (-dx - 6) * kCurveH / skirt);
        } else {                                         // BANDPASS
            const int16_t a = static_cast<int16_t>(dx < 0 ? -dx : dx);
            y = (a < 6) ? yPeak
                        : static_cast<int16_t>(yPeak + (a - 6) * kCurveH / skirt);
        }

        if (y < T)     y = T;
        if (y > B - 1) y = static_cast<int16_t>(B - 1);

        gfx_->drawLine(px, py, x, y, C_CURVE);
        px = x;
        py = y;
    }

    // Name the shape and the cutoff, so the curve is never the only readout.
    char buf[16];
    const ParamDesc* cD = JtParam::descOf(ID::FILTER_CUTOFF);
    if (cD) {
        JtParam::format(*cD, cut, buf, sizeof buf);
        gfx_->setTextSize(1);
        gfx_->setTextColor(C_CURVE, C_BG);
        gfx_->setCursor(static_cast<int16_t>(xc - 18),
                        static_cast<int16_t>(B - 12));   // inside the band
        gfx_->print(buf);
    }

    const ParamDesc* nD = (eI == 0) ? mD : vD;
    const uint8_t    nI = (eI == 0) ? mI : vI;
    if (nD && nD->options && nI < nD->optionCount) {
        gfx_->setTextSize(1);
        gfx_->setTextColor(C_LABEL, C_BG);
        gfx_->setCursor(static_cast<int16_t>(L + 6), static_cast<int16_t>(T + 4));
        gfx_->print(nD->options[nI]);
    }
}

} // namespace JtView
