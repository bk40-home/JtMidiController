// =============================================================================
// PageDefs.h — JT-8000 page navigation types and control slot structures
// =============================================================================
// All type definitions for the table-driven mapping system. The actual mapping
// data lives in PageMappings.h — this file defines the schema only.
//
// Design principle: CC assignments are DATA, not code. Swapping what maps where
// is a config change in PageMappings.h, not a rewrite.
// =============================================================================
#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Control types — determines hardware behaviour and UI treatment
// ─────────────────────────────────────────────────────────────────────────────
enum class CtrlType : uint8_t {
    CONT,       // Pot or encoder: 0–127 continuous sweep
    SELECT,     // Encoder: turn to scroll options, push to cycle
    TOGGLE,     // Encoder push: on/off (CC 0 or 127)
    BIPOLAR,    // Encoder: CC 64 = zero centre, push resets to 64
    ENV,        // Pot: ADSR value, 0–127
    STEP_VAL,   // Pot: step sequencer value, LED brightness = value
    SUB_SEL,    // Encoder push: select sub-page (internal navigation only)
    NONE        // Unmapped slot — control is inactive
};

// ─────────────────────────────────────────────────────────────────────────────
// Page identifiers — match ByteSwitch button positions
// ─────────────────────────────────────────────────────────────────────────────
enum class PageID : uint8_t {
    HOME = 0,   // Default when no edit page active
    OSC  = 1,   // Oscillators
    MIX  = 2,   // Mixer
    FLT  = 3,   // Filter
    ENV  = 4,   // Envelopes (3 sub-pages)
    LFO  = 5,   // LFOs (2 sub-pages)
    FX   = 6,   // Effects (2 sub-pages)
    SEQ  = 7,   // Step Sequencer
    PERF = 8,   // Performance
    COUNT = 9
};

// ─────────────────────────────────────────────────────────────────────────────
// Scene — Angle8 and Encoder8 each have an independent A/B switch
// ─────────────────────────────────────────────────────────────────────────────
enum class Scene : uint8_t {
    A = 0,      // Primary controls (90% of performance)
    B = 1       // Secondary/overflow controls
};

// ─────────────────────────────────────────────────────────────────────────────
// Sub-page identifiers — used by ENV, LFO, and FX pages
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t SUBPAGE_MAX = 3;  // ENV has 3 sub-pages (max)

// ─────────────────────────────────────────────────────────────────────────────
// ControlSlot — one pot or encoder mapping entry
//
// A slot describes what one physical control does on the current page/scene.
// The PageManager reads these to route hardware input to CC output.
// ─────────────────────────────────────────────────────────────────────────────
struct ControlSlot {
    const char* label;   // Display label (e.g. "CUTOFF", "OSC1 WAVE")
    uint8_t     cc;      // CC number to send (0 for NONE/SUB_SEL)
    CtrlType    type;    // How the control behaves

    // True if this slot is mapped to a real parameter
    bool isActive() const { return type != CtrlType::NONE; }

    // True if this slot is a sub-page selector (no CC, internal nav only)
    bool isSubSel() const { return type == CtrlType::SUB_SEL; }

    // True if this is a bipolar control (CC 64 = centre zero)
    bool isBipolar() const { return type == CtrlType::BIPOLAR; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Null slot — used for unmapped positions
// ─────────────────────────────────────────────────────────────────────────────
static constexpr ControlSlot SLOT_NONE = { "\xE2\x80\x94", 0, CtrlType::NONE };

// ─────────────────────────────────────────────────────────────────────────────
// PageMapping — complete control layout for one page or sub-page
//
// Each page has 4 banks of 8 controls:
//   potsA[8]  — Angle8 scene A
//   potsB[8]  — Angle8 scene B
//   encsA[8]  — Encoder8 scene A
//   encsB[8]  — Encoder8 scene B
// ─────────────────────────────────────────────────────────────────────────────
struct PageMapping {
    const char*  name;       // Full page name (e.g. "OSCILLATORS")
    const char*  tab;        // Short tab name (e.g. "OSC")
    uint32_t     ledColour;  // RGB colour for LEDs (0xRRGGBB)
    ControlSlot  potsA[8];
    ControlSlot  potsB[8];
    ControlSlot  encsA[8];
    ControlSlot  encsB[8];
};

// ─────────────────────────────────────────────────────────────────────────────
// SubPageDef — a page with sub-pages (ENV, LFO, FX)
// Contains an array of PageMapping, one per sub-page.
// ─────────────────────────────────────────────────────────────────────────────
struct SubPageDef {
    const char*        name;          // Parent page name
    const char*        tab;           // Parent tab name
    uint8_t            count;         // Number of sub-pages
    const PageMapping* subPages;      // Array of sub-page mappings
};

// ─────────────────────────────────────────────────────────────────────────────
// Page colour constants — match the handoff spec exactly
// Prefixed PC_ to avoid collision with Arduino_GFX.h macros
// (#define RED, #define GREEN, #define CYAN, etc.)
// ─────────────────────────────────────────────────────────────────────────────
namespace PageColour {
    static constexpr uint32_t PC_ORANGE  = 0xFF8C00;   // HOME, FLT
    static constexpr uint32_t PC_WHITE   = 0xFFFFFF;   // HOME voice LEDs
    static constexpr uint32_t PC_CYAN    = 0x00D4FF;   // OSC
    static constexpr uint32_t PC_GREEN   = 0x00FF80;   // MIX
    static constexpr uint32_t PC_RED     = 0xFF4444;   // ENV / AMP sub
    static constexpr uint32_t PC_GOLD    = 0xFFD700;   // ENV / FILTER sub
    static constexpr uint32_t PC_MAGENTA = 0xE040FB;   // ENV / PITCH sub
    static constexpr uint32_t PC_PURPLE  = 0x8B5CF6;   // LFO / LFO1 sub
    static constexpr uint32_t PC_INDIGO  = 0x6366F1;   // LFO / LFO2 sub
    static constexpr uint32_t PC_TEAL    = 0x14B8A6;   // FX / EFFECTS sub
    static constexpr uint32_t PC_SILVER  = 0x94A3B8;   // FX / REVERB sub
    static constexpr uint32_t PC_PINK    = 0xF472B6;   // SEQ
    static constexpr uint32_t PC_AMBER   = 0xF59E0B;   // PERF
} // namespace PageColour
