// =============================================================================
// PatchOverlay.cpp — see PatchOverlay.h for the design rationale.
// =============================================================================
#include "PatchOverlay.h"
#include <Arduino_GFX_Library.h>
#include <stdio.h>

// Same palette as SelectPopup, deliberately: the two modals should read as
// the same species of thing.
namespace {
    constexpr uint16_t COL_PANEL  = 0x0000;   // black panel
    constexpr uint16_t COL_TITLE  = 0x10A2;   // dark strip behind the title
    constexpr uint16_t COL_ACCENT = 0xFD20;   // JT orange
    constexpr uint16_t COL_TEXT   = 0xFFFF;
    constexpr uint16_t COL_DIM    = 0x8410;   // grey for EMPTY slots
}

void PatchOverlay::open() {
    if (!pm_) return;
    active_     = true;
    fullRedraw_ = true;
    // Land the window on the highlight PatchManager already holds — it
    // remembers the last browsed slot across opens, which is what you want
    // when saving a few variations into neighbouring slots.
    followHighlight();
}

void PatchOverlay::followHighlight() {
    const uint8_t hi = pm_->highlightedSlot();
    if (hi < top_) {
        top_ = hi;
    } else if (hi >= static_cast<uint8_t>(top_ + kVisRows)) {
        top_ = static_cast<uint8_t>(hi - kVisRows + 1);
    }
}

// ── Input ────────────────────────────────────────────────────────────────────

PatchOverlay::Action PatchOverlay::handleTouch(int16_t x, int16_t y) {
    if (!active_ || !pm_) return Action::NONE;

    // Outside the panel: close. Nothing was armed-and-committed by this tap,
    // and a pending SAVE arm simply times out inside PatchManager.
    if (x < kPanelX || x >= kPanelX + kPanelW ||
        y < kPanelY || y >= kPanelY + kPanelH) {
        close();
        return Action::CLOSED;
    }

    // Action row: SAVE on the left half, RENAME on the right.
    if (y >= actionY() && y < listY()) {
        if (x < kPanelX + kPanelW / 2) {
            // First tap arms ("SAVE SLOT N? TAP AGAIN" banner), second tap
            // within the arm window commits — the standing confirmation rule,
            // enforced by PatchManager, not re-implemented here.
            PatchManager::onAction(PtchEncoder::SAVE, /*isPush=*/true, 0);
        } else {
            // The NameEditor is full-screen; close first so its exit repair
            // returns to the normal view, not to a stale panel underneath.
            close();
            PatchManager::onAction(PtchEncoder::RENAME, /*isPush=*/true, 0);
            return Action::CLOSED;
        }
        return Action::NONE;
    }

    // Slot list: move the highlight to the tapped slot, then LOAD it. Both go
    // through the public router — SCROLL is relative, so the delta is
    // computed against the current highlight.
    if (y >= listY()) {
        const uint8_t row  = static_cast<uint8_t>((y - listY()) / kRowH);
        if (row >= kVisRows) return Action::NONE;
        const uint8_t slot = static_cast<uint8_t>(top_ + row);
        const int32_t d    = static_cast<int32_t>(slot)
                           - static_cast<int32_t>(pm_->highlightedSlot());
        if (d != 0) PatchManager::onAction(PtchEncoder::SCROLL, false, d);
        PatchManager::onAction(PtchEncoder::LOAD, /*isPush=*/true, 0);
        return Action::NONE;   // stay open: the banner reports the result
    }

    return Action::NONE;   // title / banner strip: inert
}

