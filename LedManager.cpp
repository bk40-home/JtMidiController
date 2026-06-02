// =============================================================================
// LedManager.cpp
// =============================================================================
#include "LedManager.h"

// Page colours for ByteSwitch LEDs (indexed by PageID - 1, since HOME = 0)
static const uint32_t kPageLedColours[] = {
    PageColour::PC_CYAN,    // OSC (1)
    PageColour::PC_GREEN,   // MIX (2)
    PageColour::PC_ORANGE,  // FLT (3)
    PageColour::PC_RED,     // ENV (4)
    PageColour::PC_PURPLE,  // LFO (5)
    PageColour::PC_TEAL,    // FX  (6)
    PageColour::PC_PINK,    // SEQ (7)
    PageColour::PC_AMBER,   // PERF(8)
};

void LedManager::begin(Angle8Unit& angle, Encoder8Unit& encoder,
                       ByteButtonUnit& buttons) {
    angle.allLedsOff();
    encoder.allLedsOff();
    buttons.allLedsOff();

    // Force a full repaint on first update()
    memset(prevBtnLed_, 0xFF, sizeof(prevBtnLed_));
    memset(prevEncLed_, 0xFF, sizeof(prevEncLed_));
    memset(prevPotLed_, 0xFF, sizeof(prevPotLed_));
}

void LedManager::update(const PageManager& pages,
                        Angle8Unit& angle, Encoder8Unit& encoder,
                        ByteButtonUnit& buttons) {
    updateByteButtons(pages, buttons);
    updateEncoderLeds(pages, encoder);
    updatePotLeds(pages, angle);

    prevPage_    = pages.currentPage();
    prevSubPage_ = pages.currentSubPage();
}

// ── ByteButton LEDs ─────────────────────────────────────────────────────────

void LedManager::updateByteButtons(const PageManager& pages,
                                   ByteButtonUnit& buttons) {
    if (!buttons.isPresent()) return;

    const PageID activePage = pages.currentPage();

    for (uint8_t i = 0; i < 8; ++i) {
        uint32_t colour;
        // Button i maps to page (i+1). Active page is bright, others dim.
        const PageID btnPage = static_cast<PageID>(i + 1);
        if (btnPage == activePage) {
            colour = kPageLedColours[i];
        } else {
            // Dim: scale to ~10% brightness
            colour = ColorUtils::scaleBrightness(kPageLedColours[i], 25);
        }

        if (colour != prevBtnLed_[i]) {
            buttons.setLed(i, colour);
            prevBtnLed_[i] = colour;
        }
    }
}

// ── Encoder LEDs ────────────────────────────────────────────────────────────

void LedManager::updateEncoderLeds(const PageManager& pages,
                                   Encoder8Unit& encoder) {
    if (!encoder.isPresent()) return;

    const auto& map = pages.activeMapping();
    const uint32_t pageColour = map.ledColour;
    const ControlSlot* encs = (pages.encScene() == Scene::A)
                              ? map.encsA : map.encsB;

    for (uint8_t i = 0; i < 8; ++i) {
        uint32_t colour;

        if (!encs[i].isActive() && !encs[i].isSubSel()) {
            colour = 0;  // unmapped → off
        } else if (encs[i].isSubSel()) {
            // Sub-page selector: bright if this is the active sub-page
            if (i == pages.currentSubPage()) {
                colour = pageColour;
            } else {
                colour = ColorUtils::scaleBrightness(pageColour, 40);
            }
        } else if (encs[i].type == CtrlType::TOGGLE) {
            // Toggle: bright = on, dim = off
            const uint8_t val = pages.getCCValue(encs[i].cc);
            colour = (val > 63) ? pageColour
                                : ColorUtils::scaleBrightness(pageColour, 25);
        } else {
            // Standard encoder: page colour
            colour = pageColour;
        }

        if (colour != prevEncLed_[i]) {
            encoder.setLed(i, colour);
            prevEncLed_[i] = colour;
        }
    }
}

// ── Pot LEDs ────────────────────────────────────────────────────────────────

void LedManager::updatePotLeds(const PageManager& pages,
                               Angle8Unit& angle) {
    if (!angle.isPresent()) return;

    const auto& map = pages.activeMapping();
    const uint32_t pageColour = map.ledColour;
    const ControlSlot* pots = (pages.potScene() == Scene::A)
                              ? map.potsA : map.potsB;

    uint8_t r, g, b;
    ColorUtils::unpackRgb(pageColour, r, g, b);

    for (uint8_t i = 0; i < 8; ++i) {
        uint32_t colour;

        if (!pots[i].isActive()) {
            colour = 0;  // unmapped → off
        } else if (pages.pickup().isSeeking(i)) {
            // Seeking: show page colour at brightness matching TARGET value.
            // Static — no pulse/flash. Pot output is suppressed until the
            // physical position crosses the target (pickup mode).
            const uint8_t tgt = pages.pickup().targetValue(i);
            const uint8_t bri = 20 + (uint16_t(tgt) * 80 / 127);
            angle.setLed(i, r, g, b, bri);
            prevPotLed_[i] = pageColour | ((uint32_t)bri << 24);
            continue;
        } else {
            // Active: brightness proportional to CC value
            const uint8_t val = pages.getCCValue(pots[i].cc);
            // Map 0..127 → 20..100 brightness (never fully off when active)
            const uint8_t bri = 20 + (uint16_t(val) * 80 / 127);
            angle.setLed(i, r, g, b, bri);
            prevPotLed_[i] = pageColour | ((uint32_t)bri << 24);
            continue;  // setLed called directly with brightness param
        }

        // Pack for change comparison (simplified — just use colour)
        if (colour != prevPotLed_[i]) {
            uint8_t cr, cg, cb;
            ColorUtils::unpackRgb(colour, cr, cg, cb);
            angle.setLed(i, cr, cg, cb);
            prevPotLed_[i] = colour;
        }
    }
}
