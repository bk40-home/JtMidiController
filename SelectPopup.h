// =============================================================================
// SelectPopup.h — modal scrollable dropdown for SELECT parameters (Phase D)
// =============================================================================
// A tap on a SELECT cell opens this overlay: a scrollable list of the option
// strings, current value highlighted. Tap an entry to choose, tap outside to
// dismiss. Encoder fallback: rotate to move the highlight, push to commit.
//
// PHASE D CHANGE — KEYED BY ParamID, NOT CC
//   Was: open(uint8_t cc, uint8_t value), pulling its option list from
//   ParamFormat — a HAND-WRITTEN table that had drifted from params.yaml. Its
//   LFO waveform list had 4 entries when the engine had 6, so every non-zero
//   value decoded to the wrong waveform and S&H/NOISE were unreachable.
//
//   Now: open(uint16_t paramId, float t), reading the option list straight from
//   the GENERATED table. The popup cannot disagree with the engine about what
//   options exist, because there is only one list.
//
// IT WAS ALSO DEAD CODE
//   This component was fully implemented in Phase C and NEVER INSTANTIATED.
//   PageManager parked a popup request that nothing ever collected, which is
//   why tapping a string value did nothing at all. ViewController still parks
//   the request (takeSelectPopupRequest()) — the .ino now collects it.
//
// Mirrors NameEditor's contract so it drops into the SAME modal dispatch slot:
//   open(paramId, t)           -> active, highlight seeded from t
//   handleTouch(x, y)          -> Action { NONE, COMMIT, CANCEL }
//   handleEncoder(delta, push) -> same
//   draw(gfx)                  -> render overlay (full, then dirty rows)
//   On COMMIT the owner reads resultParamId()/resultIndex(), then close().
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "Config.h"
#include "JtParamModel.h"

class SelectPopup {
public:
    enum class Action : uint8_t { NONE, COMMIT, CANCEL };

    // Open for `paramId`, seeding the highlight from normalised value `t`.
    // No-op (stays inactive) if the parameter is not a SELECT — a stray call
    // cannot open an empty box.
    void open(uint16_t paramId, float t);

    void close()          { active_ = false; }
    bool isActive() const { return active_; }

    Action handleTouch(uint16_t x, uint16_t y);
    Action handleEncoder(int32_t delta, bool push);

    void draw(Arduino_GFX* gfx);

    // Valid after COMMIT.
    uint16_t resultParamId() const { return paramId_; }
    uint8_t  resultIndex()   const { return highlight_; }

    // The rect the popup painted, for the owner's close-repair. The popup is a
    // FLOATING PANEL — it no longer scorches the whole screen on open, so on
    // close only this rect needs repainting, not the full display. Geometry
    // stays valid after close() (close only clears the active flag) until the
    // next open().
    int16_t coverX() const { return static_cast<int16_t>(panelX()); }
    int16_t coverY() const { return static_cast<int16_t>(panelY()); }
    int16_t coverW() const { return static_cast<int16_t>(PANEL_W); }
    int16_t coverH() const;

private:
    static constexpr uint16_t SCREEN_W = 480;
    static constexpr uint16_t SCREEN_H = 320;
    static constexpr uint16_t TITLE_H  = 34;
    static constexpr uint16_t ROW_H    = 30;
    static constexpr uint8_t  VIS_ROWS = 8;
    static constexpr uint16_t MARGIN_X = 60;
    static constexpr uint16_t PANEL_W  = SCREEN_W - 2 * MARGIN_X;

    static constexpr uint16_t COL_BG     = 0x1082;
    static constexpr uint16_t COL_PANEL  = 0x0000;
    static constexpr uint16_t COL_TEXT   = 0xFFFF;
    static constexpr uint16_t COL_ACCENT = 0xFC00;   // orange
    static constexpr uint16_t COL_TITLE  = 0x18E3;

    void     clampScroll();
    uint16_t panelX() const { return MARGIN_X; }
    uint16_t panelY() const;
    uint16_t listTop() const { return panelY() + TITLE_H; }

    bool     active_     = false;
    bool     fullRedraw_ = true;
    uint16_t paramId_    = 0;

    // Borrowed from the generated table — static storage, never freed.
    const JT::Params::ParamDesc* desc_ = nullptr;

    uint8_t count_         = 0;
    uint8_t highlight_     = 0;
    uint8_t scroll_        = 0;
    uint8_t prevHighlight_ = 0xFF;
    uint8_t prevScroll_    = 0xFF;
};
