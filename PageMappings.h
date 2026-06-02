// =============================================================================
// PageMappings.h — JT-8000 complete CC mapping tables
// =============================================================================
// SOURCE OF TRUTH: JT8000_UI_Controller_Handoff.md + JT8000_ControlMapping.jsx
//
// ALL CC assignments live here as const data. To remap a control, change the
// table entry — no code changes needed anywhere else. PageManager indexes
// into these arrays at runtime.
//
// Organisation:
//   kHomeMapping        — HOME page (no ByteSwitch active)
//   kOscMapping         — Page 1: OSC
//   kMixMapping         — Page 2: MIX
//   kFltMapping         — Page 3: FLT
//   kEnvSubPages[3]     — Page 4: ENV (AMP, FILTER, PITCH sub-pages)
//   kLfoSubPages[2]     — Page 5: LFO (LFO1, LFO2 sub-pages)
//   kFxSubPages[2]      — Page 6: FX  (EFFECTS, REVERB sub-pages)
//   kSeqMapping         — Page 7: SEQ
//   kPerfMapping        — Page 8: PERF
//
// The getPageMapping() function returns the active mapping for any page/sub.
// =============================================================================
#pragma once

#include "PageDefs.h"
#include "ParamDefs.h"

namespace PageMap {

// ─── Shorthand for readability ──────────────────────────────────────────────
// S() builds a ControlSlot in-place. N is the null/inactive slot.
#define S(lbl, ccNum, typ) { lbl, ccNum, CtrlType::typ }
#define N                  SLOT_NONE

// =============================================================================
// HOME — default page when no edit page is active
// =============================================================================
static const PageMapping kHomeMapping = {
    "HOME", "HOME", PageColour::PC_ORANGE,
    // potsA — Primary Performance
    { S("CUTOFF",       CC::FILTER_CUTOFF,     CONT),
      S("RESONANCE",    CC::FILTER_RESONANCE,  CONT),
      S("FILT ENV AMT", CC::FILTER_ENV_AMOUNT, BIPOLAR),
      S("OSC BALANCE",  CC::OSC_MIX_BALANCE,   BIPOLAR),
      S("LFO1 RATE",    CC::LFO1_FREQ,         CONT),
      S("LFO1 DEPTH",   CC::LFO1_DEPTH,        CONT),
      S("REV MIX",      CC::FX_REVERB_MIX,     CONT),
      S("AMP LEVEL",    CC::AMP_MOD_FIXED_LEVEL, CONT) },
    // potsB — FX Tweaking
    { S("MOD MIX",   CC::FX_MOD_MIX,            CONT),
      S("MOD RATE",  CC::FX_MOD_RATE,            CONT),
      S("DLY MIX",   CC::FX_JPFX_DELAY_MIX,     CONT),
      S("DLY TIME",  CC::FX_JPFX_DELAY_TIME,     CONT),
      S("DLY FB",    CC::FX_JPFX_DELAY_FEEDBACK,  CONT),
      S("DRY MIX",   CC::FX_DRY_MIX,             CONT),
      S("JPFX MIX",  CC::FX_JPFX_MIX,            CONT),
      S("REV SIZE",  CC::FX_REVERB_SIZE,          CONT) },
    // encsA — Quick Sound Selection
    { S("OSC1 WAVE",  CC::OSC1_WAVE,       SELECT),
      S("OSC2 WAVE",  CC::OSC2_WAVE,       SELECT),
      S("FILT MODE",  CC::FILTER_MODE,     SELECT),
      S("LFO1 WAVE",  CC::LFO1_WAVEFORM,  SELECT),
      S("LFO1 DEST",  CC::LFO1_DESTINATION, SELECT),
      S("POLY MODE",  CC::POLY_MODE,       SELECT),
      S("GLIDE ON",   CC::GLIDE_ENABLE,    TOGGLE),
      S("PERF MODE",  CC::PERF_MODE,       SELECT) },
    // encsB — Secondary Selections
    { S("OSC1 PITCH",  CC::OSC1_PITCH_OFFSET, SELECT),
      S("OSC2 PITCH",  CC::OSC2_PITCH_OFFSET, SELECT),
      S("FILT ENGINE", CC::FILTER_ENGINE,     SELECT),
      S("LFO2 WAVE",   CC::LFO2_WAVEFORM,    SELECT),
      S("LFO2 DEST",   CC::LFO2_DESTINATION, SELECT),
      S("DRIVE",       CC::FX_DRIVE,         SELECT),
      S("MOD TYPE",    CC::FX_MOD_EFFECT,    SELECT),
      S("DLY TYPE",    CC::FX_JPFX_DELAY_EFFECT, SELECT) }
};

// =============================================================================
// Page 1: OSC — Oscillators (Cyan)
// =============================================================================
static const PageMapping kOscMapping = {
    "OSCILLATORS", "OSC", PageColour::PC_CYAN,
    // potsA — Supersaw
    { S("SS1 DETUNE", CC::SUPERSAW1_DETUNE, CONT),
      S("SS1 MIX",    CC::SUPERSAW1_MIX,   CONT),
      S("SS2 DETUNE", CC::SUPERSAW2_DETUNE, CONT),
      S("SS2 MIX",    CC::SUPERSAW2_MIX,   CONT),
      N, N, N, N },
    // potsB — Feedback & Ring
    { S("OSC1 FB AMT", CC::OSC1_FEEDBACK_AMOUNT, CONT),
      S("OSC1 FB MIX", CC::OSC1_FEEDBACK_MIX,    CONT),
      S("OSC2 FB AMT", CC::OSC2_FEEDBACK_AMOUNT, CONT),
      S("OSC2 FB MIX", CC::OSC2_FEEDBACK_MIX,    CONT),
      S("RING 1 MIX",  CC::RING1_MIX,            CONT),
      S("RING 2 MIX",  CC::RING2_MIX,            CONT),
      N, N },
    // encsA — Waveform & Tuning
    { S("OSC1 WAVE",   CC::OSC1_WAVE,      SELECT),
      S("OSC1 PITCH",  CC::OSC1_PITCH_OFFSET, SELECT),
      S("OSC1 FINE",   CC::OSC1_FINE_TUNE,  BIPOLAR),
      S("OSC1 DETUNE", CC::OSC1_DETUNE,     BIPOLAR),
      S("OSC2 WAVE",   CC::OSC2_WAVE,       SELECT),
      S("OSC2 PITCH",  CC::OSC2_PITCH_OFFSET, SELECT),
      S("OSC2 FINE",   CC::OSC2_FINE_TUNE,  BIPOLAR),
      S("OSC2 DETUNE", CC::OSC2_DETUNE,     BIPOLAR) },
    // encsB — empty
    { N, N, N, N, N, N, N, N }
};

// =============================================================================
// Page 2: MIX — Mixer (Green)
// =============================================================================
static const PageMapping kMixMapping = {
    "MIXER", "MIX", PageColour::PC_GREEN,
    // potsA
    { S("OSC1 MIX",  CC::OSC1_MIX,  CONT),
      S("OSC2 MIX",  CC::OSC2_MIX,  CONT),
      S("SUB MIX",   CC::SUB_MIX,   CONT),
      S("NOISE MIX", CC::NOISE_MIX, CONT),
      N, N, N, N },
    // potsB — empty
    { N, N, N, N, N, N, N, N },
    // encsA
    { S("OSC BALANCE", CC::OSC_MIX_BALANCE,   BIPOLAR),
      S("CROSS MOD",   CC::OSC_CROSS_MOD_DEPTH, CONT),
      S("OSC SYNC",    CC::OSC_SYNC_ENABLE,   TOGGLE),
      N, N, N, N, N },
    // encsB — empty
    { N, N, N, N, N, N, N, N }
};

// =============================================================================
// Page 3: FLT — Filter (Orange)
// =============================================================================
static const PageMapping kFltMapping = {
    "FILTER", "FLT", PageColour::PC_ORANGE,
    // potsA — continuous + bipolar controls on pots for direct access
    { S("CUTOFF",     CC::FILTER_CUTOFF,     CONT),
      S("RESONANCE",  CC::FILTER_RESONANCE,  CONT),
      S("ENV AMT",    CC::FILTER_ENV_AMOUNT,  BIPOLAR),
      S("KEY TRACK",  CC::FILTER_KEY_TRACK,   BIPOLAR),
      N, N, N, N },
    // potsB — empty
    { N, N, N, N, N, N, N, N },
    // encsA — selection-type params stay on encoders
    { S("ENGINE",    CC::FILTER_ENGINE,           SELECT),
      S("MODE",      CC::FILTER_MODE,             SELECT),
      S("VA TYPE",   CC::VA_FILTER_TYPE,          SELECT),
      S("XPANDER",   CC::FILTER_OBXA_XPANDER_MODE, SELECT),
      N, N, N, N },
    // encsB — empty
    { N, N, N, N, N, N, N, N }
};

// =============================================================================
// Page 4: ENV — Envelopes (Red) — 3 sub-pages
// =============================================================================

// Sub 0: AMP envelope
static const PageMapping kEnvAmpMapping = {
    "AMP", "AMP", PageColour::PC_RED,
    // potsA — ADSR (0–3) + curve controls (4–6)
    { S("ATTACK",  CC::AMP_ATTACK,  ENV),
      S("DECAY",   CC::AMP_DECAY,   ENV),
      S("SUSTAIN", CC::AMP_SUSTAIN,  ENV),
      S("RELEASE", CC::AMP_RELEASE,  ENV),
      S("ATK CRV", CC::AMP_ATTACK_CURVE,  CONT),
      S("DEC CRV", CC::AMP_DECAY_CURVE,   CONT),
      S("REL CRV", CC::AMP_RELEASE_CURVE, CONT),
      N },
    // potsB — empty
    { N, N, N, N, N, N, N, N },
    // encsA — sub-page selectors only (curves moved to pots)
    { S("AMP",     0, SUB_SEL),
      S("FILT",    0, SUB_SEL),
      S("PITCH",   0, SUB_SEL),
      N, N, N, N, N },
    // encsB — empty
    { N, N, N, N, N, N, N, N }
};

// Sub 1: FILTER envelope
static const PageMapping kEnvFilterMapping = {
    "FILTER", "FILT", PageColour::PC_GOLD,
    // potsA — ADSR (0–3) + curve controls (4–6)
    { S("ATTACK",  CC::FILTER_ENV_ATTACK,  ENV),
      S("DECAY",   CC::FILTER_ENV_DECAY,   ENV),
      S("SUSTAIN", CC::FILTER_ENV_SUSTAIN,  ENV),
      S("RELEASE", CC::FILTER_ENV_RELEASE,  ENV),
      S("ATK CRV", CC::FILTER_ATTACK_CURVE,  CONT),
      S("DEC CRV", CC::FILTER_DECAY_CURVE,   CONT),
      S("REL CRV", CC::FILTER_RELEASE_CURVE, CONT),
      N },
    // potsB — empty
    { N, N, N, N, N, N, N, N },
    // encsA — sub-page selectors only (curves moved to pots)
    { S("AMP",     0, SUB_SEL),
      S("FILT",    0, SUB_SEL),
      S("PITCH",   0, SUB_SEL),
      N, N, N, N, N },
    { N, N, N, N, N, N, N, N }
};

// Sub 2: PITCH envelope (CCs 65–69 defined in ParamDefs.h)
static const PageMapping kEnvPitchMapping = {
    "PITCH", "PITCH", PageColour::PC_MAGENTA,
    // potsA — ADSR (0–3) + depth (4) + curves (5–7)
    { S("ATTACK",  CC::PITCH_ENV_ATTACK,   ENV),
      S("DECAY",   CC::PITCH_ENV_DECAY,    ENV),
      S("SUSTAIN", CC::PITCH_ENV_SUSTAIN,  ENV),
      S("RELEASE", CC::PITCH_ENV_RELEASE,  ENV),
      S("DEPTH",   CC::PITCH_ENV_DEPTH,       BIPOLAR),
      S("ATK CRV", CC::PITCH_ATTACK_CURVE,   CONT),
      S("DEC CRV", CC::PITCH_DECAY_CURVE,    CONT),
      S("REL CRV", CC::PITCH_RELEASE_CURVE,  CONT) },
    // potsB — empty
    { N, N, N, N, N, N, N, N },
    // encsA — sub-page selectors only (depth + curves moved to pots)
    { S("AMP",     0, SUB_SEL),
      S("FILT",    0, SUB_SEL),
      S("PITCH",   0, SUB_SEL),
      N, N, N, N, N },
    // encsB — empty
    { N, N, N, N, N, N, N, N }
};

static const PageMapping kEnvSubPages[3] = {
    kEnvAmpMapping, kEnvFilterMapping, kEnvPitchMapping
};

// =============================================================================
// Page 5: LFO (Purple) — 2 sub-pages
// =============================================================================

// Sub 0: LFO1
static const PageMapping kLfo1Mapping = {
    "LFO1", "LFO1", PageColour::PC_PURPLE,
    // potsA
    { S("RATE",       CC::LFO1_FREQ,         CONT),
      S("DEPTH",      CC::LFO1_DEPTH,        CONT),
      S("DELAY",      CC::LFO1_DELAY,        CONT),
      S("PITCH DEP",  CC::LFO1_PITCH_DEPTH,  CONT),
      S("FILT DEP",   CC::LFO1_FILTER_DEPTH, CONT),
      S("PWM DEP",    CC::LFO1_PWM_DEPTH,    CONT),
      S("AMP DEP",    CC::LFO1_AMP_DEPTH,    CONT),
      N },
    { N, N, N, N, N, N, N, N },
    // encsA
    { S("LFO1",     0, SUB_SEL),
      S("LFO2",     0, SUB_SEL),
      S("WAVEFORM",  CC::LFO1_WAVEFORM,    SELECT),
      S("DEST",      CC::LFO1_DESTINATION, SELECT),
      S("SYNC",      CC::LFO1_TIMING_MODE, SELECT),
      N, N, N },
    { N, N, N, N, N, N, N, N }
};

// Sub 1: LFO2
static const PageMapping kLfo2Mapping = {
    "LFO2", "LFO2", PageColour::PC_INDIGO,
    { S("RATE",       CC::LFO2_FREQ,         CONT),
      S("DEPTH",      CC::LFO2_DEPTH,        CONT),
      S("DELAY",      CC::LFO2_DELAY,        CONT),
      S("PITCH DEP",  CC::LFO2_PITCH_DEPTH,  CONT),
      S("FILT DEP",   CC::LFO2_FILTER_DEPTH, CONT),
      S("PWM DEP",    CC::LFO2_PWM_DEPTH,    CONT),
      S("AMP DEP",    CC::LFO2_AMP_DEPTH,    CONT),
      N },
    { N, N, N, N, N, N, N, N },
    { S("LFO1",     0, SUB_SEL),
      S("LFO2",     0, SUB_SEL),
      S("WAVEFORM",  CC::LFO2_WAVEFORM,    SELECT),
      S("DEST",      CC::LFO2_DESTINATION, SELECT),
      S("SYNC",      CC::LFO2_TIMING_MODE, SELECT),
      N, N, N },
    { N, N, N, N, N, N, N, N }
};

static const PageMapping kLfoSubPages[2] = {
    kLfo1Mapping, kLfo2Mapping
};

// =============================================================================
// Page 6: FX (Teal) — 2 sub-pages
// =============================================================================

// Sub 0: EFFECTS
static const PageMapping kFxEffectsMapping = {
    "EFFECTS", "FX", PageColour::PC_TEAL,
    // potsA
    { S("MOD MIX",  CC::FX_MOD_MIX,             CONT),
      S("MOD RATE", CC::FX_MOD_RATE,             CONT),
      S("MOD FB",   CC::FX_MOD_FEEDBACK,         CONT),
      S("DLY TIME", CC::FX_JPFX_DELAY_TIME,      CONT),
      S("DLY MIX",  CC::FX_JPFX_DELAY_MIX,       CONT),
      S("DLY FB",   CC::FX_JPFX_DELAY_FEEDBACK,   CONT),
      S("DRY MIX",  CC::FX_DRY_MIX,              CONT),
      S("JPFX MIX", CC::FX_JPFX_MIX,             CONT) },
    { N, N, N, N, N, N, N, N },
    // encsA
    { S("FX",       0, SUB_SEL),
      S("REVERB",   0, SUB_SEL),
      S("MOD TYPE", CC::FX_MOD_EFFECT,           SELECT),
      S("DLY TYPE", CC::FX_JPFX_DELAY_EFFECT,    SELECT),
      S("DRIVE",    CC::FX_DRIVE,                SELECT),
      S("DLY SYNC", CC::DELAY_TIMING_MODE,       SELECT),
      S("BASS",     CC::FX_BASS_GAIN,            BIPOLAR),
      S("TREBLE",   CC::FX_TREBLE_GAIN,          BIPOLAR) },
    { N, N, N, N, N, N, N, N }
};

// Sub 1: REVERB (Global FX scope — shared across layers)
static const PageMapping kFxReverbMapping = {
    "REVERB", "REV", PageColour::PC_SILVER,
    // potsA
    { S("SIZE",     CC::FX_REVERB_SIZE,    CONT),
      S("HI DAMP",  CC::FX_REVERB_DAMP,    CONT),
      S("LO DAMP",  CC::FX_REVERB_LODAMP,  CONT),
      S("MIX",      CC::FX_REVERB_MIX,     CONT),
      S("SHIMMER",  CC::FX_REVERB_SHIMMER, CONT),
      S("LO PASS",  CC::FX_REVERB_LOWPASS, CONT),
      S("HI PASS",  CC::FX_REVERB_HIPASS,  CONT),
      N },
    { N, N, N, N, N, N, N, N },
    // encsA
    { S("FX",      0, SUB_SEL),
      S("REVERB",  0, SUB_SEL),
      S("BYPASS",  CC::FX_REVERB_BYPASS, TOGGLE),
      S("FREEZE",  CC::FX_REVERB_FREEZE, TOGGLE),
      N, N, N, N },
    { N, N, N, N, N, N, N, N }
};

static const PageMapping kFxSubPages[2] = {
    kFxEffectsMapping, kFxReverbMapping
};

// =============================================================================
// Page 7: SEQ — Step Sequencer (Pink)
// =============================================================================
static const PageMapping kSeqMapping = {
    "SEQUENCER", "SEQ", PageColour::PC_PINK,
    // potsA — Steps 1–8 (all use CC::SEQ_STEP_VALUE with step addressing)
    { S("STEP 1", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 2", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 3", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 4", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 5", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 6", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 7", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 8", CC::SEQ_STEP_VALUE, STEP_VAL) },
    // potsB — Steps 9–16
    { S("STEP 9",  CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 10", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 11", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 12", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 13", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 14", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 15", CC::SEQ_STEP_VALUE, STEP_VAL),
      S("STEP 16", CC::SEQ_STEP_VALUE, STEP_VAL) },
    // encsA — Settings
    { S("ENABLE",    CC::SEQ_ENABLE,      TOGGLE),
      S("DIRECTION", CC::SEQ_DIRECTION,   SELECT),
      S("DEST",      CC::SEQ_DESTINATION, SELECT),
      S("SYNC",      CC::SEQ_TIMING_MODE, SELECT),
      S("RETRIGGER", CC::SEQ_RETRIGGER,   TOGGLE),
      S("DEPTH",     CC::SEQ_DEPTH,       BIPOLAR),
      N, N },
    // encsB — Continuous Settings
    { S("STEPS",  CC::SEQ_STEPS,       CONT),
      S("GATE",   CC::SEQ_GATE_LENGTH, CONT),
      S("SLIDE",  CC::SEQ_SLIDE,       CONT),
      S("RATE",   CC::SEQ_RATE,        CONT),
      N, N, N, N }
};

// =============================================================================
// Page 8: PERF — Performance (Amber)
// =============================================================================
static const PageMapping kPerfMapping = {
    "PERFORMANCE", "PERF", PageColour::PC_AMBER,
    // potsA
    { S("GLIDE TIME", CC::GLIDE_TIME,          CONT),
      S("UNI DETUNE", CC::UNISON_DETUNE,       CONT),
      S("BEND RANGE", CC::PITCH_BEND_RANGE,    CONT),
      S("AMP LEVEL",  CC::AMP_MOD_FIXED_LEVEL, CONT),
      S("BPM",        CC::BPM_INTERNAL_TEMPO,  CONT),
      N, N, N },
    { N, N, N, N, N, N, N, N },
    // encsA
    { S("POLY MODE",   CC::POLY_MODE,        SELECT),
      S("GLIDE ON",    CC::GLIDE_ENABLE,     TOGGLE),
      S("PERF MODE",   CC::PERF_MODE,        SELECT),
      S("EDIT TARGET", CC::PERF_EDIT_TARGET, SELECT),
      S("CLK SRC",     CC::BPM_CLOCK_SOURCE, SELECT),
      N, N, N },
    { N, N, N, N, N, N, N, N }
};

// =============================================================================
// Lookup helpers — PageManager uses these to get the active mapping
// =============================================================================

// Returns true if the given page has sub-pages
inline bool hasSubPages(PageID page) {
    return page == PageID::ENV || page == PageID::LFO || page == PageID::FX;
}

// Returns the sub-page count for pages that have them (0 otherwise)
inline uint8_t subPageCount(PageID page) {
    switch (page) {
        case PageID::ENV: return 3;
        case PageID::LFO: return 2;
        case PageID::FX:  return 2;
        default:          return 0;
    }
}

// Returns the mapping for a flat page (no sub-pages)
inline const PageMapping& getFlatMapping(PageID page) {
    switch (page) {
        case PageID::OSC:  return kOscMapping;
        case PageID::MIX:  return kMixMapping;
        case PageID::FLT:  return kFltMapping;
        case PageID::SEQ:  return kSeqMapping;
        case PageID::PERF: return kPerfMapping;
        default:           return kHomeMapping;
    }
}

// Returns the mapping for a sub-page (page must have sub-pages)
inline const PageMapping& getSubMapping(PageID page, uint8_t subIdx) {
    switch (page) {
        case PageID::ENV: return kEnvSubPages[subIdx < 3 ? subIdx : 0];
        case PageID::LFO: return kLfoSubPages[subIdx < 2 ? subIdx : 0];
        case PageID::FX:  return kFxSubPages[subIdx < 2 ? subIdx : 0];
        default:          return kHomeMapping;
    }
}

// Returns the active mapping for any page/sub combination
inline const PageMapping& getMapping(PageID page, uint8_t subIdx = 0) {
    if (page == PageID::HOME) return kHomeMapping;
    if (hasSubPages(page))    return getSubMapping(page, subIdx);
    return getFlatMapping(page);
}

// Clean up macros — not needed outside this header
#undef S
#undef N

} // namespace PageMap
