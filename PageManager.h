// =============================================================================
// PageManager.h — Page navigation, scene switching, CC dispatch
// =============================================================================
// The PageManager is the central coordinator between hardware input and MIDI
// output. It owns the current page/scene/sub-page state and the local CC
// cache. On each poll cycle it:
//
//   1. Reads ByteButton for page navigation
//   2. Reads Angle8/Encoder8 scene switches
//   3. Routes pot/encoder input through the active page mapping
//   4. Applies pickup mode for pots
//   5. Handles encoder push actions (sub-page select, toggle, reset)
//   6. Sends changed CCs to the MIDI output
//   7. Updates LED state
//
// The CC cache (ccState_[]) mirrors the Teensy's parameter state. It is
// populated from incoming MIDI (Teensy → ESP32) and from local edits.
// Pickup mode uses the cache to know where each pot should "pick up" to.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"
#include "ParamDefs.h"
#include "PageDefs.h"
#include "PageMappings.h"
#include "PickupMode.h"
#include "Angle8Unit.h"
#include "Encoder8Unit.h"
#include "ByteButtonUnit.h"

// Callback for sending a CC out (to MIDI router)
using SendCCCallback = void(*)(uint8_t cc, uint8_t value);

class PageManager {
public:
    PageManager() = default;

    void begin();

    // ── Per-loop processing ─────────────────────────────────────────────────
    // Call once per loop. Reads all hardware, dispatches CCs, updates state.
    void update(Angle8Unit& angle, Encoder8Unit& encoder,
                ByteButtonUnit& buttons);

    // ── MIDI output callback ────────────────────────────────────────────────
    void setSendCC(SendCCCallback cb) { sendCC_ = cb; }

    // ── Incoming CC from Teensy ─────────────────────────────────────────────
    // Updates local CC cache and pickup targets. Called by UartMidi callback.
    void onReceiveCC(uint8_t cc, uint8_t value);

    // ── State queries ───────────────────────────────────────────────────────
    PageID       currentPage()    const { return page_; }
    uint8_t      currentSubPage() const { return subPage_; }
    Scene        potScene()       const { return potScene_; }
    Scene        encScene()       const { return encScene_; }
    const PageMapping& activeMapping() const;

    // Get the local CC cache value
    uint8_t getCCValue(uint8_t cc) const;

    // Get a step sequencer value (0–15). Returns cached value for display.
    uint8_t getStepValue(uint8_t step) const;

    // ── Pickup state access (for display) ───────────────────────────────────
    const PickupMode& pickup() const { return pickup_; }

private:
    PageID       page_     = PageID::HOME;
    uint8_t      subPage_  = 0;
    Scene        potScene_ = Scene::A;
    Scene        encScene_ = Scene::A;

    // Previous scene switch states (for edge detection)
    bool prevPotSwitch_ = false;
    bool prevEncSwitch_ = false;

    // Local CC cache — mirrors Teensy parameter state
    uint8_t ccState_[Config::CC_STATE_SIZE] = {};

    // Step sequencer value cache — 16 steps, populated by pot edits
    // and incoming Teensy CCs (SEQ_STEP_SELECT + SEQ_STEP_VALUE pairs)
    uint8_t stepValues_[16] = {};
    uint8_t lastStepSelect_  = 0;  // tracks the most recent step select CC

    // Pickup mode for pots
    PickupMode pickup_;

    // CC output callback
    SendCCCallback sendCC_ = nullptr;

    // ── Internal handlers ───────────────────────────────────────────────────
    void handlePageButtons(ByteButtonUnit& buttons);
    void handleSceneSwitches(bool potSwitch, bool encSwitch);
    void handlePots(Angle8Unit& angle);
    void handleEncoders(Encoder8Unit& encoder);
    void handleSubPageSelect(uint8_t encIdx);

    // Apply an encoder delta to a CC value based on control type
    void applyEncoderDelta(const ControlSlot& slot, int32_t delta);

    // Send a CC and update local cache
    void emitCC(uint8_t cc, uint8_t value);

    // Refresh pickup targets from current mapping and CC cache
    void refreshPickupTargets();

    // Clamp value to 0..127
    static uint8_t clamp7(int value);

    // Step sequencer: sends step select + value pair
    void handleStepPot(uint8_t potIdx, uint8_t value);
};
