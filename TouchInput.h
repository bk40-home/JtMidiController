// =============================================================================
// TouchInput.h — FT6336 capacitive touch driver (Waveshare ESP32-S3-Touch-LCD-3.5)
// =============================================================================
// Thin wrapper around SensorLib's TouchDrvFT6X36 (lewisxhe/SensorLib), the
// same driver the Waveshare LVGL demo uses on this exact board — so the raw
// I2C / chip-revision handling is the vendor-proven path, not hand-rolled.
//
// LIBRARY DEPENDENCY:
//   Install "SensorLib" by Lewis He via Arduino IDE Library Manager (or
//   lib_deps = lewisxhe/SensorLib in PlatformIO). This wrapper pulls in only
//   the FT6X36 touch driver header in the .cpp, not the whole library.
//
// This class adds the project-standard layer ON TOP of getPoint():
//   - begin(TwoWire*)   — probe + init, return presence (same as other units)
//   - poll()            — GATED: only reads the bus every Config::TOUCH_POLL_MS
//                         (or, with a touch INT pin set, only when INT asserts)
//   - latched tap event — tapped() / clearTap(), the rising-edge-latch pattern
//                         used by ByteButtonUnit::pressed() / clearPress()
//   - two-point access  — point 0 drives nav/edit; point 1 is exposed for
//                         future pinch/gesture work
//   - ONE rotation transform — native portrait → landscape, centralised
//
// WHY GATED POLLING:
//   The FT6336 shares the 50 kHz I2C bus with three M5 units. Config.h already
//   documents this bus as the system bottleneck. Reading at ~33 Hz is far
//   faster than any human tap yet leaves the bus free for pot/encoder polling.
//   When the gate hasn't elapsed, poll() returns immediately — no bus traffic,
//   nothing calculated.
//
// COORDINATE FRAME:
//   The FT6336 reports raw coordinates in the panel's NATIVE PORTRAIT frame
//   (320×480). The display runs at rotation=3 (landscape, 480×320).
//   transformPoint() maps native→landscape in ONE place so the rest of the
//   firmware works purely in landscape pixels matching DisplayRenderer's
//   480×320 layout. Change the display rotation → change ONLY that function.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

// Forward-declare the SensorLib driver so including TouchInput.h elsewhere
// (PageManager, DisplayRenderer, .ino) does NOT force those translation units
// to pull in the SensorLib header. The concrete include lives in TouchInput.cpp.
class TouchDrvFT6X36;

class TouchInput {
public:
    // FT6336 hardware tracks at most two simultaneous contacts.
    static constexpr uint8_t kMaxPoints = 2;

    TouchInput();
    ~TouchInput();

    // Probe + initialise the controller (and, if configured, the INT pin).
    // The bus must already be Wire.begin()-initialised by the caller — same
    // contract as the M5 unit drivers (the .ino does this before any begin()).
    // Returns true if the controller was found.
    bool begin(TwoWire* wire);

    // Call every loop. GATED internally — see header comment. Updates the
    // current point set and the latched tap event only when it actually reads.
    void poll();

    // ── Current contact state (landscape frame, valid after a poll() read) ──
    uint8_t pointCount() const { return pointCount_; }
    bool    touched()    const { return pointCount_ > 0; }

    // Coordinates of contact `idx` (0 = primary), in the 480×320 landscape
    // frame. Returns 0 for an inactive point. Bounds-checked.
    uint16_t x(uint8_t idx = 0) const;
    uint16_t y(uint8_t idx = 0) const;

    // ── Latched tap event (primary contact) ────────────────────────────────
    // Latches once on the touch-down edge of the primary contact, exactly like
    // ByteButtonUnit::pressed(). Caller acts on it, then calls clearTap().
    bool tapped() const { return tapLatched_; }
    void clearTap()     { tapLatched_ = false; }

    // X/Y of the most recent latched tap (landscape frame). Stable until the
    // next tap latches, so it stays valid after the finger lifts.
    uint16_t tapX() const { return tapX_; }
    uint16_t tapY() const { return tapY_; }

    // ── Presence ────────────────────────────────────────────────────────────
    bool isPresent() const { return present_; }

private:
    TouchDrvFT6X36* drv_      = nullptr;   // owned; allocated in begin()
    TwoWire*        wire_     = nullptr;
    bool            present_  = false;

    // Per-point current state, already transformed to landscape pixels.
    uint16_t px_[kMaxPoints] = {};
    uint16_t py_[kMaxPoints] = {};
    uint8_t  pointCount_     = 0;

    // Press-edge tracking for the primary contact.
    bool     prevTouched_ = false;
    bool     tapLatched_  = false;
    uint16_t tapX_        = 0;
    uint16_t tapY_        = 0;

    // Poll gate timestamp (millis of the last actual bus read).
    uint32_t lastPollMs_  = 0;

    // Map one raw native-portrait coordinate pair to the landscape frame.
    // Centralised so every coordinate goes through the same rotation maths.
    static void transformPoint(uint16_t nx, uint16_t ny,
                               uint16_t& outX, uint16_t& outY);
};
