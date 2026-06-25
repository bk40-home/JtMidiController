// =============================================================================
// PageManager.h — Page navigation, scene switching, CC dispatch
// =============================================================================
// The PageManager is the central coordinator between hardware input and MIDI
// output. It owns the current page/scene/sub-page state and the local CC
// cache. On each poll cycle it:
//
//   1. Reads ByteButton for page navigation
//   2. Reads Angle8/Encoder8 scene switches
//   3. Routes pot/encoder input through the active page mapping
//   4. Applies pickup mode for pots
//   5. Handles encoder push actions (sub-page select, toggle, reset)
//   6. Sends changed CCs to the MIDI output
//   7. Updates LED state
//
// The CC cache (ccState_[]) mirrors the Teensy's parameter state. It is
// populated from incoming MIDI (Teensy → ESP32) and from local edits.
// Pickup mode uses the cache to know where each pot should "pick up" to.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "ParamDefs.h"
#include "PageDefs.h"
#include "PageMappings.h"
#include "PickupMode.h"
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"

// Callback for sending a CC out (to MIDI router)
using SendCCCallback = void(*)(uint8_t cc, uint8_t value);

// Callback for ACTION-type encoder slots (Patch Manager scroll/load/save/
// import). encIdx identifies which encoder fired (0..7) so one callback
// can dispatch all four PTCH actions; isPush distinguishes a button push
// from a rotation, and delta carries the rotation amount (0 for a push).
using ActionCallback = void(*)(uint8_t encIdx, bool isPush, int32_t delta);

class PageManager {
public:
    PageManager() = default;

    void begin();

    // ── Per-loop processing ─────────────────────────────────────────────────
    // Call once per loop. Reads all hardware, dispatches CCs, updates state.
    void update(Angle8Unit& angle, Encoder8Unit& encoder,
                ByteButtonUnit& buttons);

    // ── MIDI output callback ────────────────────────────────────────────────
    void setSendCC(SendCCCallback cb) { sendCC_ = cb; }

    // ── ACTION encoder callback (Patch Manager) ─────────────────────────────
    void setActionCallback(ActionCallback cb) { actionCB_ = cb; }

    // ── Incoming CC from Teensy ─────────────────────────────────────────────
    // Updates local CC cache and pickup targets. Called by UartMidi callback.
    void onReceiveCC(uint8_t cc, uint8_t value);

    // ── State queries ───────────────────────────────────────────────────────
    PageID       currentPage()    const { return page_; }
    uint8_t      currentSubPage() const { return subPage_; }
    Scene        potScene()       const { return potScene_; }
    Scene        encScene()       const { return encScene_; }
    const PageMapping& activeMapping() const;

    // Get the local CC cache value
    uint8_t getCCValue(uint8_t cc) const;

    // Get a step sequencer value (0–15). Returns cached value for display.
    uint8_t getStepValue(uint8_t step) const;

    // ── Pickup state access (for display) ───────────────────────────────────
    const PickupMode& pickup() const { return pickup_; }

    // ── Patch Manager support ───────────────────────────────────────────────
    // Read-only access to the full CC cache, for PatchStore::save().
    const uint8_t* ccStateRaw()  const { return ccState_; }
    uint16_t       ccStateSize() const { return Config::CC_STATE_SIZE; }

    // Bulk-write the CC cache from a loaded patch WITHOUT sending MIDI.
    // Used after PatchStore::load() so the display reflects the new patch
    // instantly; the caller is responsible for relaying the same values to
    // the Teensy via sendCC_ separately (see PatchManager::loadSlot()).
    //
    // IMPORTANT: src must be a full Config::CC_STATE_SIZE buffer indexed by
    // raw CC number — i.e. exactly the shape PatchStore::load() fills in.
    // This is NOT indexed by PatchSchema::kPatchableCCs position. Only the
    // entries whose CC number appears in kPatchableCCs are copied into the
    // cache; everything else in src is ignored (PatchStore's file format
    // stores the full 160-byte array, but only the patchable subset is
    // meaningful — the rest is padding from however the slot was last
    // written).
    void bulkLoadCC(const uint8_t* src, uint16_t size);

    // Open the Patch Manager page directly (called by the long-press
    // handler in handlePageButtons). Public so a future dedicated control
    // could also reach it without duplicating the page-switch logic.
    void openPatchManager();

    // ── Touch navigation (Phase 1b) ─────────────────────────────────────────
    // Switch to a page (OSC..PERF). Shared by ByteButton short-press and touch
    // tab taps so both drive identical behaviour (re-select closes to HOME).
    // PTCH is not reachable here by design — long-press only.
    void selectPage(PageID target);

