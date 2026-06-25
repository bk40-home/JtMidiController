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
#include "PatchManager.h"

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

// ─────────────────────────────────────────────────────────────────────────────
// TabHit — result of a touch hit-test against the tab bars.
//   kind  : which region was hit (a page tab, a sub-page tab, or nothing)
//   index : PAGE   → 0..7, mapping to PageID OSC..PERF via (index + 1)
//           SUBTAB → 0-based sub-page index on the current page
// Returned by DisplayRenderer::hitTestTab() and consumed by
// PageManager::handleTouch(). Kept here (not in PageManager) so the tab
// geometry has a single owner: the renderer that draws the tabs.
// ─────────────────────────────────────────────────────────────────────────────
enum class TabKind : uint8_t { NONE, PAGE, SUBTAB };
struct TabHit {
    TabKind kind  = TabKind::NONE;
    uint8_t index = 0;
};

class DisplayRenderer {
public:
    DisplayRenderer() : tca_(Config::TCA_ADDR) {};

    // Initialise display hardware (SPI, TCA9554 reset, backlight).
    bool begin();

    // Full page redraw — call on page/scene/sub-page change.
    // Detects ENV pages and routes to the curve renderer automatically.
    void drawPage(const PageManager& pages);

    // PTCH-aware overload. Call this one instead of the above whenever a
    // PatchManager instance is available (i.e. always, from the .ino) —
    // it falls through to the plain overload for every page except PTCH,
    // so there's no behaviour change for OSC/MIX/FLT/etc. Kept as a
    // separate overload rather than changing drawPage's signature so
    // nothing else in the codebase needs updating.
    void drawPage(const PageManager& pages, const PatchManager& patches);

    // Lightweight value refresh — redraws only what changed since last frame.
    // Normal pages: individual cells.  ENV pages: full curve if any CC moved.
    void refreshValues(const PageManager& pages);

    // PTCH-aware overload — see drawPage(pages, patches) above for rationale.
    void refreshValues(const PageManager& pages, const PatchManager& patches);

    // Update header info (voice count, preset name, etc.)
    void updateHeader(const PageManager& pages);

    // ── Touch hit-testing ─────────────────────────────────────────────────
    // Map a tap coordinate (landscape 480×320, matching TouchInput's output)
    // to a tab region. Pure geometry — no drawing, no state. The caller passes
    // whether the current page has sub-pages and how many, so the sub-tab
    // strip is only tested when it's actually on screen. Returns kind=NONE for
    // taps that miss every tab (content area, scene indicator zone, gaps).
    static TabHit hitTestTab(uint16_t x, uint16_t y,
                             bool pageHasSub, uint8_t subCount);

    // Map a tap coordinate to a parameter-grid cell index (0..7), or 0xFF if
    // the tap is outside the grid (header, footer, sub-tab strip). pageHasSub
    // selects the correct content-top offset and cell height so it agrees with
    // the param-grid renderer. Used by tap-to-edit (Phase 2).
    static constexpr uint8_t kNoCell = 0xFF;
    static uint8_t hitTestCell(uint16_t x, uint16_t y, bool pageHasSub);

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

    // Per-frame content-area top. Equals PARAM_AREA_Y on flat pages, or
    // PARAM_AREA_Y + SUBTAB_H on sub-paged pages (room for the sub-tab strip).
    // Set at the start of each grid/ENV draw+refresh; the param grid and ENV
    // curve read this instead of the PARAM_AREA_Y constant so their content
    // sits below the strip. SEQ/PTCH/HOME never set or read it.
    uint16_t paramTop_ = PARAM_AREA_Y;

    // Per-frame cell height. PARAM_AREA_H/2 on flat pages; (PARAM_AREA_H -
    // SUBTAB_H)/2 on sub-paged pages, so the 2-row grid still fits between the
    // sub-tab strip and the footer. Set alongside paramTop_.
    uint16_t cellH_ = PARAM_AREA_H / 2;

    // Touch-edit focus highlight tracking (Phase 2). Remembers which cell last
    // had the focus border so refreshValues() can erase the old border and
    // draw the new one only when focus moves — no per-frame full redraw.
    uint8_t prevEditCell_ = 0xFF;  // 0xFF = none drawn

    // Perf-indicator dirty tracking. The header's MODE/target readout must
    // update when those CCs change even if the page doesn't — refreshValues()
    // repaints just the indicator when either differs from these caches.
    // 0xFF = "never drawn", forcing a paint on the first refresh.
    uint8_t prevPerfMode_   = 0xFF;
    uint8_t prevEditTarget_ = 0xFF;

    // ── ENV page cache — full parameter snapshot for dirty detection ─────────
    EnvParams prevEnv_ = {};

    // ── SEQ page cache — last drawn SeqParams snapshot for dirty detection ──
    // Also tracks the scene the cache was captured under, so a scene switch
    // forces a full redraw (handled at the higher level via drawPage).
    SeqParams prevSeq_ = {};

    // ── PTCH page cache — last drawn state for dirty detection ──────────────
    uint8_t        prevPtchHighlight_ = 0xFF;  // 0xFF = "never drawn"
    PtchBannerType prevPtchBanner_    = PtchBannerType::NONE;

    // ── Layout constants ────────────────────────────────────────────────────
    static constexpr uint16_t SCREEN_W     = 480;
    static constexpr uint16_t SCREEN_H     = 320;
    static constexpr uint16_t HEADER_H     = 28;
    static constexpr uint16_t FOOTER_H     = 28;
    static constexpr uint16_t PARAM_AREA_Y = HEADER_H;
    static constexpr uint16_t PARAM_AREA_H = SCREEN_H - HEADER_H - FOOTER_H;
    static constexpr uint16_t CELL_W       = SCREEN_W / 4;
    static constexpr uint16_t CELL_H       = PARAM_AREA_H / 2;

