// =============================================================================
// NavBar.h — header + sub-tab strip, and ALL touch navigation (Phase E)
// =============================================================================
// Phase D had 17 flat sections against 8 ByteButtons and NO touch navigation
// at all, so 9 sections were simply unreachable. This fixes that: every page and
// every sub-tab is reachable by touch alone. The ByteButtons remain as
// shortcuts, but they are no longer the only way in.
//
//   Row 1 (24 px)  page name  |  patch name  |  voice dots
//                  ^ tap the page name to drop down the list of all 8 pages
//   Row 2 (22 px)  sub-tab strip — tap a tab to switch
//
//   Swipe left/right anywhere in the content area pages through.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "NavModel.h"

namespace JtView {

class NavBar {
public:
    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; }

    void draw(uint8_t pageIdx, uint8_t subIdx, const char* patchName,
              uint8_t activeVoices, uint8_t maxVoices,
              uint8_t editLayer, bool dimB);

    // The page drop-down. Open by tapping the page name.
    //
    // The menu paints ONCE per open — drawPageMenu() is self-guarded by
    // menuDirty_. Phase E repainted it every display frame while it was open,
    // a constant SPI burn for a static image and one of the loop-starvation
    // sources behind the pot lag. Its content cannot change while open (the
    // highlight is the page it opened on), so once is exactly enough.
    //
    // closePageMenu() does NOT repaint anything: the OWNER repairs the rect
    // the menu covered (kMenuX/Y/W/H) — see ViewController::repairRect.
    void openPageMenu()  { menuOpen_ = true;  menuDirty_ = true; }
    void closePageMenu() { menuOpen_ = false; }
    bool isMenuOpen() const { return menuOpen_; }
    void drawPageMenu(uint8_t pageIdx);

    // ── Touch ───────────────────────────────────────────────────────────────
    enum class Hit : uint8_t { None, PageMenu, SubTab, Content, LayerChip };

    // Classify a touch. `out` receives the sub-tab index for Hit::SubTab, or
    // the chosen page for a tap inside an open menu (see menuPick).
    Hit hitTest(uint8_t pageIdx, int16_t x, int16_t y, uint8_t& out) const;

    // Which page did a tap inside the open menu choose? 0xFF if it missed
    // (which cancels the menu).
    uint8_t menuPick(int16_t x, int16_t y) const;

    static constexpr int16_t kHeaderH = 24;
    static constexpr int16_t kTabH    = 22;
    static constexpr int16_t kTabsY   = kHeaderH;
    static constexpr int16_t kContentY = kHeaderH + kTabH;

    // The rect the open menu covers, for the owner's close-repair.
    // ── EDIT-layer chip (fault 2) ───────────────────────────────────────────
    // A tappable "A|B" on the PERF page's row 1, right of the patch name and
    // left of the voice dots. It sets which layer parameter EDITS target — a
    // panel-local choice carried in NRPN bit 13, entirely separate from perf
    // mode, voice split and the A/B mix balance. Only PERF shows it (that is
    // where you set up a layered/split patch); every other page leaves row 1
    // as it was. Geometry is fixed so hitTest and draw agree without state.
    static constexpr uint8_t  kLayerChipPage = 8;    // PERF (see NavModel kPages)
    static constexpr int16_t  kChipX = 292;          // left edge of "A"
    static constexpr int16_t  kChipY = 4;
    static constexpr int16_t  kChipW = 64;           // covers "A | B" at size 2
    static constexpr int16_t  kChipH = 18;
    static constexpr int16_t  kChipMidX = kChipX + kChipW / 2;  // A/B split point

    static constexpr int16_t kMenuX = 0;
    static constexpr int16_t kMenuY = kHeaderH;
    static constexpr int16_t kMenuW = 160;
    static constexpr int16_t kMenuRowH = 30;
    static constexpr int16_t kMenuH = JtNav::kPageCount * kMenuRowH;

private:
    Arduino_GFX* gfx_ = nullptr;
    bool dirty_    = true;
    bool menuOpen_ = false;

    bool menuDirty_ = false;

    uint8_t lastPage_ = 0xFF;
    uint8_t lastSub_  = 0xFF;
    uint8_t lastVoices_ = 0xFF;
    uint8_t lastLayer_  = 0xFF;   // fault 2: chip repaints on layer/mode change
    bool    lastDimB_   = false;
};

} // namespace JtView