    // Select a sub-page by index on the current page. Used by touch sub-tab
    // taps; the encoder SUB_SEL path uses the same underlying logic. No-op if
    // the current page has no sub-pages or the index is out of range.
    void selectSubPage(uint8_t subIdx) { handleSubPageSelect(subIdx); }

    // ── Touch-to-edit parameter cells (Phase 2) ─────────────────────────────
    // Called every loop with the live touch state for the content grid.
    //   cellIdx : grid cell under the finger at PRESS time (0..7), or
    //             DisplayRenderer::kNoCell. Only read on the press edge; once a
    //             cell is grabbed, the edit sticks to it until release even if
    //             the finger drifts to another cell.
    //   curY    : current finger Y (landscape px), used for drag delta.
    //   touched : whether a finger is currently down.
    // Drag up increases value, down decreases (CONT/ENV/BIPOLAR/SELECT via the
    // shared applyEncoderDelta path). A TOGGLE flips on a tap-release that
    // never exceeded the drag threshold. Emits the same CCs as the encoders.
    void handleContentTouch(uint8_t cellIdx, uint16_t curY, bool touched);

    // Which cell (0..7) currently has the touch-edit focus, or kNoCell. The
    // renderer reads this to highlight the focused cell. Public, const.
    uint8_t editCell() const { return editCell_; }

private:
    PageID       page_     = PageID::HOME;
    uint8_t      subPage_  = 0;
    Scene        potScene_ = Scene::A;
    Scene        encScene_ = Scene::A;

    // Previous scene switch states (for edge detection)
    bool prevPotSwitch_ = false;
    bool prevEncSwitch_ = false;

    // Local CC cache — mirrors Teensy parameter state
    uint8_t ccState_[Config::CC_STATE_SIZE] = {};

    // Step sequencer value cache — 16 steps, populated by pot edits
    // and incoming Teensy CCs (SEQ_STEP_SELECT + SEQ_STEP_VALUE pairs)
    uint8_t stepValues_[16] = {};
    uint8_t lastStepSelect_  = 0;  // tracks the most recent step select CC

    // Pickup mode for pots
    PickupMode pickup_;

    // CC output callback
    SendCCCallback sendCC_ = nullptr;

    // ACTION encoder callback (Patch Manager) — nullptr until PatchManager
    // registers itself in setup(). handleEncoders() no-ops ACTION slots if
    // this isn't set, so PTCH is harmless even before wiring is complete.
    ActionCallback actionCB_ = nullptr;

    // ── Internal handlers ───────────────────────────────────────────────────
    void handlePageButtons(ByteButtonUnit& buttons);
    void handleSceneSwitches(bool potSwitch, bool encSwitch);
    void handlePots(Angle8Unit& angle);
    void handleEncoders(Encoder8Unit& encoder);
    void handleSubPageSelect(uint8_t encIdx);

    // ── Touch-edit state (Phase 2) ──────────────────────────────────────────
    // editCell_ is the grabbed cell (0..7) or kNoCell when idle. The drag
    // baseline (start Y + value at grab) lets us compute an absolute delta each
    // frame rather than accumulating rounding error. editMoved_ distinguishes a
    // value drag from a tap (TOGGLE flips only on a tap that never moved).
    static constexpr uint8_t kNoCell = 0xFF;
    uint8_t  editCell_     = kNoCell;
    uint16_t editStartY_   = 0;
    uint8_t  editStartVal_ = 0;
    bool     editMoved_    = false;
    bool     editPrevTouch_ = false;  // touch state last frame (edge detect)

    // Resolve which ControlSlot a grid cell (0..7) currently shows, mirroring
    // the pot-then-encoder fill order used by drawPage()/refreshValues(). Sets
    // outIsEncoder for callers that care. Returns nullptr for an empty cell.
    const ControlSlot* slotForCell(uint8_t cellIdx, bool& outIsEncoder) const;

    // Apply a touch drag to the grabbed cell's slot, given the current finger
    // Y. Converts travel → value delta and routes through applyEncoderDelta.
    void applyTouchDrag(const ControlSlot& slot, uint16_t curY);

    // Apply an encoder delta to a CC value based on control type
    void applyEncoderDelta(const ControlSlot& slot, int32_t delta);

    // Send a CC and update local cache
    void emitCC(uint8_t cc, uint8_t value);

    // Refresh pickup targets from current mapping and CC cache
    void refreshPickupTargets();

    // Clamp value to 0..127
    static uint8_t clamp7(int value);

    // Step sequencer: sends step select + value pair
    void handleStepPot(uint8_t potIdx, uint8_t value);
};
