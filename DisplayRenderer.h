// =============================================================================
// DisplayRenderer.h — ST7796 480×320 display rendering
// =============================================================================
// Renders the controller UI on the Waveshare ESP32-S3-Touch-LCD-3.5 display.
// Uses Arduino_GFX_Library with TCA9554 I/O expander for reset control.
//
// LAYOUT (480×320, landscape):
//
//   NORMAL PAGES (OSC, MIX, FLT, etc.):
//   ┌──────────────────────────────────────────────────────────────┐
//   │  HEADER: Page name │ Scene A/B                               │  28px
//   ├──────────────────────────────────────────────────────────────┤
//   │  PARAMETER GRID: 2 rows × 4 columns                        │
//   │  Each cell: label + value + bar indicator                    │
//   ├──────────────────────────────────────────────────────────────┤
//   │  FOOTER: Sub-page tabs │ Scene toggle                        │  28px
//   └──────────────────────────────────────────────────────────────┘
//
//   ENV PAGES (AMP, FILTER, PITCH envelopes):
//   ┌──────────────────────────────────────────────────────────────┐
//   │  HEADER: "AMP ENVELOPE" │ Scene A/B                          │  28px
//   ├──────────────────────────────────────────────────────────────┤
//   │                                                              │
//   │        /\                                                    │
//   │       /  \                                                  │
//   │      /    \___                                              │
//   │     /         \___       A → D → R back-to-back curves       │
//   │    /              \___   sustain is the y-vertex at the D→R  │
//   │  ─/──────────────────\─  boundary, no horizontal hold region │
//   │   ATTACK   DECAY   RELEASE                                  │
//   ├──────────────────────────────────────────────────────────────┤
//   │  FOOTER: [AMP] [FILT] [PITCH] sub-page tabs                 │  28px
//   └──────────────────────────────────────────────────────────────┘
//
//   SEQ PAGE (Step sequencer):
//   ┌──────────────────────────────────────────────────────────────┐
//   │  HEADER: "SEQUENCER"  │ Scene A/B                            │  28px
//   ├──────────────────────────────────────────────────────────────┤
//   │  DEST: FILTER   DIR: →   SYNC: 1/16   DEPTH: +12   RTG: ON   │ info row
//   │                                                              │
//   │   ▆▆     ▆▆▆▆      ▆▆     ▆▆▆      ░░    ░░    ░░    ░░     │
//   │   ▆▆ ╲   ▆▆▆▆     ▆▆      ▆▆▆      ░░    ░░    ░░    ░░     │  bars
//   │   ▆▆  ╲  ▆▆▆▆ ╱── ▆▆      ▆▆▆      ░░    ░░    ░░    ░░     │
//   │  ─▆▆───╲─▆▆▆▆╱────▆▆──────▆▆▆─────░░────░░────░░────░░──    │
//   │   1     2     3    4       5       6     7     8            │  step #
//   ├──────────────────────────────────────────────────────────────┤
//   │  FOOTER (empty — SEQ has no sub-pages)                       │  28px
//   └──────────────────────────────────────────────────────────────┘
//   Bar width = gate length CC. Slide line drawn between bar tops
//   when SEQ_SLIDE > 0. ░ = inactive (beyond active step count).
//   Whole bars area dims when SEQ_ENABLE is off.
//
// RENDERING STRATEGY:
//   - Full redraw on page / scene / sub-page change.
//   - Normal pages: per-cell partial update; refresh path leaves the static
//     label intact and only repaints the value text + bar gauge.
//   - ENV pages: changes to A/D/S/R trigger a full curve redraw (geometry
//     moves). Changes to crvA/crvD/crvR only repaint that one segment's
//     outline by drawing it in COL_BG using the OLD exponent, then drawing
//     the NEW outline. Depth-only changes repaint the depth-text rect only.
//   - SEQ pages: changes to enable/steps/gate/slide-binary repaint the bars
//     area; changes to dest/dir/sync/rate/depth/retrigger repaint the info
//     row only; a single step value change repaints just that bar's slot +
//     adjacent slide-line segments.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Config.h"
#include "TCA9554.h"
#include "PageManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// EnvParams — snapshot of all CC values needed to draw one ADSR curve.
// Populated once per frame from the PageManager CC cache.
// ─────────────────────────────────────────────────────────────────────────────
struct EnvParams {
    uint8_t a;          // Attack time   (0–127, after pot inversion)
    uint8_t d;          // Decay time    (0–127, after pot inversion)
    uint8_t s;          // Sustain level (0–127, after pot inversion)
    uint8_t r;          // Release time  (0–127, after pot inversion)
    uint8_t crvA;       // Attack curve  (0–127: 0=log, 64=linear, 127=exp)
    uint8_t crvD;       // Decay curve   (0–127)
    uint8_t crvR;       // Release curve (0–127)
    int8_t  depth;      // Pitch depth   (−63..+63, 0 = neutral)
    bool    hasDepth;   // True only on the PITCH sub-page

