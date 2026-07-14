// =============================================================================
// PatchOverlay.h — modal patch browser (Phase F3, brief item 4)
// =============================================================================
// The Phase C patch UI was a PAGE (PageID::PTCH) rendered by DisplayRenderer;
// both died in the Phase E rebuild and only the entry point was lost — the
// PatchManager underneath (browse state, two-push save confirm, banners,
// rename flow) survived intact. This class is that lost entry point rebuilt
// as a MODAL on the SelectPopup pattern: a floating panel that never paints
// outside its own rect, so closing it repairs one bounded rect instead of the
// whole screen.
//
// This is a pure VIEW plus input router over PatchManager. All actions go
// through PatchManager::onAction() — the same tested route the old PTCH page
// used — so the standing confirmation rule (SAVE arms on the first push and
// commits on the second, LOAD fires once) is inherited, not re-implemented.
//
// Layout, top to bottom:
//   title    "PATCHES"
//   banner   PatchManager's timed one-shot messages (SAVED / ARMED / EMPTY…)
//   actions  [ SAVE > NN ]  [ RENAME ]     (NN = highlighted slot)
//   list     kVisRows slot rows, window follows the highlight
//
// Interactions:
//   tap a slot row   -> highlight it and LOAD it (load is non-destructive)
//   tap SAVE         -> arm; tap again within the arm window -> commit
//   tap RENAME       -> CLOSES the overlay and opens the NameEditor (the
//                       editor is full-screen; stacking it over the panel
//                       would just mean repainting both on the way out)
//   tap outside      -> close
//   encoder rotate   -> move the highlight (PatchManager clamps the range)
//   encoder push     -> LOAD the highlighted slot
//
// Opened by a LONG-PRESS on any ByteButton, either button mode — the request
// is parked in ViewController and collected by the .ino, the same pattern as
// the SelectPopup, so the modal open (and its tap-latch discard) lives in
// exactly one place.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "PatchManager.h"

class Arduino_GFX;

class PatchOverlay {

public:
    enum class Action : uint8_t {
        NONE,     // handled (or ignored); the overlay stays open
        CLOSED    // the overlay closed; the owner repairs coverX/Y/W/H
    };

    // The overlay drives PatchManager through its public router; it never
    // touches PatchStore or the parameter store directly.
    void begin(PatchManager& pm) { pm_ = &pm; }

    void open();
    void close() { active_ = false; }
    bool isActive() const { return active_; }

    // Tap coordinates from the touch latch (landing position — always valid,
    // unlike release coordinates).
    Action handleTouch(int16_t x, int16_t y);

    // The modal encoder: rotate scrolls the highlight, push loads it.
    Action handleEncoder(int32_t delta, bool push);

    // Self-guarded: paints on open and when the state it displays (highlight,
    // window, banner, loaded slot) actually changed. Idle frames are free.
    void draw(Arduino_GFX* gfx);

    // The rect the panel painted, for the owner's close-repair. Valid after
    // close() until the next open().
    int16_t coverX() const { return kPanelX; }
    int16_t coverY() const { return kPanelY; }
    int16_t coverW() const { return kPanelW; }
    int16_t coverH() const { return kPanelH; }

private:
    // ── Geometry — fixed, unlike SelectPopup's list-sized panel: the slot
    //    list is always MAX_PATCHES long, so the window never shrinks. ──────
    static constexpr int16_t kPanelW  = 380;
    static constexpr int16_t kTitleH  = 28;
    static constexpr int16_t kBannerH = 22;
    static constexpr int16_t kActionH = 30;
    static constexpr int16_t kRowH    = 28;
    static constexpr uint8_t kVisRows = 6;
    static constexpr int16_t kPanelH  = kTitleH + kBannerH + kActionH
                                      + kVisRows * kRowH;
    static constexpr int16_t kPanelX  = (480 - kPanelW) / 2;
    static constexpr int16_t kPanelY  = (320 - kPanelH) / 2;

    int16_t bannerY() const { return kPanelY + kTitleH; }
    int16_t actionY() const { return kPanelY + kTitleH + kBannerH; }
    int16_t listY()   const { return kPanelY + kTitleH + kBannerH + kActionH; }

    // Keep the highlighted slot inside the visible window; scroll the window
    // only when the highlight walks off an edge (steady while browsing).
    void followHighlight();

    void drawBanner(Arduino_GFX* gfx);
    void drawActions(Arduino_GFX* gfx);
    void drawRows(Arduino_GFX* gfx, bool all);

    PatchManager* pm_     = nullptr;
    bool          active_ = false;
    bool          fullRedraw_ = false;

    uint8_t top_ = 0;   // first slot in the visible window

    // Last-drawn state, so draw() repaints only on change — same dirty
    // discipline as every other widget after Phase F1.
    uint8_t        prevHi_     = 0xFF;
    uint8_t        prevTop_    = 0xFF;
    uint8_t        prevLoaded_ = 0xFF;
    PtchBannerType prevBanner_ = PtchBannerType::NONE;
    uint8_t        prevBSlot_  = 0xFF;
};
