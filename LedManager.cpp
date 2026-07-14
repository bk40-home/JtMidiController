// =============================================================================
// LedManager.cpp — see LedManager.h.
// =============================================================================
#include "LedManager.h"

#include <string.h>

namespace {

// One colour per page. 8 pages, 8 buttons — they line up exactly, which is why
// the page count was chosen to be 8.
constexpr uint32_t kPageColour[8] = {
    0x00D4FF,   // 0 OSC    cyan
    0xFF8C00,   // 1 FILT   orange
    0xFF4444,   // 2 ENV    red
    0x8B5CF6,   // 3 LFO    purple
    0x14B8A6,   // 4 FX     teal
    0xF472B6,   // 5 SEQ    pink
    0x00FF80,   // 6 VOICE  green
    0xF59E0B,   // 7 PERF   amber
};

constexpr uint32_t kAccent = 0xFF8C00;   // unified orange
constexpr uint32_t kSeek   = 0xFF2000;   // red — pot has not picked up

} // anonymous namespace

void LedManager::begin(Angle8Unit& angle, Encoder8Unit& encoder,
                       ByteButtonUnit& buttons) {
    angle.allLedsOff();
    encoder.allLedsOff();
    buttons.allLedsOff();

    // 0xFF forces every LED to be written on the first update().
    memset(prevBtn_, 0xFF, sizeof prevBtn_);
    memset(prevEnc_, 0xFF, sizeof prevEnc_);
    memset(prevPot_, 0xFF, sizeof prevPot_);
}

void LedManager::update(const ViewController& view,
                        Angle8Unit& angle, Encoder8Unit& encoder,
                        ByteButtonUnit& buttons) {
    const uint8_t page = view.page();

    // ── ByteButtons: page palette, or toggle states in toggle mode ──────────
    // The Encoder8 scene switch flips the buttons' role (see ViewController::
    // handleButtons); the LEDs are what makes the mode legible on the
    // hardware, so they MUST follow it: page colours in page mode, orange
    // on/off states in toggle mode, dark where no toggle is bound.
    if (buttons.isPresent()) {
        for (uint8_t i = 0; i < 8; ++i) {
            uint32_t c;
            if (view.buttonsToggleMode()) {
                c = 0;   // dark: no toggle bound -> this button does nothing
                const uint8_t row = view.btnBoundRow(i);
                if (row != JtView::RowList::kNoRow) {
                    const JT::Params::ParamDesc* d = view.rowDesc(row);
                    const bool on =
                        d && JtParam::isOn(*d, view.rowValue(row));
                    c = on ? kAccent : ColorUtils::scaleBrightness(kAccent, 20);
                }
            } else {
                c = (i == page)
                    ? kPageColour[i]
                    : ColorUtils::scaleBrightness(kPageColour[i], 25);
            }
            if (c != prevBtn_[i]) { buttons.setLed(i, c); prevBtn_[i] = c; }
        }
    }

    // ── Pots: the bound Control::Pot rows of the ACTIVE bank ────────────────
    if (angle.isPresent()) {
        for (uint8_t i = 0; i < 8; ++i) {
            uint32_t c = 0;   // dark: unbound -> this pot does nothing
            if (view.potBoundRow(i) != JtView::RowList::kNoRow) {
                // Red while the pot has not crossed its stored value. This is
                // the one piece of state the user cannot otherwise see, and not
                // showing it makes pickup feel like a broken knob.
                c = view.pickupSeeking(i)
                  ? kSeek
                  : ColorUtils::scaleBrightness(kAccent, 70);
            }
            if (c != prevPot_[i]) {
                // Angle8 takes SEPARATE r/g/b bytes, unlike the Encoder8 and
                // ByteButton which take a packed 0xRRGGBB.
                angle.setLed(i,
                             static_cast<uint8_t>((c >> 16) & 0xFF),
                             static_cast<uint8_t>((c >>  8) & 0xFF),
                             static_cast<uint8_t>( c        & 0xFF));
                prevPot_[i] = c;
            }
        }
    }

    // ── Encoders: the bound Control::Encoder rows ───────────────────────────
    if (encoder.isPresent()) {
        for (uint8_t i = 0; i < 8; ++i) {
            uint32_t c = 0;
            const uint8_t row = view.encBoundRow(i);

            if (row != JtView::RowList::kNoRow) {
                const JT::Params::ParamDesc* d = view.rowDesc(row);
                if (d && d->type == JT::Params::Type::Toggle) {
                    // A toggle shows its STATE — the most useful thing an LED
                    // can do: you can see sync/glide/freeze is on without
                    // looking at the screen.
                    const bool on = JtParam::isOn(*d, view.rowValue(row));
                    c = on ? kAccent : ColorUtils::scaleBrightness(kAccent, 20);
                } else {
                    c = ColorUtils::scaleBrightness(kAccent, 70);
                }
            }
            if (c != prevEnc_[i]) { encoder.setLed(i, c); prevEnc_[i] = c; }
        }
    }
}
