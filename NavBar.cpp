// =============================================================================
// NavBar.cpp — see NavBar.h.
// =============================================================================
#include "NavBar.h"
#include "RowList.h"

namespace JtView {
namespace {

constexpr uint16_t C_BG     = 0x0000;
constexpr uint16_t C_ACCENT = 0xFC00;   // orange
constexpr uint16_t C_DIM    = 0x4A49;
constexpr uint16_t C_TEXT   = 0xFFFF;
constexpr uint16_t C_RULE   = 0x2124;
constexpr uint16_t C_TABBG  = 0x1082;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Header + tabs
// ─────────────────────────────────────────────────────────────────────────────

void NavBar::draw(uint8_t pageIdx, uint8_t subIdx, const char* patchName,
                  uint8_t activeVoices, uint8_t maxVoices) {
    if (!gfx_) return;

    const bool changed = dirty_
                      || pageIdx != lastPage_
                      || subIdx  != lastSub_
                      || activeVoices != lastVoices_;
    if (!changed) return;

    const JtNav::Page& pg = JtNav::page(pageIdx);

    // ── Row 1: page name (tappable), patch, voice dots ──────────────────────
    gfx_->fillRect(0, 0, kScreenW, kHeaderH, C_BG);

    gfx_->setTextSize(2);
    gfx_->setTextColor(C_ACCENT, C_BG);
    gfx_->setCursor(8, 5);
    gfx_->print(pg.name);
    // The chevron is the affordance: it tells the user the page name is a
    // control, not a label. Without it nobody discovers the drop-down.
    gfx_->setTextSize(1);
    gfx_->setCursor(static_cast<int16_t>(8 + 12 * 6), 11);
    gfx_->print("v");

    if (patchName) {
        gfx_->setTextSize(1);
        gfx_->setTextColor(C_DIM, C_BG);
        gfx_->setCursor(150, 9);
        gfx_->print(patchName);
    }

    for (uint8_t i = 0; i < maxVoices && i < 16; ++i) {
        const int16_t cx = static_cast<int16_t>(kScreenW - 12 - (maxVoices - 1 - i) * 11);
        if (i < activeVoices) gfx_->fillCircle(cx, 12, 3, C_ACCENT);
        else                  gfx_->drawCircle(cx, 12, 3, C_DIM);
    }

    gfx_->drawFastHLine(0, static_cast<int16_t>(kHeaderH - 1), kScreenW, C_RULE);

    // ── Row 2: sub-tabs ─────────────────────────────────────────────────────
    // ENV's overlay ("ALL") is a 4th tab with no section of its own.
    const uint8_t nTabs = static_cast<uint8_t>(pg.subCount + (pg.hasOverlayTab ? 1 : 0));

    gfx_->fillRect(0, kTabsY, kScreenW, kTabH, C_TABBG);

    if (nTabs > 1) {
        const int16_t tw = static_cast<int16_t>(kScreenW / nTabs);
        for (uint8_t i = 0; i < nTabs; ++i) {
            const bool sel = (i == subIdx);
            const int16_t tx = static_cast<int16_t>(i * tw);

            if (sel) gfx_->fillRect(tx, kTabsY, tw, kTabH, C_ACCENT);

            const char* nm = (i < pg.subCount) ? pg.subs[i].name : "ALL";
            uint8_t n = 0; while (nm[n]) ++n;

            gfx_->setTextSize(1);
            gfx_->setTextColor(sel ? C_BG : C_DIM, sel ? C_ACCENT : C_TABBG);
            gfx_->setCursor(static_cast<int16_t>(tx + (tw - n * 6) / 2),
                            static_cast<int16_t>(kTabsY + 8));
            gfx_->print(nm);
        }
    }

    gfx_->drawFastHLine(0, static_cast<int16_t>(kTabsY + kTabH - 1), kScreenW, C_RULE);

    lastPage_   = pageIdx;
    lastSub_    = subIdx;
    lastVoices_ = activeVoices;
    dirty_      = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// The page drop-down — the touch route to all 8 pages.
// ─────────────────────────────────────────────────────────────────────────────

void NavBar::drawPageMenu(uint8_t pageIdx) {
    if (!gfx_ || !menuOpen_) return;
    // Painted once per open — see the header note. Re-entering here every
    // frame was pure SPI waste for a static menu.
    if (!menuDirty_) return;
    menuDirty_ = false;

    const int16_t h = static_cast<int16_t>(JtNav::kPageCount * kMenuRowH);
    gfx_->fillRect(0, kHeaderH, kMenuW, h, C_BG);
    gfx_->drawRect(0, kHeaderH, kMenuW, h, C_ACCENT);

    for (uint8_t i = 0; i < JtNav::kPageCount; ++i) {
        const bool sel = (i == pageIdx);
        const int16_t y = static_cast<int16_t>(kHeaderH + i * kMenuRowH);

        if (sel) gfx_->fillRect(1, y, static_cast<int16_t>(kMenuW - 2),
                                kMenuRowH, C_ACCENT);

        gfx_->setTextSize(2);
        gfx_->setTextColor(sel ? C_BG : C_TEXT, sel ? C_ACCENT : C_BG);
        gfx_->setCursor(12, static_cast<int16_t>(y + 8));
        gfx_->print(JtNav::page(i).name);
    }
}

uint8_t NavBar::menuPick(int16_t x, int16_t y) const {
    if (!menuOpen_) return 0xFF;
    if (x < 0 || x >= kMenuW || y < kHeaderH) return 0xFF;

    const int16_t r = static_cast<int16_t>((y - kHeaderH) / kMenuRowH);
    return (r >= 0 && r < static_cast<int16_t>(JtNav::kPageCount))
         ? static_cast<uint8_t>(r) : 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Touch classification
// ─────────────────────────────────────────────────────────────────────────────

NavBar::Hit NavBar::hitTest(uint8_t pageIdx, int16_t x, int16_t y,
                            uint8_t& out) const {
    out = 0xFF;

    // Row 1: only the page NAME is a control, not the whole header — otherwise
    // a tap near the voice dots would open the menu unexpectedly.
    if (y < kHeaderH) {
        if (x < 90) return Hit::PageMenu;
        return Hit::None;
    }

    // Row 2: sub-tabs.
    if (y < kTabsY + kTabH) {
        const JtNav::Page& pg = JtNav::page(pageIdx);
        const uint8_t nTabs =
            static_cast<uint8_t>(pg.subCount + (pg.hasOverlayTab ? 1 : 0));
        if (nTabs <= 1) return Hit::None;

        const int16_t tw = static_cast<int16_t>(kScreenW / nTabs);
        const int16_t i  = static_cast<int16_t>(x / tw);
        if (i >= 0 && i < static_cast<int16_t>(nTabs)) {
            out = static_cast<uint8_t>(i);
            return Hit::SubTab;
        }
        return Hit::None;
    }

    return Hit::Content;
}

} // namespace JtView
