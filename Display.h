// =============================================================================
// Display.h — TFT board bring-up (Phase D)
// =============================================================================
// Just the panel: TCA9554 reset, SPI bus, ST7796 init, backlight. It returns a
// bare Arduino_GFX* and does no drawing.
//
// This used to live inside DisplayRenderer, which also owned the entire page
// layout and every widget. Splitting it out is what lets the view panels be pure
// drawing with no board knowledge, and lets the board bring-up be verified
// independently of anything to do with parameters.
//
// The sequence below is NOT arbitrary — it matches the working Waveshare demo
// exactly (this is the NON-B board: ST7796 over SPI, FT6336 over I2C). The
// reset must go through the TCA9554 expander, and the panel must be declared in
// its NATIVE 320x480 portrait with rotation 3 applied to get 480x320 landscape.
// Deviating from any of it produces a blank or scrambled panel.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Bring the panel up. Returns nullptr on failure.
// Safe to call once, from setup().
Arduino_GFX* jtDisplayBegin();
