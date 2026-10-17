// =============================================================================
// SeqPanel.cpp — see SeqPanel.h.
// =============================================================================
#include "SeqPanel.h"

#include <math.h>

namespace JtView {
namespace {

namespace ID = JT::Params::ID;   // ID is a NAMESPACE of constants, not a type
using JT::Params::ParamDesc;

constexpr uint16_t C_BG      = 0x0000;
constexpr uint16_t C_GRID    = 0x2124;
constexpr uint16_t C_BAR     = 0xFC00;   // orange — an active GATE-lane step
constexpr uint16_t C_BAR_AUX = 0x07FF;   // cyan   — an active AUX-lane step (D-U1)
constexpr uint16_t C_BAR_ARP = 0xF81F;   // magenta — an active ARP accent step (Phase 9)
constexpr uint16_t C_BAR_OFF = 0x4A49;   // grey   — beyond seq.steps
constexpr uint16_t C_HEAD    = 0xFFFF;   // white  — the sounding step
constexpr uint16_t C_FOCUS   = 0xFFE0;   // yellow — the step being edited
constexpr uint16_t C_CENTRE  = 0x4208;   // the zero line for bipolar steps

// Bass lane (bass-seq).  Green, distinct from the gate/aux/arp lane colours.
constexpr uint16_t C_BASS       = 0x07E0;   // note
constexpr uint16_t C_BASS_ACC   = 0xF800;   // accented note (red = emphasis)
constexpr uint16_t C_BASS_GHOST = 0x01E0;   // rest: faint mark at its degree
constexpr uint16_t C_BASS_BEAT  = 0x0841;   // alternate-beat column shading
constexpr uint16_t C_BASS_MARK  = 0xFFFF;   // octave band / hand-edit tick

// Packed step bits — mirror BassPattern.h (kept local so this file needs no
// engine header; the format is frozen in saved patches).
constexpr uint16_t B_DEGREE = 0x001Fu;
constexpr uint16_t B_GATE   = 1u << 5;
constexpr uint16_t B_ACCENT = 1u << 6;
constexpr uint16_t B_SLIDE  = 1u << 7;
constexpr uint16_t B_OCT    = 0x0300u;      // >> 8: 0 = -1, 1 = 0, 2 = +1
constexpr uint16_t B_USER   = 1u << 10;

constexpr int16_t kPadX  = 8;
constexpr int16_t kGap   = 2;

// Column width for a given length: the full 464 px grid divided evenly.  The
// last pixel of every column is a 1 px gap (also where a slide tie is drawn).
inline int16_t bassColW(uint8_t length)
{
    const uint8_t n = (length < 1) ? 1 : length;
    return static_cast<int16_t>((480 - 2 * kPadX) / n);
}

// 16 bars across 480 - 2*8 = 464 px, with a 2 px gap: 27 px each.
constexpr int16_t kBarW  = (480 - 2 * kPadX - 15 * kGap) / SeqPanel::kSteps;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Hit test
// ─────────────────────────────────────────────────────────────────────────────

uint8_t SeqPanel::stepAt(int16_t x, int16_t y) {
    if (y < kGridY || y >= kGridY + kGridH) return 0xFF;
    if (x < kPadX) return 0xFF;

    const int16_t rel = static_cast<int16_t>(x - kPadX);
    const int16_t i   = static_cast<int16_t>(rel / (kBarW + kGap));
    return (i >= 0 && i < kSteps) ? static_cast<uint8_t>(i) : 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

float SeqPanel::valueFromY(int16_t y) {
    // Unipolar, mirroring drawBar exactly: bottom edge = 0, top edge = 1.
    // (The first tap-grid pass used the bipolar mid-line mapping, which made
    // half-height taps produce near-empty bars — hard to edit by eye.)
    const float B = static_cast<float>(kGridY + kGridH);
    float v = (B - static_cast<float>(y)) / static_cast<float>(kGridH);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

void SeqPanel::drawBar(uint8_t i, float v, uint8_t activeCount,
                       bool isHead, bool isFocus) {
    const int16_t x = static_cast<int16_t>(kPadX + i * (kBarW + kGap));
    const int16_t B = static_cast<int16_t>(kGridY + kGridH);

    gfx_->fillRect(x, kGridY, kBarW, kGridH, C_BG);

    // A step beyond seq.steps still EXISTS and keeps its value — it simply is
    // not played. Drawn grey rather than hidden, so shortening the pattern does
    // not look like the steps were destroyed.
    const bool active = (i < activeCount);
    const uint16_t barCol = (lane_ == Lane::Aux) ? C_BAR_AUX
                          : (lane_ == Lane::Arp) ? C_BAR_ARP : C_BAR;   // D-U1 / Phase 9
    const uint16_t col = active ? barCol : C_BAR_OFF;

    // UNIPOLAR: the bar grows from the baseline; its height IS the value —
    // what the finger taps is what it gets. (The bipolar mid-line drawing was
    // faithful to "0.5 = no modulation" but made the grid hard to edit by
    // eye: half-value taps produced near-empty bars.)
    const int16_t h = static_cast<int16_t>(v * static_cast<float>(kGridH));
    if (h > 0) {
        gfx_->fillRect(x, static_cast<int16_t>(B - h), kBarW, h, col);
    }

    // The mid-line stays as a REFERENCE: on bipolar destinations 0.5 is "no
    // modulation", and a half-height bar sitting exactly on the line reads
    // that way at a glance. Drawn over the bar so it shows either way.
    const int16_t mid = static_cast<int16_t>(kGridY + kGridH / 2);
    gfx_->drawFastHLine(x, mid, kBarW, C_CENTRE);

    // Both outlines sit INSIDE the bar rect. The focus outline used to be
    // drawn one pixel OUTSIDE it — outside the erase above — so leaving a
    // step stranded a yellow halo on screen that nothing ever cleared.
    if (isHead)  gfx_->drawRect(x, kGridY, kBarW, kGridH, C_HEAD);
    if (isFocus) gfx_->drawRect(x, kGridY, kBarW, kGridH, C_FOCUS);

    gfx_->drawFastHLine(x, B, kBarW, C_GRID);
}

void SeqPanel::draw(const JtParam::Store& store, const uint16_t* ord16,
                    uint8_t activeCount, uint8_t playHead, uint8_t focusStep) {
    if (!gfx_ || !ord16) return;

    uint8_t count = activeCount;
    if (count < 1)      count = 1;
    if (count > kSteps) count = kSteps;

    for (uint8_t i = 0; i < kSteps; ++i) {
        // Read the step STRAIGHT from the store. Anything that moves it —
        // a tap here, a pot, an inbound NRPN from the plugin, a patch load —
        // lands in the same slot, so the bars cannot disagree with the sound.
        const float v = store.get(ord16[i]);

        const bool headMoved  = (i == playHead)  != (i == lastHead_);
        const bool focusMoved = (i == focusStep) != (i == lastFocus_);
        const bool valMoved   = !(v == lastDrawn_[i]);
        const bool countMoved = (count != lastCount_);

        if (dirty_ || valMoved || headMoved || focusMoved || countMoved) {
            drawBar(i, v, count, i == playHead, i == focusStep);
            lastDrawn_[i] = v;
        }
    }

    lastHead_  = playHead;
    lastFocus_ = focusStep;
    lastCount_ = count;
    dirty_     = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bass pattern grid
// ─────────────────────────────────────────────────────────────────────────────

uint8_t SeqPanel::bassStepAt(int16_t x, int16_t y, uint8_t length) {
    if (y < kGridY || y >= kGridY + kGridH) return 0xFF;
    if (x < kPadX) return 0xFF;
    const int16_t i = static_cast<int16_t>((x - kPadX) / bassColW(length));
    return (i >= 0 && i < length && i < kBassMaxSteps) ? static_cast<uint8_t>(i) : 0xFF;
}

uint8_t SeqPanel::bassRowAt(int16_t y) {
    // Row 0 (the root) at the BOTTOM, as on a piano roll.
    const int16_t B = static_cast<int16_t>(kGridY + kGridH);
    int16_t r = static_cast<int16_t>((B - 1 - y) / kBassRowH);
    if (r < 0) r = 0;
    if (r >= kBassRows) r = kBassRows - 1;
    return static_cast<uint8_t>(r);
}

void SeqPanel::drawBassColumn(uint8_t i, uint16_t s, bool nextGated,
                              uint8_t length, bool isHead, bool isFocus) {
    const int16_t colW = bassColW(length);
    const int16_t x    = static_cast<int16_t>(kPadX + i * colW);
    const int16_t w    = static_cast<int16_t>(colW - 1);          // content width
    const int16_t B    = static_cast<int16_t>(kGridY + kGridH);

    // Background, including this column's gap pixel (so a removed tie clears).
    // Alternate beats (4 steps) are shaded so the bar structure reads at a glance.
    const uint16_t bg = (((i / 4u) & 1u) == 0u) ? C_BASS_BEAT : C_BG;
    gfx_->fillRect(x, kGridY, colW, kGridH, bg);

    uint8_t deg = static_cast<uint8_t>(s & B_DEGREE);
    if (deg >= kBassRows) deg = kBassRows - 1;
    const int16_t yTop = static_cast<int16_t>(B - (deg + 1) * kBassRowH);

    if (s & B_GATE) {
        const uint16_t col = (s & B_ACCENT) ? C_BASS_ACC : C_BASS;
        gfx_->fillRect(static_cast<int16_t>(x + 1), static_cast<int16_t>(yTop + 1),
                       static_cast<int16_t>(w - 2), static_cast<int16_t>(kBassRowH - 1), col);

        // Slide: a tie across the gap into the next note.
        if ((s & B_SLIDE) && nextGated) {
            gfx_->fillRect(static_cast<int16_t>(x + w - 1),
                           static_cast<int16_t>(yTop + kBassRowH / 2), 2, 2, col);
        }

        // Octave jump: a band just above (+1) or below (-1) the note.
        const uint16_t oct = static_cast<uint16_t>((s & B_OCT) >> 8);
        if (oct == 2u)
            gfx_->drawFastHLine(static_cast<int16_t>(x + 1), yTop,
                                static_cast<int16_t>(w - 2), C_BASS_MARK);
        else if (oct == 0u)
            gfx_->drawFastHLine(static_cast<int16_t>(x + 1),
                                static_cast<int16_t>(yTop + kBassRowH),
                                static_cast<int16_t>(w - 2), C_BASS_MARK);
    } else {
        // A rest keeps its degree, so tapping it back on returns that pitch.
        gfx_->fillRect(static_cast<int16_t>(x + 2), static_cast<int16_t>(yTop + 3),
                       static_cast<int16_t>(w - 4), 2, C_BASS_GHOST);
    }

    // Hand edit: a short tick along the column's top edge.
    if (s & B_USER)
        gfx_->fillRect(static_cast<int16_t>(x + 1), kGridY, static_cast<int16_t>(w - 2), 2, C_BASS_MARK);

    // Outlines inside the column rect, as the bar grid does (no stranded halo).
    if (isHead)  gfx_->drawRect(x, kGridY, w, kGridH, C_HEAD);
    if (isFocus) gfx_->drawRect(x, kGridY, w, kGridH, C_FOCUS);
    gfx_->drawFastHLine(x, B, w, C_GRID);
}

void SeqPanel::drawBass(const uint16_t* packed32, uint8_t length,
                        uint8_t playHead, uint8_t focusStep) {
    if (!gfx_ || !packed32) return;
    if (length < 1) length = 1;
    if (length > kBassMaxSteps) length = kBassMaxSteps;

    // A length change moves every column: clear the whole grid once, then
    // treat everything as dirty.  Columns past the new length stay blank.
    const bool full = dirty_ || (length != bassLastLen_);
    if (full) {
        gfx_->fillRect(0, kGridY, 480, static_cast<int16_t>(kGridH + 1), C_BG);
    }

    for (uint8_t i = 0; i < length; ++i) {
        const uint16_t s        = packed32[i];
        const uint8_t  nxt      = static_cast<uint8_t>((i + 1u < length) ? i + 1u : 0u);
        // A tie never wraps on screen (it would point off the right edge).
        const bool     nextNote = (nxt != 0u) && ((packed32[nxt] & B_GATE) != 0u);
        const uint16_t key      = static_cast<uint16_t>(s | (nextNote ? 0x8000u : 0u));

        const bool headMoved  = (i == playHead)  != (i == lastHead_);
        const bool focusMoved = (i == focusStep) != (i == lastFocus_);
        if (full || key != bassLast_[i] || headMoved || focusMoved) {
            drawBassColumn(i, s, nextNote, length, i == playHead, i == focusStep);
            bassLast_[i] = key;
        }
    }

    bassLastLen_ = length;
    lastHead_    = playHead;
    lastFocus_   = focusStep;
    dirty_       = false;
    // The bar-grid filters must not survive into a later bar-lane draw.
    lastCount_   = 0xFF;
}

} // namespace JtView
