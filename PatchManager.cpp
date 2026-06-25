// =============================================================================
// PatchManager.cpp
// =============================================================================
#include "PatchManager.h"

PatchManager* PatchManager::instance_ = nullptr;

void PatchManager::begin(PatchStore& store, PageManager& pages,
                          SendCCCallback sendCC) {
    store_  = &store;
    pages_  = &pages;
    sendCC_ = sendCC;
    instance_ = this;

    // If slot 0 already has a saved patch, reflect its name immediately so
    // the header isn't blank before the user ever opens PTCH. Purely
    // cosmetic — does not load or send anything.
    if (store_->exists(0)) {
        const char* n = store_->getName(0);
        strncpy(loadedName_, n ? n : "UNNAMED", 16);
        loadedName_[16] = '\0';
    }
}

// ── Per-loop update ──────────────────────────────────────────────────────────

void PatchManager::update() {
    clearExpiredBanner();
}

// ── Static trampoline — forwards to the live instance ──────────────────────

void PatchManager::onAction(uint8_t encIdx, bool isPush, int32_t delta) {
    if (!instance_) return;  // PatchManager::begin() not called yet — ignore

    if (!isPush) {
        // Only SCROLL responds to rotation. LOAD/SAVE/IMPORT are push-only;
        // rotating them is a no-op by design (see PageMappings.h kPtchMapping).
        if (encIdx == PtchEncoder::SCROLL) instance_->handleScroll(delta);
        return;
    }

    switch (encIdx) {
        case PtchEncoder::LOAD:   instance_->handleLoadPush();          break;
        case PtchEncoder::SAVE:   instance_->handleSavePush();          break;
        case PtchEncoder::IMPORT: instance_->handleImportPush();        break;
        case PtchEncoder::RENAME: instance_->openRenameHighlighted();   break;
        // SCROLL ignores push — there's nothing to "activate" by pressing
        // the scroll encoder; LOAD is the explicit action for that.
        default: break;
    }
}

// ── SCROLL ───────────────────────────────────────────────────────────────────

void PatchManager::handleScroll(int32_t delta) {
    // Any scroll input cancels a pending save-arm — moving away from the
    // armed slot before confirming should not silently carry the arm to
    // a different slot the user scrolls onto.
    if (saveArmed_) {
        saveArmed_ = false;
        showBanner(PtchBannerType::SAVE_CANCELLED, saveArmedSlot_,
                   Config::PTCH_BANNER_MS);
    }

    // Encoder8 delta is signed step count; one detent = one slot. Clamp to
    // the valid range rather than wrapping — wrapping from slot 127 back to
    // 0 (or vice versa) on a single extra detent is disorienting on a list
    // this long, unlike the wrap-around behaviour used for SELECT-type
    // parameter encoders elsewhere (those wrap because option lists are
    // short and cyclic; this is a flat index into 128 slots).
    int32_t next = static_cast<int32_t>(highlightedSlot_) + delta;
    if (next < 0) next = 0;
    if (next > Config::MAX_PATCHES - 1) next = Config::MAX_PATCHES - 1;
    highlightedSlot_ = static_cast<uint8_t>(next);
}

// ── LOAD ─────────────────────────────────────────────────────────────────────

void PatchManager::handleLoadPush() {
    if (!store_->exists(highlightedSlot_)) {
        showBanner(PtchBannerType::EMPTY_SLOT, highlightedSlot_,
                  Config::PTCH_BANNER_MS);
        return;
    }

    // PatchStore::load() fills a full CC_STATE_SIZE buffer indexed by raw
    // CC number — same shape PageManager::bulkLoadCC() expects. See the
    // comment on bulkLoadCC() in PageManager.h for why this isn't a packed
    // kPatchableCCs-sized array.
    static uint8_t buf[Config::CC_STATE_SIZE];  // static: keep off the stack,
                                                  // this function only runs
                                                  // from the main loop, never
                                                  // re-entrantly or from an ISR
    if (!store_->load(highlightedSlot_, buf)) {
        // Load failed despite exists() saying yes — treat as empty rather
        // than silently leaving the previous patch in place.
        showBanner(PtchBannerType::EMPTY_SLOT, highlightedSlot_,
                  Config::PTCH_BANNER_MS);
        return;
    }

    relayLoadedPatch(buf);

    loadedSlot_ = highlightedSlot_;
    const char* n = store_->getName(loadedSlot_);
    strncpy(loadedName_, n ? n : "UNNAMED", 16);
    loadedName_[16] = '\0';

    showBanner(PtchBannerType::LOADED, loadedSlot_, Config::PTCH_BANNER_MS);
}