    // Byte-level equality for dirty detection — avoids redraw when nothing moved
    bool operator==(const EnvParams& o) const {
        return a == o.a && d == o.d && s == o.s && r == o.r
            && crvA == o.crvA && crvD == o.crvD && crvR == o.crvR
            && depth == o.depth;
    }
    bool operator!=(const EnvParams& o) const { return !(*this == o); }
};

// ─────────────────────────────────────────────────────────────────────────────
// SeqParams — snapshot of every CC the sequencer page depends on.
// Used for dirty-detection so we can route a refresh into the cheapest
// repaint path (info row only, bars-area only, or single bar + slide lines).
// ─────────────────────────────────────────────────────────────────────────────
struct SeqParams {
    // Global params (encoder-set)
    uint8_t enable;     // SEQ_ENABLE      — TOGGLE (0 / 127)
    uint8_t steps;      // SEQ_STEPS       — CONT, decoded to 1..16
    uint8_t gate;       // SEQ_GATE_LENGTH — CONT, drives bar width
    uint8_t slide;      // SEQ_SLIDE       — CONT, line shown when > 0
    uint8_t dir;        // SEQ_DIRECTION   — SELECT (Fwd/Rev/Bounce/Random)
    uint8_t dest;       // SEQ_DESTINATION — SELECT (None/Pitch/Filter/PWM/Amp)
    uint8_t sync;       // SEQ_TIMING_MODE — SELECT (Free, 1/16, …)
    uint8_t rate;       // SEQ_RATE        — CONT
    int8_t  depth;      // SEQ_DEPTH       — BIPOLAR, decoded to −64..+63
    uint8_t retrigger;  // SEQ_RETRIGGER   — TOGGLE (0 / 127)

    // Per-step values for all 16 steps (only the current scene's 8 are drawn,
    // but we keep all 16 so per-step change detection survives scene flips).
    uint8_t values[16];

    bool operator==(const SeqParams& o) const {
        if (enable    != o.enable    || steps  != o.steps
         || gate      != o.gate      || slide  != o.slide
         || dir       != o.dir       || dest   != o.dest
         || sync      != o.sync      || rate   != o.rate
         || depth     != o.depth     || retrigger != o.retrigger) return false;
        for (uint8_t i = 0; i < 16; ++i) if (values[i] != o.values[i]) return false;
        return true;
    }
    bool operator!=(const SeqParams& o) const { return !(*this == o); }
};

class DisplayRenderer {
public:
    DisplayRenderer() : tca_(Config::TCA_ADDR) {};

    // Initialise display hardware (SPI, TCA9554 reset, backlight).
    bool begin();

    // Full page redraw — call on page/scene/sub-page change.
    // Detects ENV pages and routes to the curve renderer automatically.
    void drawPage(const PageManager& pages);

    // Lightweight value refresh — redraws only what changed since last frame.
    // Normal pages: individual cells.  ENV pages: full curve if any CC moved.
    void refreshValues(const PageManager& pages);

    // Update header info (voice count, preset name, etc.)
    void updateHeader(const PageManager& pages);