PatchOverlay::Action PatchOverlay::handleEncoder(int32_t delta, bool push) {
    if (!active_ || !pm_) return Action::NONE;

    if (delta != 0) {
        PatchManager::onAction(PtchEncoder::SCROLL, false, delta);
        followHighlight();
    }
    if (push) {
        PatchManager::onAction(PtchEncoder::LOAD, true, 0);
    }
    return Action::NONE;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void PatchOverlay::draw(Arduino_GFX* gfx) {
    if (!active_ || !gfx || !pm_) return;

    followHighlight();

    const uint8_t        hi     = pm_->highlightedSlot();
    const uint8_t        loaded = pm_->loadedSlot();
    const PtchBannerType ban    = pm_->bannerType();
    const uint8_t        bslot  = pm_->bannerSlot();

    const bool rowsChanged   = fullRedraw_ || hi != prevHi_ || top_ != prevTop_
                            || loaded != prevLoaded_
                            // SAVED/RENAMED are the only events that change a
                            // slot NAME while the overlay is open — repaint
                            // the rows when one lands so the new name shows.
                            || (ban != prevBanner_
                                && (ban == PtchBannerType::SAVED
                                 || ban == PtchBannerType::RENAMED));
    const bool bannerChanged = fullRedraw_ || ban != prevBanner_
                            || bslot != prevBSlot_;

    if (fullRedraw_) {
        // Panel + border ONLY — floating panel, same rule as SelectPopup:
        // never paint outside the rect the close-repair will cover.
        gfx->fillRect(kPanelX, kPanelY, kPanelW, kPanelH, COL_PANEL);
        gfx->drawRect(kPanelX, kPanelY, kPanelW, kPanelH, COL_ACCENT);

        gfx->fillRect(kPanelX, kPanelY, kPanelW, kTitleH, COL_TITLE);
        gfx->setTextColor(COL_ACCENT);
        gfx->setTextSize(2);
        gfx->setCursor(kPanelX + 8, kPanelY + 7);
        gfx->print("PATCHES");
    }

    if (bannerChanged) drawBanner(gfx);
    if (fullRedraw_ || rowsChanged || bannerChanged) drawActions(gfx);
    if (rowsChanged) drawRows(gfx, fullRedraw_ || top_ != prevTop_);

    prevHi_     = hi;
    prevTop_    = top_;
    prevLoaded_ = loaded;
    prevBanner_ = ban;
    prevBSlot_  = bslot;
    fullRedraw_ = false;
}

void PatchOverlay::drawBanner(Arduino_GFX* gfx) {
    gfx->fillRect(kPanelX + 1, bannerY(), kPanelW - 2, kBannerH, COL_PANEL);

    char text[32];
    uint16_t col = COL_DIM;
    const uint8_t bslot = pm_->bannerSlot();

    switch (pm_->bannerType()) {
        case PtchBannerType::LOADED:
            snprintf(text, sizeof(text), "LOADED: %s", pm_->loadedName());
            col = COL_ACCENT; break;
        case PtchBannerType::SAVED:
            snprintf(text, sizeof(text), "SAVED - SLOT %u", bslot + 1);
            col = COL_ACCENT; break;
        case PtchBannerType::SAVE_ARMED:
            snprintf(text, sizeof(text), "SAVE SLOT %u? TAP AGAIN", bslot + 1);
            col = COL_TEXT; break;
        case PtchBannerType::SAVE_CANCELLED:
            snprintf(text, sizeof(text), "SAVE CANCELLED"); break;
        case PtchBannerType::SAVE_FAILED:
            snprintf(text, sizeof(text), "SAVE FAILED"); col = COL_TEXT; break;
        case PtchBannerType::EMPTY_SLOT:
            snprintf(text, sizeof(text), "SLOT EMPTY"); break;
        case PtchBannerType::LOAD_FAILED:
            snprintf(text, sizeof(text), "LOAD FAILED"); col = COL_TEXT; break;
        case PtchBannerType::RENAMED:
            snprintf(text, sizeof(text), "RENAMED"); col = COL_ACCENT; break;
        case PtchBannerType::COMING_SOON:
            snprintf(text, sizeof(text), "SYX IMPORT - COMING SOON"); break;
        case PtchBannerType::NONE:
        default:
            // Idle banner doubles as the "what is loaded" header.
            snprintf(text, sizeof(text), "CURRENT: %s", pm_->loadedName());
            break;
    }

    gfx->setTextSize(1);
    gfx->setTextColor(col);
    gfx->setCursor(kPanelX + 8, bannerY() + 7);
    gfx->print(text);
}

void PatchOverlay::drawActions(Arduino_GFX* gfx) {
    const int16_t half = kPanelW / 2;
    char save[16];
    snprintf(save, sizeof(save), "SAVE > %u", pm_->highlightedSlot() + 1);

    // SAVE cell inverts while armed so the pending destructive action is
    // impossible to miss — the banner says it, the button shows it.
    const bool armed = (pm_->bannerType() == PtchBannerType::SAVE_ARMED);

    gfx->fillRect(kPanelX + 1, actionY(), half - 1, kActionH,
                  armed ? COL_ACCENT : COL_TITLE);
    gfx->setTextSize(2);
    gfx->setTextColor(armed ? COL_PANEL : COL_TEXT);
    gfx->setCursor(kPanelX + 10, actionY() + 8);
    gfx->print(save);

    gfx->fillRect(kPanelX + half, actionY(), half - 1, kActionH, COL_TITLE);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(kPanelX + half + 10, actionY() + 8);
    gfx->print("RENAME");
}

void PatchOverlay::drawRows(Arduino_GFX* gfx, bool all) {
    const uint8_t hi     = pm_->highlightedSlot();
    const uint8_t loaded = pm_->loadedSlot();

    for (uint8_t r = 0; r < kVisRows; ++r) {
        const uint8_t slot = static_cast<uint8_t>(top_ + r);

        // Repaint the row if the window moved, or if its highlight state
        // could have changed (it is, or was, the highlighted row).
        const bool isHi  = (slot == hi);
        const bool wasHi = (slot == prevHi_);
        if (!all && !isHi && !wasHi) continue;

        const int16_t ry = listY() + r * kRowH;
        gfx->fillRect(kPanelX + 1, ry, kPanelW - 2, kRowH,
                      isHi ? COL_ACCENT : COL_PANEL);

        const char* name  = pm_->slotName(slot);
        const bool  empty = (name == nullptr || name[0] == '\0');

        char line[28];
        snprintf(line, sizeof(line), "%3u %c %s",
                 slot + 1,
                 (slot == loaded) ? '*' : ' ',      // * marks the live patch
                 empty ? "- EMPTY -" : name);

        gfx->setTextSize(2);
        gfx->setTextColor(isHi ? COL_PANEL
                               : (empty ? COL_DIM : COL_TEXT));
        gfx->setCursor(kPanelX + 8, ry + 6);
        gfx->print(line);
    }
}