void PatchManager::relayLoadedPatch(const uint8_t* ccState) {
    // 1. Update the local display cache directly — instant, no UART
    //    round-trip, so the PTCH banner and any other visible page reflect
    //    the new patch immediately. (This was "Option B" from the agreed
    //    plan — see PageManager::bulkLoadCC() doc comment.)
    pages_->bulkLoadCC(ccState, Config::CC_STATE_SIZE);

    // 2. Relay the same values to the Teensy over MIDI so the actual synth
    //    engine matches what the display now shows. Only the patchable
    //    subset is sent — kPatchableCCs is the definitive list of CCs the
    //    Teensy's Patch.cpp actually persists/restores (see ParamDefs.h).
    if (!sendCC_) return;
    for (int i = 0; i < PatchSchema::kPatchableCount; ++i) {
        const uint8_t cc = PatchSchema::kPatchableCCs[i];
        if (cc < Config::CC_STATE_SIZE) {
            sendCC_(cc, ccState[cc]);
        }
    }
}

// ── SAVE (two-push confirm) ──────────────────────────────────────────────────

void PatchManager::handleSavePush() {
    const uint32_t now = millis();

    if (saveArmed_ && saveArmedSlot_ == highlightedSlot_
                    && (now - saveArmedAtMs_) <= Config::PTCH_SAVE_ARM_MS) {
        // Second push within the arm window on the SAME slot — commit.
        saveArmed_ = false;

        const uint8_t* raw = pages_->ccStateRaw();
        // PatchStore::save() writes the full ccState_ buffer verbatim — its
        // own format already matches PageManager's CC-indexed layout, so no
        // repacking is needed here (unlike load's relay step, which has to
        // pick out the patchable subset before sending MIDI).
        const bool ok = store_->save(highlightedSlot_, raw, loadedName_[0]
                                     ? loadedName_ : "UNNAMED");

        if (ok) {
            loadedSlot_ = highlightedSlot_;
            showBanner(PtchBannerType::SAVED, loadedSlot_,
                      Config::PTCH_BANNER_MS);
        } else {
            // Genuine storage failure (LittleFS not mounted, file open/write
            // error) — PatchStore has already logged the specific reason to
            // Serial. Distinct from SAVE_CANCELLED (user backing out) so the
            // on-screen banner doesn't look identical for two very different
            // situations.
            showBanner(PtchBannerType::SAVE_FAILED, highlightedSlot_,
                      Config::PTCH_BANNER_MS);
        }
        return;
    }

    // First push (or arm expired, or user moved to a different slot since
    // arming) — (re)arm on the currently highlighted slot.
    saveArmed_     = true;
    saveArmedSlot_ = highlightedSlot_;
    saveArmedAtMs_ = now;
    showBanner(PtchBannerType::SAVE_ARMED, highlightedSlot_,
              Config::PTCH_SAVE_ARM_MS);
}

// ── IMPORT (stubbed) ─────────────────────────────────────────────────────────

void PatchManager::handleImportPush() {
    // Deliberately not implemented this pass — see the file header comment
    // and the project's agreed scope: SYX import is a separate task with
    // its own diagnosis/sign-off, not bundled silently into Patch Manager.
    showBanner(PtchBannerType::COMING_SOON, 0, Config::PTCH_BANNER_MS);
}

// ── Banner timing ────────────────────────────────────────────────────────────

void PatchManager::showBanner(PtchBannerType type, uint8_t slot,
                              uint32_t durationMs) {
    banner_      = type;
    bannerSlot_  = slot;
    bannerUntil_ = millis() + durationMs;
}

void PatchManager::clearExpiredBanner() {
    if (banner_ == PtchBannerType::NONE) return;
    if (static_cast<int32_t>(millis() - bannerUntil_) >= 0) {
        banner_ = PtchBannerType::NONE;
    }

    // Save-arm has its own independent timeout, checked separately from the
    // banner's lifetime — the SAVE_ARMED banner happens to share the same
    // duration (Config::PTCH_SAVE_ARM_MS) as the arm window itself, but the
    // two are conceptually different (banner is cosmetic, arm is state).
    if (saveArmed_ && (millis() - saveArmedAtMs_) > Config::PTCH_SAVE_ARM_MS) {
        saveArmed_ = false;
    }
}

// ── Name editing (Phase 3) ──────────────────────────────────────────────────

void PatchManager::openRenameHighlighted() {
    if (!editor_ || !store_) return;
    // Seed with the slot's current name (may be nullptr → blank editor).
    const char* cur = store_->getName(highlightedSlot_);
    editor_->open(NameEditor::Target::PATCH, highlightedSlot_, cur);
}

void PatchManager::openRenamePerformance() {
    if (!editor_ || !store_) return;
    editor_->open(NameEditor::Target::PERFORMANCE, 0, store_->getPerfName());
}

void PatchManager::commitNameEdit() {
    if (!editor_ || !store_ || !editor_->isActive()) return;

    const char* text = editor_->buffer();
    if (editor_->target() == NameEditor::Target::PERFORMANCE) {
        store_->setPerfName(text);
        showBanner(PtchBannerType::RENAMED, 0, Config::PTCH_BANNER_MS);
    } else {
        const uint8_t slot = editor_->slot();
        store_->setName(slot, text);
        // Keep the loaded-name cache in step if we renamed the active slot.
        if (slot == loadedSlot_) {
            strncpy(loadedName_, text, 16);
            loadedName_[16] = '\0';
        }
        showBanner(PtchBannerType::RENAMED, slot, Config::PTCH_BANNER_MS);
    }
    editor_->close();
}