    // ── Tab bar geometry (Phase 1b) ─────────────────────────────────────────
    // The header (top) and footer (bottom) rows double as the two page-tab
    // rows: OSC/MIX/FLT/ENV on top, LFO/FX/SEQ/PERF on the bottom — so the tab
    // bar costs ZERO extra vertical space over the original header+footer.
    // Geometry lives here because the renderer owns it; hitTestTab() and the
    // draw code share these exact constants (single source of truth).
    //
    // Four tabs per row, left-aligned, leaving the top row's right edge free
    // for the scene indicator. The HIT zone for each tab is the FULL row
    // height (HEADER_H / FOOTER_H), even though the drawn pill is shorter —
    // a fingertip lands anywhere in the row, not just on the 22px pill.
    static constexpr uint16_t TAB_W      = 88;  // drawn + hit width per tab
    static constexpr uint16_t TAB_PAD_X  = 2;   // left margin of the first tab
    static constexpr uint16_t TAB_PILL_M = 2;   // pill inset inside the row
    static constexpr uint8_t  TABS_PER_ROW = 4;

    // Sub-page tab strip — a thin band at the TOP of the content area, shown
    // only on pages with sub-pages (OSC/FLT/ENV/LFO/FX). Pushes those pages'
    // content down by SUBTAB_H; flat pages are unaffected.
    static constexpr uint16_t SUBTAB_Y   = HEADER_H;      // directly below header
    static constexpr uint16_t SUBTAB_H   = 16;
    static constexpr uint16_t SUBTAB_W   = 80;            // centred row of tabs

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

    // ── PTCH page layout ─────────────────────────────────────────────────────
    // List of Config::PTCH_VISIBLE_ROWS rows, each showing "SLOT NNN  Name".
    // A banner strip overlays the bottom of the list area when active
    // (LOADED / SAVED / SAVE_ARMED / etc.) rather than shifting the list,
    // so the highlighted row position never jumps when a banner appears.
    static constexpr uint16_t PTCH_PAD_X     = 16;
    static constexpr uint16_t PTCH_ROW_Y     = PARAM_AREA_Y + 6;
    static constexpr uint16_t PTCH_ROW_H     = 32;
    static constexpr uint16_t PTCH_BANNER_H  = 28;
    // Banner sits at the bottom of the param area, just above the footer.
    static constexpr uint16_t PTCH_BANNER_Y  =
        PARAM_AREA_Y + PARAM_AREA_H - PTCH_BANNER_H;

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
    // Tab-bar helpers (Phase 1b). drawPageTab renders one page-tab pill into a
    // given row; drawSubTabs renders the thin sub-page strip atop the content
    // area on sub-paged pages. Geometry matches hitTestTab() via the shared
    // TAB_* / SUBTAB_* constants.
    void drawPageTab(uint8_t tabIdx, const char* shortLabel,
                     const char* fullName, uint16_t colour, bool active,
                     uint16_t rowY, uint16_t rowH);
    void drawSubTabs(const PageManager& pages);

    // Performance MODE + edit-target readout at the header's right edge.
    // Replaces the old scene-switch "S:A/B" text. Reads PERF_MODE /
    // PERF_EDIT_TARGET from the CC cache the synth mirrors.
    void drawPerfIndicator(const PageManager& pages);

    // Repaint the perf indicator only when MODE/target changed (called from
    // refreshValues so edit-target switches show without a full page redraw).
    void refreshPerfIndicator(const PageManager& pages);

    // Draw or erase the touch-edit focus border around a grid cell. `on` true
    // paints an accent border; false erases it (redraws the cell's border in
    // the background colour). Cell index 0..7; no-op for kNoCell/0xFF.
    void drawCellFocus(uint8_t cellIdx, bool on, uint16_t accent);

    // Vertical offset applied to the content area on pages that show the
    // sub-tab strip (OSC/FLT/ENV/LFO/FX). Returns SUBTAB_H so the param grid
    // and ENV curve start BELOW the strip; returns 0 on flat pages (SEQ/PTCH/
    // HOME), which have no strip and must not move. Centralised so the param
    // grid and ENV renderer agree on where their content begins.
    static uint16_t contentTopOffset(PageID page) {
        return PageMap::hasSubPages(page) ? SUBTAB_H : 0;
    }
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

    // ── PTCH page rendering (Patch Manager) ─────────────────────────────────
    // No CC values involved — this is a list browser over PatchStore slots
    // plus a timed banner, so it has no analogue to the ENV/SEQ "snapshot
    // struct + operator==" dirty-detection pattern. Instead, refresh just
    // tracks the last-drawn highlighted slot and banner type/text directly;
    // any change to either triggers a redraw of the affected region only.
    void drawPatchPage(const PatchManager& patches);
    void refreshPatchPage(const PatchManager& patches);
    // One row of the browse list (slot number + name, or "-- EMPTY --").
    // highlighted = true draws the selection background + accent text.
    // Takes the PatchManager reference so it can resolve the slot's name
    // via PatchManager::slotName() — keeps PatchStore access funnelled
    // through PatchManager rather than DisplayRenderer reaching past it.
    void drawPtchRow(const PatchManager& patches, uint8_t rowIdx,
                     uint8_t slot, bool highlighted);
    // Banner strip — drawn over the bottom of the list area when active,
    // erased (redraw rows underneath) when it expires.
    void drawPtchBanner(const PatchManager& patches);
    void erasePtchBanner(const PatchManager& patches);

    // TCA9554 display reset sequence
    void resetDisplay();
};
