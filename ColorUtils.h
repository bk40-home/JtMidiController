// =============================================================================
// ColorUtils.h — Colour conversion utilities (integer-only, no floats)
// =============================================================================
#pragma once

#include <Arduino.h>

namespace ColorUtils {

// Convert HSV to RGB. Hue: degrees (any int, wrapped to 0..359).
// Sat and Val: 0..255. Output: 0..255 per channel.
void hsvToRgb(int16_t hue, uint8_t sat, uint8_t val,
              uint8_t& r, uint8_t& g, uint8_t& b);

// Pack 8-bit R,G,B into 0xRRGGBB (used by Encoder8 and ByteButton libraries)
inline uint32_t packRgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// Extract R,G,B from packed 0xRRGGBB
inline void unpackRgb(uint32_t rgb, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (rgb >> 16) & 0xFF;
    g = (rgb >> 8)  & 0xFF;
    b =  rgb        & 0xFF;
}

// Bright random colour (random hue, full sat/val). Returns 0xRRGGBB.
uint32_t randomColor();

// Scale a packed RGB colour by a brightness factor (0..255)
uint32_t scaleBrightness(uint32_t rgb, uint8_t brightness);

} // namespace ColorUtils