    // ── Display state ───────────────────────────────────────────────────────
    Arduino_GFX* gfx() { return gfx_; }
    bool isReady() const { return ready_; }

private:
    TCA9554           tca_;
    Arduino_GFX*      gfx_   = nullptr;
    Arduino_DataBus*  bus_   = nullptr;
    bool              ready_ = false;

    // ── Normal page cache — per-cell CC values for dirty detection ─────────
    // Tracks whatever is displayed in each cell (pot or encoder value)
    uint8_t prevValues_[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    // ── ENV page cache — full parameter snapshot for dirty detection ─────────
    EnvParams prevEnv_ = {};

    // ── SEQ page cache — last drawn SeqParams snapshot for dirty detection ──
    // Also tracks the scene the cache was captured under, so a scene switch
    // forces a full redraw (handled at the higher level via drawPage).
    SeqParams prevSeq_ = {};

    // ── Layout constants ────────────────────────────────────────────────────
    static constexpr uint16_t SCREEN_W     = 480;
    static constexpr uint16_t SCREEN_H     = 320;
    static constexpr uint16_t HEADER_H     = 28;
    static constexpr uint16_t FOOTER_H     = 28;
    static constexpr uint16_t PARAM_AREA_Y = HEADER_H;
    static constexpr uint16_t PARAM_AREA_H = SCREEN_H - HEADER_H - FOOTER_H;
    static constexpr uint16_t CELL_W       = SCREEN_W / 4;
    static constexpr uint16_t CELL_H       = PARAM_AREA_H / 2;

    // Envelope curve drawing area — inset from screen edges
    static constexpr uint16_t ENV_PAD_X    = 20;   // left/right margin
    static constexpr uint16_t ENV_PAD_TOP  = 22;   // below pot dots row
    static constexpr uint16_t ENV_PAD_BOT  = 22;   // above stage labels

    // Points per curved segment (attack, decay, release).
    // 16 gives a smooth visual at modest SPI cost. There is no longer a
    // separate sustain segment — sustain is the y-vertex where decay meets
    // release. Sample count is capped to segment width at runtime so a tiny
    // segment never gets drawn with overlapping samples. There is no fill
    // under the curve any more — just the outline, dots, and labels.
    // Budget: ~48 drawLine ≈ 48 SPI ops per full envelope draw.
    static constexpr uint8_t  PTS_PER_SEG  = 16;

    // ── SEQ page layout ─────────────────────────────────────────────────────
    // Info row (DEST / DIR / SYNC / DEPTH / RETRIG) sits at the top of the
    // param area. Bars area is intentionally less than full height — leaves
    // room for the info row above and the step-number row below.
    static constexpr uint16_t SEQ_INFO_Y     = PARAM_AREA_Y + 2;            // 30
    static constexpr uint16_t SEQ_INFO_H     = 22;
    static constexpr uint16_t SEQ_BARS_Y     = SEQ_INFO_Y + SEQ_INFO_H + 8; // 60
    static constexpr uint16_t SEQ_BARS_H     = 150;
    static constexpr uint16_t SEQ_LABELS_Y   = SEQ_BARS_Y + SEQ_BARS_H + 6; // 216
    static constexpr uint16_t SEQ_PAD_X      = 16;                          // 16 px side margin
    static constexpr uint16_t SEQ_SLOT_GAP   = 8;                           // gap between slot columns
    static constexpr uint8_t  SEQ_VISIBLE    = 8;                           // bars drawn per scene

    // ── M5 Angle8 pot direction ─────────────────────────────────────────────
    // adcToCC() already inverts so clockwise = higher CC.
    // Set false to avoid double-inversion.  Only set true if adcToCC is
    // changed to a non-inverting formula.
    static constexpr bool INVERT_POT = false;

    // ── Colour theme (RGB565) ───────────────────────────────────────────────
    static constexpr uint16_t COL_BG       = 0x1082;  // dark navy
    static constexpr uint16_t COL_ENC_BG   = 0x10A2;  // slightly lighter for encoder cells
    static constexpr uint16_t COL_HEADER   = 0x18E3;  // slightly lighter
    static constexpr uint16_t COL_TEXT     = 0xFFFF;  // white
    static constexpr uint16_t COL_ENC_TEXT = 0xBDF7;  // muted white for encoder values
    static constexpr uint16_t COL_LABEL    = 0x94B2;  // grey
    static constexpr uint16_t COL_ENC_LABEL = 0x6B4D; // dimmer label for encoder cells
    static constexpr uint16_t COL_ACCENT   = 0xFC00;  // orange
    static constexpr uint16_t COL_SEEKING  = 0x4A49;  // dim
    static constexpr uint16_t COL_GRID     = 0x18C3;  // faint grid lines

    // ── Colour utilities ────────────────────────────────────────────────────
    static uint16_t toRgb565(uint32_t rgb);
    static uint16_t dimColour(uint16_t c, uint8_t shift);

    // ── Curve math ──────────────────────────────────────────────────────────
    // Map CC 0–127 to power-law exponent for curve shaping.
    //   CC  0 → 0.15 (logarithmic)   CC 64 → 1.0 (linear)   CC 127 → 5.0 (exponential)
    static float ccToExponent(uint8_t cc);

    // ── Normal page rendering ───────────────────────────────────────────────
    void drawHeader(const PageManager& pages);
    void drawFooter(const PageManager& pages);
    // labelStays = true skips the label paint and only erases / redraws the
    // value-text and bar-gauge regions of the cell. Used by refreshValues()
    // so the static label doesn't flicker on every value change.
    void drawParamCell(uint8_t col, uint8_t row, const ControlSlot& slot,
                       uint8_t value, bool seeking, uint16_t accentColour,
                       bool isEncoder = false, bool labelStays = false);

    // ── ENV page rendering ──────────────────────────────────────────────────
    EnvParams readEnvParams(const PageManager& pages) const;
    void drawEnvelopePage(const PageManager& pages);
    void refreshEnvelopePage(const PageManager& pages);
    void drawEnvelopeCurve(const EnvParams& env, uint16_t accentColour);
    // Repaint one segment (0=attack, 1=decay, 2=release) using the supplied
    // exponent and colour. Used by the partial-redraw path: erase = COL_BG +
    // OLD exponent; redraw = accent + NEW exponent. Returns nothing — caller
    // is responsible for repainting adjacent vertex dots if needed.
    void drawEnvelopeSegment(uint8_t segIdx, const EnvParams& env,
                             float exponent, uint16_t colour);
    // Repaint the four ADSR vertex dots — cheap (4 fillCircle calls), called
    // after a partial-segment redraw to restore any dot pixels the erase pass
    // clipped.
    void drawEnvelopeDots(const EnvParams& env, uint16_t accentColour);

    // ── SEQ page rendering ──────────────────────────────────────────────────
    SeqParams readSeqParams(const PageManager& pages) const;
    void drawSequencerPage(const PageManager& pages);
    void refreshSequencerPage(const PageManager& pages);
    // Info row — DEST / DIR / SYNC / DEPTH / RETRIG text strip
    void drawSeqInfoRow(const SeqParams& sp, uint16_t accentColour);
    // Whole bars area: all 8 visible bars + slide lines + step number labels
    void drawSeqBarsArea(const SeqParams& sp, uint8_t baseStep,
                         uint16_t accentColour);
    // Single bar slot (used by partial redraw when only one step value moved)
    void drawSeqBar(uint8_t slotIdx, uint8_t absStep, const SeqParams& sp,
                    uint16_t accentColour);
    // Slide line between two adjacent bars. Drawn only when sp.slide > 0 AND
    // both endpoints are within the active step count. Pass colour explicitly
    // so the same function works for erase (COL_BG) and paint.
    void drawSeqSlideLine(uint8_t fromSlotIdx, uint8_t toSlotIdx,
                          const SeqParams& sp, uint8_t baseStep,
                          uint16_t colour);
    // Geometry helpers — return the on-screen x coordinates of one slot.
    void seqSlotBounds(uint8_t slotIdx, uint16_t& slotX, uint16_t& slotW) const;
    // Bar top y given step value and bars-area dimensions. Centralised so
    // erase and redraw agree.
    int16_t seqBarTopY(uint8_t value) const;

    // TCA9554 display reset sequence
    void resetDisplay();
};
