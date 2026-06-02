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
//   │  ● ● ● ● ○ ○ ○ ○   ← 8 pot dots (4 active, 4 dim)        │
//   │                                                              │
//   │        /\                                                    │
//   │       /  \__________                                        │
//   │      /              \          ADSR curve — full area        │
//   │     /                \         shape = curve exponents        │
//   │  ──/──────────────────\──                                    │
//   │  ATTACK  DECAY  SUSTAIN  RELEASE                            │
//   ├──────────────────────────────────────────────────────────────┤
//   │  FOOTER: [AMP] [FILT] [PITCH] sub-page tabs                 │  28px
//   └──────────────────────────────────────────────────────────────┘
//
// RENDERING STRATEGY:
//   - Full redraw on page/scene/sub-page change
//   - Normal pages: partial cell updates when individual CC values change
//   - ENV pages: full curve redraw when any of the 7+ CCs change
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

    // ── Normal page cache — per-pot CC values for dirty detection ────────────
    uint8_t prevValues_[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    // ── ENV page cache — full parameter snapshot for dirty detection ─────────
    EnvParams prevEnv_ = {};

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
    static constexpr uint16_t ENV_DOT_Y    = PARAM_AREA_Y + 10;  // pot dot row

    // Points per curved segment (attack, decay, release).
    // 16 gives smooth visual; sustain is a straight line.
    // Budget: ~48 drawLine + ~48 drawFastVLine ≈ 96 SPI ops (~5 ms).
    static constexpr uint8_t  PTS_PER_SEG  = 16;

    // ── M5 Angle8 pot direction ─────────────────────────────────────────────
    // adcToCC() already inverts so clockwise = higher CC.
    // Set false to avoid double-inversion.  Only set true if adcToCC is
    // changed to a non-inverting formula.
    static constexpr bool INVERT_POT = false;

    // ── Colour theme (RGB565) ───────────────────────────────────────────────
    static constexpr uint16_t COL_BG       = 0x1082;  // dark navy
    static constexpr uint16_t COL_HEADER   = 0x18E3;  // slightly lighter
    static constexpr uint16_t COL_TEXT     = 0xFFFF;  // white
    static constexpr uint16_t COL_LABEL    = 0x94B2;  // grey
    static constexpr uint16_t COL_ACCENT   = 0xFC00;  // orange
    static constexpr uint16_t COL_SEEKING  = 0x4A49;  // dim
    static constexpr uint16_t COL_GRID     = 0x18C3;  // faint grid lines
    static constexpr uint16_t COL_DIM_DOT  = 0x2124;  // inactive pot dot

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
    void drawParamCell(uint8_t col, uint8_t row, const ControlSlot& slot,
                       uint8_t value, bool seeking, uint16_t accentColour);
    void drawValueBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint8_t value, uint16_t colour);

    // ── ENV page rendering ──────────────────────────────────────────────────
    EnvParams readEnvParams(const PageManager& pages) const;
    void drawEnvelopePage(const PageManager& pages);
    void refreshEnvelopePage(const PageManager& pages);
    void drawEnvelopeCurve(const EnvParams& env, uint16_t accentColour);

    // ── SEQ page rendering ──────────────────────────────────────────────────
    void drawSequencerPage(const PageManager& pages);
    void refreshSequencerPage(const PageManager& pages);
    void drawSequencerBar(uint8_t idx, uint8_t value, uint16_t accent,
                          uint8_t stepNum);

    // TCA9554 display reset sequence
    void resetDisplay();
};
