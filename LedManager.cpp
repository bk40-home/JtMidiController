// =============================================================================
// LedManager.cpp — see LedManager.h.
// =============================================================================
#include "LedManager.h"

#include <string.h>

#include "HwPalette.h"

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

// Identity colours live in HwPalette.h — shared with the on-screen chips so
// the LED and the chip cannot drift apart. All states are expressed as
// BRIGHTNESS of the identity colour, never as a different hue: after a page
// or bank flip every pot is seeking at once, and repainting them all in an
// alert colour would erase identity exactly when the user is re-finding
// their knob.

// ── Per-unit write budget ───────────────────────────────────────────────────
// The M5 units' internal MCUs silently DROP LED writes that arrive in a
// back-to-back burst — a page change used to issue up to 8 per unit, and the
// dropped ones left the unit showing stale colours (including its factory
// rainbow) while our cache believed the write had landed. So: at most this
// many writes per unit per update; a slot over budget is skipped WITHOUT
// latching its cache entry, stays dirty, and retries next loop. The loop runs
// every few ms, so a full 8-LED repaint settles within ~4 updates —
// imperceptible — and consecutive writes to a unit are now spaced by a whole
// loop iteration, which is the recovery time these units need. Steady state
// still costs zero I2C.
constexpr uint8_t kLedWriteBudget = 2;

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
        uint8_t writes = 0;
        for (uint8_t i = 0; i < 8 && writes < kLedWriteBudget; ++i) {
            uint32_t c;
            if (view.buttonsToggleMode()) {
                c = 0;   // dark: no toggle bound -> this button does nothing
                const uint8_t row = view.btnBoundRow(i);
                if (row != JtView::RowList::kNoRow) {
                    const JT::Params::ParamDesc* d = view.rowDesc(row);
                    const bool on =
                        d && JtParam::isOn(*d, view.rowValue(row));
                    // State by BRIGHTNESS, identity by HUE: bright = on,
                    // dim = off, both in this button's slot colour so the
                    // chip on screen still matches either way.
                    c = on ? JtHw::kSlotColour[i]
                           : ColorUtils::scaleBrightness(JtHw::kSlotColour[i], 20);
                }
            } else {
                // Buttons map to editing pages 1..8 (HOME is page 0, no
                // button) — mirror of ViewController::handleButtons.
                c = (static_cast<uint8_t>(i + 1) == page)
                    ? kPageColour[i]
                    : ColorUtils::scaleBrightness(kPageColour[i], 25);
            }
            // Latch the cache ONLY on an issued write — see kLedWriteBudget.
            if (c != prevBtn_[i]) { buttons.setLed(i, c); prevBtn_[i] = c; ++writes; }
        }
    }

    // ── Pots: the bound Control::Pot rows of the ACTIVE bank ────────────────
    // The Angle8 scales the RGB by its OWN brightness parameter inside
    // setLEDColor (Angle8Unit.cpp), so software-scaling the RGB as well
    // stacks two attenuations: 20/256 x 40/255 landed at ~2/255 per channel
    // — indistinguishable from off, which is exactly the bug this replaces.
    // So the split is: RGB carries the IDENTITY at full value (also keeps the
    // hue exact — scaling RGB toward black shifts perceived hue on these
    // LEDs, and hue is what has to match the on-screen chip), and the unit's
    // brightness parameter carries the pickup STATE: dim while seeking,
    // bright once the pot has crossed its stored value.
    if (angle.isPresent()) {
        constexpr uint8_t kPotBrSeek = 8;    // dim-but-visible: not engaged
        constexpr uint8_t kPotBrLive = 40;   // the unit's default drive
        uint8_t writes = 0;
        for (uint8_t i = 0; i < 8 && writes < kLedWriteBudget; ++i) {
            uint32_t c  = 0;   // dark: unbound -> this pot does nothing
            uint8_t  br = 0;
            if (view.potBoundRow(i) != JtView::RowList::kNoRow) {
                c  = JtHw::kSlotColour[i];
                br = view.pickupSeeking(i) ? kPotBrSeek : kPotBrLive;
            }
            // Change-gate on colour AND brightness: the colour only occupies
            // 24 bits, so the brightness rides in the spare top byte. Without
            // it, a seek->live transition (same colour, new brightness) would
            // be swallowed by the cache and the LED would stay dim.
            const uint32_t key = (static_cast<uint32_t>(br) << 24) | c;
            if (key != prevPot_[i]) {
                // Angle8 takes SEPARATE r/g/b bytes, unlike the Encoder8 and
                // ByteButton which take a packed 0xRRGGBB.
                angle.setLed(i,
                             static_cast<uint8_t>((c >> 16) & 0xFF),
                             static_cast<uint8_t>((c >>  8) & 0xFF),
                             static_cast<uint8_t>( c        & 0xFF),
                             br);
                prevPot_[i] = key;
                ++writes;
            }
        }
    }

    // ── Encoders: the bound Control::Encoder rows ───────────────────────────
    // Bound = full slot colour, unbound = dark. Nothing in between: the
    // Encoder8 has no separate brightness input, and software-scaling the RGB
    // both dims into invisibility and shifts the hue away from the on-screen
    // chip (the pot bug's sibling). No state display either — the parameter
    // table has NO Toggle-typed rows on Encoder control (all toggles are
    // Control::Switch -> ByteButtons), so the old per-encoder toggle branch
    // was dead code and is gone.
    if (encoder.isPresent()) {
        uint8_t writes = 0;
        for (uint8_t i = 0; i < 8 && writes < kLedWriteBudget; ++i) {
            const uint32_t c =
                (view.encBoundRow(i) != JtView::RowList::kNoRow)
                    ? JtHw::kSlotColour[i] : 0u;
            // Latch the cache ONLY on an issued write — see kLedWriteBudget.
            if (c != prevEnc_[i]) { encoder.setLed(i, c); prevEnc_[i] = c; ++writes; }
        }
    }
}
