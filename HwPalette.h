// =============================================================================
// HwPalette.h — the 8 hardware-slot identity colours (screen chip <-> unit LED)
// =============================================================================
// WHY THIS FILE EXISTS
//   Physical control i and the on-screen row it drives must show the SAME
//   colour, or the whole scheme is worse than useless. The screen wants
//   RGB565; the M5 unit LEDs want 24-bit RGB. One table, two constexpr views,
//   zero chance of drift. Header-only: everything folds to compile-time
//   constants, there is nothing to link.
//
// PALETTE CHOICES
//   - No pure red. Red is reserved for pickup-seek (LedManager kSeek) — the
//     one state that must never be mistaken for an identity.
//   - Hues picked for separation on BOTH renderers: the ST7796 panel and the
//     tiny diffused RGB LEDs on the Angle8/Encoder8/ByteButton units, which
//     crush saturation and wash out close hues (teal vs cyan is unreadable on
//     them; amber vs yellow only just isn't, so amber was dropped).
//   - Deliberately NOT LedManager's kPageColour ramp. The ByteButtons show
//     page colours in page mode and slot colours in toggle mode; sharing one
//     ramp would make the two modes indistinguishable per button.
// =============================================================================
#pragma once

#include <stdint.h>

namespace JtHw {

// Identity colour of physical control index 0..7. Pots, encoders and buttons
// share the ramp on purpose: a row is bound to exactly ONE control type, and
// the units are physically separate boxes, so "slot 3" is never ambiguous.
constexpr uint32_t kSlotColour[8] = {
    0x00D4FF,   // 0 cyan
    0xFFD400,   // 1 yellow
    0xFF00A8,   // 2 magenta
    0x00E060,   // 3 green
    0x4066FF,   // 4 blue
    0xFF8C00,   // 5 orange
    0xFFFFFF,   // 6 white
    0x9440FF,   // 7 violet
};

// 24-bit 0xRRGGBB -> RGB565, folded at compile time (constexpr, constant
// operands). Top 5/6/5 bits of each channel land in their 565 fields.
constexpr uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800u) |
                                 ((rgb >> 5) & 0x07E0u) |
                                 ((rgb >> 3) & 0x001Fu));
}

// The same ramp as the TFT wants it. Indexed identically to kSlotColour —
// derived, not hand-typed, so an edit above propagates here automatically.
constexpr uint16_t kSlotColour565[8] = {
    rgb565(kSlotColour[0]), rgb565(kSlotColour[1]),
    rgb565(kSlotColour[2]), rgb565(kSlotColour[3]),
    rgb565(kSlotColour[4]), rgb565(kSlotColour[5]),
    rgb565(kSlotColour[6]), rgb565(kSlotColour[7]),
};

// ── Row -> hardware tag: one packed byte per visible row ────────────────────
// ViewController builds an array of these in rebindControls() (the reverse of
// its slot->row binding maps) and hands RowList a plain pointer:
//
//   bits 7-6  control type (kHwPot / kHwEnc / kHwBtn)
//   bit  5    HOLLOW: bound, but not currently reachable — a pot on the
//             inactive Angle8 bank, or a toggle while the ByteButtons are in
//             page mode. Drawn as an outline: "this control, once you flip
//             its switch". Flip it and the chip fills.
//   bits 2-0  physical control index 0..7 = palette index
//   0xFF      no hardware control drives this row (touch/drag only): no chip.
//
// RowList draws the type as the chip's SHAPE — square = pot, circle =
// encoder, triangle = button — so a cyan pot row and a cyan encoder row on
// the same tab stay distinguishable without spending more colours.
constexpr uint8_t kHwNone   = 0xFF;
constexpr uint8_t kHwPot    = 0x00;
constexpr uint8_t kHwEnc    = 0x40;
constexpr uint8_t kHwBtn    = 0x80;
constexpr uint8_t kHwHollow = 0x20;

constexpr uint8_t hwIndex(uint8_t tag)  { return static_cast<uint8_t>(tag & 0x07u); }
constexpr uint8_t hwType(uint8_t tag)   { return static_cast<uint8_t>(tag & 0xC0u); }
constexpr bool    hwHollow(uint8_t tag) { return (tag & kHwHollow) != 0; }

} // namespace JtHw
