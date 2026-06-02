// =============================================================================
// ColorUtils.cpp
// =============================================================================
#include "ColorUtils.h"

void ColorUtils::hsvToRgb(int16_t hue, uint8_t sat, uint8_t val,
                           uint8_t& r, uint8_t& g, uint8_t& b) {
    // Wrap hue to 0..359
    hue = hue % 360;
    if (hue < 0) hue += 360;

    // Fast path: zero saturation = greyscale
    if (sat == 0) { r = g = b = val; return; }

    // Integer HSV→RGB using 8-bit fixed-point arithmetic
    const uint8_t  region = hue / 60;
    const uint16_t remainder = (hue - region * 60) * 256 / 60;

    const uint8_t p = (uint16_t(val) * (255 - sat)) >> 8;
    const uint8_t q = (uint16_t(val) * (255 - ((sat * remainder) >> 8))) >> 8;
    const uint8_t t = (uint16_t(val) * (255 - ((sat * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  r = val; g = t;   b = p;   break;
        case 1:  r = q;   g = val; b = p;   break;
        case 2:  r = p;   g = val; b = t;   break;
        case 3:  r = p;   g = q;   b = val; break;
        case 4:  r = t;   g = p;   b = val; break;
        default: r = val; g = p;   b = q;   break;
    }
}

uint32_t ColorUtils::randomColor() {
    uint8_t r, g, b;
    hsvToRgb(random(360), 255, 255, r, g, b);
    return packRgb(r, g, b);
}

uint32_t ColorUtils::scaleBrightness(uint32_t rgb, uint8_t brightness) {
    uint8_t r, g, b;
    unpackRgb(rgb, r, g, b);
    r = (uint16_t(r) * brightness) >> 8;
    g = (uint16_t(g) * brightness) >> 8;
    b = (uint16_t(b) * brightness) >> 8;
    return packRgb(r, g, b);
}
