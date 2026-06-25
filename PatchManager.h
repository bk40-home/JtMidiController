// =============================================================================
// PatchManager.h — Patch Manager page coordinator
// =============================================================================
// Sits between the PTCH page's encoder actions (SCROLL/LOAD/SAVE/IMPORT) and
// the actual storage (PatchStore on LittleFS) + relay target (PageManager's
// CC cache and the MIDI send callback). Neither PatchStore nor PageManager
// know this class exists — they're called, never call back into it — so
// this is purely a coordinator, not a dependency anyone else needs.
//
// WHY THIS EXISTS (rather than putting save/load logic inside PageManager):
//   PageManager's job is hardware → CC routing for every page. Patch
//   browsing/saving/loading is a distinct workflow with its own state
//   (scroll position, save-arm timer, banner text) that would otherwise
//   bloat PageManager with concerns unrelated to its existing job.
//
// CONFIRMATION RULE (per project standing instruction):
//   SAVE is the only destructive action here and always requires two
//   pushes — first arms, second (within Config::PTCH_SAVE_ARM_MS) commits.
//   LOAD is non-destructive (doesn't touch storage) so it fires on a single
//   push, but still shows a banner — nothing happens silently.
//
// IMPORT is STUBBED in this pass — porting the JP-8000 SysEx byte-layout
// parser to fixed-buffer Arduino code is its own task with its own
// diagnosis/sign-off, agreed separately. Pushing IMPORT here just shows a
// "COMING SOON" banner so the control isn't dead air.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "PatchStore.h"
#include "PageManager.h"
#include "NameEditor.h"

// Matches the encoder index order in PageMappings.h kPtchMapping::encsA.
// Kept as named constants rather than magic numbers in PatchManager.cpp.
namespace PtchEncoder {
    static constexpr uint8_t SCROLL = 0;
    static constexpr uint8_t LOAD   = 1;
    static constexpr uint8_t SAVE   = 2;
    static constexpr uint8_t IMPORT = 3;
    static constexpr uint8_t RENAME = 4;   // push opens the name editor
}

// What the PTCH page should currently show in its banner area. NONE means
// "no banner — show the normal browse list." Each other value is a
// timed, one-shot message that reverts to NONE after its duration elapses.
enum class PtchBannerType : uint8_t {
    NONE,
    LOADED,        // "LOADED: <name>"
    SAVED,         // "SAVED — SLOT N"
    SAVE_ARMED,    // "SAVE SLOT N? PUSH AGAIN"
    SAVE_CANCELLED,// "SAVE CANCELLED" — user backed out (scrolled away or timed out)
    SAVE_FAILED,   // "SAVE FAILED — SEE SERIAL LOG" — PatchStore::save() returned
                   // false (LittleFS not mounted, file open/write error). A
                   // genuine storage failure, NOT the user backing out — kept
                   // distinct from SAVE_CANCELLED so the banner alone tells you
                   // which one happened, rather than both looking identical.
    EMPTY_SLOT,    // "SLOT EMPTY — NOTHING TO LOAD"
    COMING_SOON,   // "SYX IMPORT — COMING SOON"
    RENAMED        // "RENAMED" — a patch or performance name was committed
};

class PatchManager {
public:
    PatchManager() = default;

    // Wires this manager to its dependencies. Call once from setup(), after
    // patchStore.begin() and before the main loop starts.
    void begin(PatchStore& store, PageManager& pages, SendCCCallback sendCC);

    // Call once per loop while PTCH is the active page. Cheap no-op
    // otherwise (just a banner-timeout check), safe to call unconditionally.
    void update();

    // Registered with PageManager::setActionCallback(). Free function
    // signature (ActionCallback is a plain function pointer, not a
    // std::function, to avoid pulling <functional> onto an ESP32 hot path)
    // — see the .cpp for how it forwards to the singleton instance.
    static void onAction(uint8_t encIdx, bool isPush, int32_t delta);

    // ── Display query API ───────────────────────────────────────────────────
    // DisplayRenderer reads these to draw the browse list + banner. All
    // cheap getters — no I/O, no LittleFS access — safe to call every frame.
    uint8_t        highlightedSlot() const { return highlightedSlot_; }
    uint8_t        loadedSlot()      const { return loadedSlot_; }
    const char*    loadedName()      const { return loadedName_; }
    PtchBannerType bannerType()      const { return banner_; }
    uint8_t        bannerSlot()      const { return bannerSlot_; }

    // Name lookup for an arbitrary slot, used by DisplayRenderer to paint
    // each row of the browse list (not just the currently-loaded one).
    // Returns nullptr for an empty/unused slot — same contract as
    // PatchStore::getName(), which this forwards to directly. Exposed here
    // rather than handing DisplayRenderer a raw PatchStore* so PatchManager
    // stays the single point of contact between the display and storage.
    const char* slotName(uint8_t slot) const {
        return store_ ? store_->getName(slot) : nullptr;
    }

    // ── Name editing (Phase 3) ───────────────────────────────────────────────
    // Wire the shared NameEditor in (called once from setup, after begin()).
    void setNameEditor(NameEditor& editor) { editor_ = &editor; }

    // Open the rename editor for the currently-highlighted patch slot. Called
    // when the user taps a browse-list row or pushes the RENAME encoder.
    void openRenameHighlighted();

    // Open the rename editor for the (single) performance name.
    void openRenamePerformance();

    // Finalise an edit: persist the editor's buffer to the right target and
    // show a banner. Called by the .ino when the editor reports COMMIT.
    // Does nothing on a null editor or inactive state.
    void commitNameEdit();

private:
    PatchStore*    store_  = nullptr;
    PageManager*   pages_  = nullptr;
    SendCCCallback sendCC_ = nullptr;
    NameEditor*    editor_ = nullptr;   // shared name editor (Phase 3)

    // Browse state
    uint8_t highlightedSlot_ = 0;   // which slot the SCROLL encoder has selected
    uint8_t loadedSlot_      = 0;   // which slot is currently active (for header)
    char    loadedName_[17]  = {};  // cached name of the loaded slot

    // Save-arm state (two-push confirm)
    bool     saveArmed_       = false;
    uint8_t  saveArmedSlot_   = 0;
    uint32_t saveArmedAtMs_   = 0;

    // Banner state (timed one-shot message)
    PtchBannerType banner_      = PtchBannerType::NONE;
    uint8_t        bannerSlot_  = 0;
    uint32_t       bannerUntil_ = 0;

    // ── Internal handlers, one per encoder action ───────────────────────────
    void handleScroll(int32_t delta);
    void handleLoadPush();
    void handleSavePush();
    void handleImportPush();

    // Relay a freshly-loaded patch to both the local display cache and the
    // Teensy. See PageManager::bulkLoadCC() for why these are two separate
    // steps rather than one round-trip through onReceiveCC().
    void relayLoadedPatch(const uint8_t* ccState);

    void showBanner(PtchBannerType type, uint8_t slot, uint32_t durationMs);
    void clearExpiredBanner();

    // PageManager's ActionCallback is a plain function pointer, so it can't
    // carry a `this`. One PatchManager instance is expected to exist for
    // the program's lifetime (mirrors patchStore/pages being globals in the
    // .ino) — onAction() forwards to it via this pointer.
    static PatchManager* instance_;
};
