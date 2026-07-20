// =============================================================================
// HomePanel.h — the HOME performance dashboard (Phase F5, brief item 7)
// =============================================================================
// Page 0. No parameter rows — the panel owns the whole content area and shows
// the things a player glances at between edits:
//
//   patch name + slot     what is loaded (big — readable from standing height)
//   LINK dot              controller<->engine UART alive (any rx < 200 ms ago)
//   VOLUME bar            master.volume, live; TAP or DRAG the bar to set it
//   VOICES                8 dots from the engine's status feed — lit = sounding
//   SEQ                   16 ticks; the playhead tick fills while running
//
// The voice dots and playhead come from the 0x3FFF status feed
// (ParamBroadcast on the firmware side, applyStatus() here) — real engine
// state, not an inference from what the controller sent. Scope is DEFERRED
// to its own pass per sign-off (waveform frames need a different transport).
//
// Same dirty discipline as every widget since Phase F1: draw() compares each
// element against what it last painted and repaints only what moved. An idle
// HOME page issues zero draw calls; a playing sequencer repaints exactly two
// ticks per step.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <string.h>
#include "Config.h"

class Arduino_GFX;

namespace JtView {

// The engine status feed's reserved NRPN address — mirror of
// ParamBroadcast::kStatusAddr on the firmware side. Never a ParamID.
constexpr uint16_t kStatusAddr = 0x3FFF;

class HomePanel {

public:
    // Everything the panel shows, gathered by ViewController per frame.
    // Passing a struct (rather than many setters) keeps one call site and
    // lets draw() do all change detection in one place.
    struct State {
        const char* name    = "";     // loaded patch name
        uint8_t     slot    = 0xFF;   // loaded slot, 0xFF = none/init
        float       volume  = 0.0f;   // master.volume norm 0..1
        uint8_t     mask    = 0;      // voice-activity bits from the engine
        uint8_t     step    = 0;      // sequencer playhead 0..15
        bool        running = false;  // sequencer enabled
        bool        link    = false;  // rx traffic seen recently
    };

    void begin(Arduino_GFX* gfx) { gfx_ = gfx; invalidate(); }
    void invalidate() { dirty_ = true; }

    void draw(const State& s);

    // ── Volume-bar geometry, shared with the touch path ─────────────────────
    static constexpr int16_t kVolX = 40;
    static constexpr int16_t kVolY = 152;
    static constexpr int16_t kVolW = 400;
    static constexpr int16_t kVolH = 24;

    static bool volumeHit(int16_t x, int16_t y) {
        // A fat hit band (±10 px vertically) — a volume bar you have to hit
        // pixel-perfect while performing is a volume bar you cannot use.
        return x >= kVolX && x < kVolX + kVolW
            && y >= kVolY - 10 && y < kVolY + kVolH + 10;
    }
    static float volumeFromX(int16_t x) {
        float v = static_cast<float>(x - kVolX) / static_cast<float>(kVolW);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return v;
    }

private:
    void drawName(const State& s);
    void drawLink(bool lit);
    void drawVolume(float v);
    void drawVoices(uint8_t mask);
    void drawSeq(uint8_t step, bool running);

    Arduino_GFX* gfx_ = nullptr;
    bool dirty_ = true;

    // Last-painted state — the change detectors.
    char    lastName_[20] = {0};
    uint8_t lastSlot_     = 0xFE;   // != any real value, forces first paint
    float   lastVol_      = -1.0f;
    uint8_t lastMask_     = 0xFF;
    uint8_t lastStep_     = 0xFF;
    bool    lastRun_      = false;
    bool    lastLink_     = false;
};

} // namespace JtView
