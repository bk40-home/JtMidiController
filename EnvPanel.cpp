// =============================================================================
// EnvPanel.cpp — see EnvPanel.h. Geometry ported from the Phase C DisplayRenderer.
// =============================================================================
#include "EnvPanel.h"
#include "RowList.h"

#include <math.h>

namespace JtView {
namespace {

namespace ID = JT::Params::ID;   // ID is a NAMESPACE of constants, not a type
using JT::Params::ParamDesc;

constexpr uint16_t C_BG     = 0x0000;
constexpr uint16_t C_GRID   = 0x2124;
constexpr uint16_t C_ACCENT = 0xFC00;   // orange — the active envelope

// The ALL tab. Three distinguishable hues, all warm enough to sit with the
// orange accent without looking like a different application.
constexpr uint16_t C_AMP    = 0xFC00;   // orange
constexpr uint16_t C_FILT   = 0xFFE0;   // yellow
constexpr uint16_t C_PITCH  = 0xF81F;   // magenta

constexpr uint8_t kPtsPerSeg = 24;      // samples per curve segment

// The three envelopes' ParamIDs, in one table. The generator names them
// consistently (ENV_<x>_ATTACK, _ATTACK_CURVE, _DECAY, ...), so a table beats a
// switch: adding a fourth envelope would need one row here and nothing else.
struct EnvIds { uint16_t a, aC, d, dC, s, r, rC; };

constexpr EnvIds kEnv[3] = {
    { ID::ENV_AMP_ATTACK,    ID::ENV_AMP_ATTACK_CURVE,
      ID::ENV_AMP_DECAY,     ID::ENV_AMP_DECAY_CURVE,
      ID::ENV_AMP_SUSTAIN,   ID::ENV_AMP_RELEASE,   ID::ENV_AMP_RELEASE_CURVE },
    { ID::ENV_FILTER_ATTACK, ID::ENV_FILTER_ATTACK_CURVE,
      ID::ENV_FILTER_DECAY,  ID::ENV_FILTER_DECAY_CURVE,
      ID::ENV_FILTER_SUSTAIN,ID::ENV_FILTER_RELEASE,ID::ENV_FILTER_RELEASE_CURVE },
    { ID::ENV_PITCH_ATTACK,  ID::ENV_PITCH_ATTACK_CURVE,
      ID::ENV_PITCH_DECAY,   ID::ENV_PITCH_DECAY_CURVE,
      ID::ENV_PITCH_SUSTAIN, ID::ENV_PITCH_RELEASE, ID::ENV_PITCH_RELEASE_CURVE },
};

inline int16_t clampY(int16_t y, int16_t lo, int16_t hi) {
    return (y < lo) ? lo : (y > hi) ? hi : y;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Shape
//
// Reads the SEGMENT TIMES as normalised values, not milliseconds. That is
// deliberate: the curve's horizontal axis is proportional, so what matters is
// each segment's share of the total, and the normalised value already IS that
// share. Using milliseconds would make a 10 s release swallow the whole plot
// and squash attack and decay into invisibility.
// ─────────────────────────────────────────────────────────────────────────────

EnvShape EnvPanel::shapeOf(uint8_t section, const JtParam::Store& store) {
    EnvShape e{};
    if (section < 4 || section > 6) { e.valid = false; return e; }

    const EnvIds& id = kEnv[section - 4];
    e.a      = store.getById(id.a);
    e.d      = store.getById(id.d);
    e.s      = store.getById(id.s);
    e.r      = store.getById(id.r);
    e.curveA = store.getById(id.aC);
    e.curveD = store.getById(id.dC);
    e.curveR = store.getById(id.rC);
    e.valid  = true;
    return e;
}

// Normalised slope -> the exponent used to shape a segment.
// 0.5 (the neutral default) must give exactly 1.0, i.e. a straight line, or the
// "no curve" setting would visibly bow.
float EnvPanel::exponentOf(float curveNorm) {
    if (curveNorm < 0.0f) curveNorm = 0.0f;
    if (curveNorm > 1.0f) curveNorm = 1.0f;
    // 0 -> 0.25 (fast/convex), 0.5 -> 1.0 (linear), 1 -> 4.0 (slow/concave)
    return powf(4.0f, (curveNorm - 0.5f) * 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Curve
// ─────────────────────────────────────────────────────────────────────────────

void EnvPanel::drawCurve(const EnvShape& e, uint16_t colour, bool withGrid) {
    if (!e.valid) return;

    const int16_t L = kPadX;
    const int16_t R = static_cast<int16_t>(kScreenW - kPadX);
    const int16_t W = static_cast<int16_t>(R - L);
    const int16_t T = kCurveY;
    const int16_t B = static_cast<int16_t>(kCurveY + kCurveH);
    const int16_t H = static_cast<int16_t>(B - T);

    // ── Segment widths ──────────────────────────────────────────────────────
    // A, D and R share the width PROPORTIONALLY. A zero-length segment gets
    // zero width, which draws as a clean vertical jump — correct, because a
    // zero-time segment has no shape. Release absorbs the rounding remainder so
    // the curve always ends exactly at R.
    const float tot = e.a + e.d + e.r;
    int16_t aW = 0, dW = 0, rW = 0;
    if (tot > 0.0001f) {
        aW = static_cast<int16_t>(e.a / tot * static_cast<float>(W));
        dW = static_cast<int16_t>(e.d / tot * static_cast<float>(W));
        rW = static_cast<int16_t>(W - aW - dW);
    }

    const int16_t xA = L;
    const int16_t xD = static_cast<int16_t>(xA + aW);    // peak
    const int16_t xR = static_cast<int16_t>(xD + dW);    // sustain vertex
    const int16_t xE = static_cast<int16_t>(xR + rW);    // end

    const int16_t peakY = T;
    const int16_t sustY = static_cast<int16_t>(B - e.s * static_cast<float>(H));

    if (withGrid) {
        gfx_->drawFastHLine(L, B, W, C_GRID);
        gfx_->drawFastVLine(L, T, H, C_GRID);
        // Dashed stage boundaries — skipped where a segment has collapsed, or
        // the marker would land on top of a vertex and look like a defect.
        if (aW > 0 && dW > 0)
            for (int16_t y = T; y < B; y += 7) gfx_->drawFastVLine(xD, y, 4, C_GRID);
        if (dW > 0 && rW > 0)
            for (int16_t y = T; y < B; y += 7) gfx_->drawFastVLine(xR, y, 4, C_GRID);
    }

    // ── One parametric segment: (x0,y0) -> (x1,y1) shaped by t^exp ──────────
    //
    // Every y is CLAMPED before it is drawn. This is not defensive padding: it
    // is the fix for a real "lines across the screen" glitch under extreme
    // parameter combinations in the original renderer. Do not remove it.
    auto segment = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1, float ex) {
        const int16_t w = static_cast<int16_t>(x1 - x0);

        if (w <= 0) {
            const int16_t lo = clampY((y0 < y1) ? y0 : y1, T, static_cast<int16_t>(B - 1));
            const int16_t hi = clampY((y0 > y1) ? y0 : y1, T, static_cast<int16_t>(B - 1));
            gfx_->drawFastVLine(x0, lo, static_cast<int16_t>(hi - lo + 1), colour);
            return;
        }

        // Never take more samples than the segment is wide — no overdraw on a
        // narrow segment.
        const uint8_t n = (w < kPtsPerSeg) ? static_cast<uint8_t>(w) : kPtsPerSeg;

        int16_t px = x0;
        int16_t py = clampY(y0, T, static_cast<int16_t>(B - 1));

        for (uint8_t i = 1; i <= n; ++i) {
            const float t  = static_cast<float>(i) / static_cast<float>(n);
            const float sh = powf(t, ex);
            const int16_t x = static_cast<int16_t>(x0 + t * static_cast<float>(w));
            int16_t y = static_cast<int16_t>(
                            static_cast<float>(y0)
                          + sh * static_cast<float>(y1 - y0));
            y = clampY(y, T, static_cast<int16_t>(B - 1));

            gfx_->drawLine(px, py, x, y, colour);
            px = x;
            py = y;
        }
    };

    segment(xA, B,     xD, peakY, exponentOf(e.curveA));   // attack:  0 -> peak
    segment(xD, peakY, xR, sustY, exponentOf(e.curveD));   // decay:   peak -> sustain
    segment(xR, sustY, xE, B,     exponentOf(e.curveR));   // release: sustain -> 0

    // Vertex dots — the breakpoints you would grab if this were draggable.
    gfx_->fillCircle(xD, peakY, 3, colour);
    gfx_->fillCircle(xR, sustY, 3, colour);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single envelope: curve, then the value rows underneath.
// ─────────────────────────────────────────────────────────────────────────────

void EnvPanel::draw(uint8_t section, const JtNav::RowSet& rows,
                    const JtParam::Store& store, uint8_t focusRow) {
    if (!gfx_) return;
    (void)rows; (void)focusRow;   // rows are drawn by RowList, by the caller

    const EnvShape e = shapeOf(section, store);
    if (!e.valid) return;

    // Redraw only when the SHAPE moved. A curve is ~70 draw calls; repainting it
    // every frame while the user twiddles an unrelated parameter would eat the
    // SPI budget for nothing.
    const float now[7] = { e.a, e.d, e.s, e.r, e.curveA, e.curveD, e.curveR };
    bool moved = dirty_;
    for (uint8_t i = 0; i < 7 && !moved; ++i) moved = (now[i] != last_[i]);
    if (!moved) return;

    for (uint8_t i = 0; i < 7; ++i) last_[i] = now[i];
    dirty_ = false;

    // Clear ONLY the graphic band. Clearing past kCurveH would wipe the value
    // rows that now sit immediately below it.
    gfx_->fillRect(0, kCurveY, kScreenW, kCurveH, C_BG);
    drawCurve(e, C_ACCENT, /*withGrid=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// The ALL tab — three envelopes, one set of axes.
// ─────────────────────────────────────────────────────────────────────────────

void EnvPanel::drawOverlay(const JtParam::Store& store) {
    if (!gfx_) return;

    // Same rule as the single-curve path: repaint only when a SHAPE moved.
    // Without this the overlay repainted three curves every display frame.
    const EnvShape amp = shapeOf(4, store);
    const EnvShape flt = shapeOf(5, store);
    const EnvShape pit = shapeOf(6, store);
    const float now[21] = {
        amp.a, amp.d, amp.s, amp.r, amp.curveA, amp.curveD, amp.curveR,
        flt.a, flt.d, flt.s, flt.r, flt.curveA, flt.curveD, flt.curveR,
        pit.a, pit.d, pit.s, pit.r, pit.curveA, pit.curveD, pit.curveR,
    };
    bool moved = !overValid_;
    for (uint8_t i = 0; i < 21 && !moved; ++i) moved = (now[i] != lastOver_[i]);
    if (!moved) return;
    for (uint8_t i = 0; i < 21; ++i) lastOver_[i] = now[i];
    overValid_ = true;

    gfx_->fillRect(0, kCurveY, kScreenW, kCurveH, C_BG);

    // Grid once, then each curve on top of it.
    drawCurve(amp, C_AMP, /*withGrid=*/true);
    drawCurve(flt, C_FILT,  /*withGrid=*/false);
    drawCurve(pit, C_PITCH, /*withGrid=*/false);

    // Legend, so the colours mean something.
    gfx_->setTextSize(1);
    // Inside the band — below it is the row list.
    const int16_t ly = static_cast<int16_t>(kCurveY + 4);
    gfx_->setTextColor(C_AMP,   C_BG); gfx_->setCursor(20,  ly); gfx_->print("AMP");
    gfx_->setTextColor(C_FILT,  C_BG); gfx_->setCursor(90,  ly); gfx_->print("FILTER");
    gfx_->setTextColor(C_PITCH, C_BG); gfx_->setCursor(180, ly); gfx_->print("PITCH");

    dirty_ = false;
}

} // namespace JtView
