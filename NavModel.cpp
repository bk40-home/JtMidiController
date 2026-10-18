// =============================================================================
// NavModel.cpp — see NavModel.h.
// =============================================================================
#include "NavModel.h"

namespace JtNav {

using JT::Params::ParamDesc;
using JT::Params::kParams;
using JT::Params::kParamCount;
using JT::Params::kNoVisDep;

// ─────────────────────────────────────────────────────────────────────────────
// FX page split (section 9 "Effects" across two sub-tabs).
//
// Group indices from ParamTable.h kGroupNames[9]:
//   [0] EQ & Drive   [1] Modulation   [2] Delay   [3] Tape Echo   [4] Output
//
// The indices are checked at COMPILE time against the generated names, so a
// params.yaml edit that renames or reorders these groups breaks the build here
// instead of quietly moving rows to the wrong tab.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

constexpr uint8_t kFxSection = 9;

constexpr uint8_t kFxGrpEqDrive = 0;
constexpr uint8_t kFxGrpMod     = 1;
constexpr uint8_t kFxGrpDelay   = 2;
constexpr uint8_t kFxGrpTape    = 3;
constexpr uint8_t kFxGrpOutput  = 4;

// Compile-time string compare (no libc, usable in static_assert).
constexpr bool strEq(const char* a, const char* b) {
    return (a && b) ? ((*a == *b) && (*a == '\0' || strEq(a + 1, b + 1)))
                    : (a == b);
}

static_assert(strEq(JT::Params::kGroupNames[kFxSection][kFxGrpEqDrive], "EQ & Drive"),
              "params.yaml moved the Effects groups: update the FX split");
static_assert(strEq(JT::Params::kGroupNames[kFxSection][kFxGrpMod],     "Modulation"),
              "params.yaml moved the Effects groups: update the FX split");
static_assert(strEq(JT::Params::kGroupNames[kFxSection][kFxGrpDelay],   "Delay"),
              "params.yaml moved the Effects groups: update the FX split");
static_assert(strEq(JT::Params::kGroupNames[kFxSection][kFxGrpTape],    "Tape Echo"),
              "params.yaml moved the Effects groups: update the FX split");
static_assert(strEq(JT::Params::kGroupNames[kFxSection][kFxGrpOutput],  "Output"),
              "params.yaml moved the Effects groups: update the FX split");

// EFFECTS = tone shaping + modulation + the dry/JP-FX output balance.
// DELAY   = the delay itself + the tape-echo character controls.
constexpr uint8_t kFxMainGroups  = groupBit(kFxGrpEqDrive) | groupBit(kFxGrpMod)
                                 | groupBit(kFxGrpOutput);
constexpr uint8_t kFxDelayGroups = groupBit(kFxGrpDelay) | groupBit(kFxGrpTape);

// Every Effects parameter must land on exactly ONE tab: no overlap, and the two
// masks together cover every group the section can have (a 6th group added in
// params.yaml would otherwise vanish from the controller).
static_assert((kFxMainGroups & kFxDelayGroups) == 0, "FX tabs overlap");
static_assert((kFxMainGroups | kFxDelayGroups)
                  == static_cast<uint8_t>((1u << JT::Params::kMaxGroups) - 1u),
              "an Effects group is on no FX tab");

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// The page table.
//
// Section indices are NOT guessed — they are the real values from
// ParamTable.h's kSectionNames, and a unit test asserts that all 140 parameters
// land on exactly one page with no orphans. An earlier hand-written version of
// this table put Global Reverb under PERF and Velocity under FX, which nothing
// would have caught by inspection.
//
//   [ 0] Oscillator 1     [ 6] Pitch Envelope    [12] BPM Clock
//   [ 1] Oscillator 2     [ 7] LFO 1             [13] Step Sequencer
//   [ 2] Mixer            [ 8] LFO 2             [14] Voice Mode
//   [ 3] Filter           [ 9] Effects           [15] Global Reverb
//   [ 4] Amp Envelope     [10] Velocity          [16] Master
//   [ 5] Filter Envelope  [11] Performance
// ─────────────────────────────────────────────────────────────────────────────
static const Page kPages[kPageCount] = {
    // HOME is page 0 — the boot page, the performance dashboard. Section 0xFF
    // is the "no rows" sentinel (same mechanism as the ENV overlay tab): the
    // HomePanel owns the whole content area. The ByteButtons deliberately map
    // to pages 1..8 (see handleButtons), so the eight EDITING pages keep the
    // exact button positions muscle memory already knows; HOME is reached by
    // two-finger swipe or the page menu.
    { "HOME",  PageKind::Home,      { {"HOME", 0xFF} },                                 1, false },
    { "OSC",   PageKind::List,      { {"OSC 1", 0}, {"OSC 2", 1}, {"MIX", 2} },        3, false },
    { "FILT",  PageKind::Filter,    { {"FILTER", 3} },                                  1, false },
    // ENV declares 3 real sub-tabs; the 4th ("ALL") is the overlay and has no
    // section of its own — it draws all three curves at once. hasOverlayTab
    // rather than a magic 4th SubTab, so nothing can index it as a section.
    { "ENV",   PageKind::Envelope,  { {"AMP", 4}, {"FILTER", 5}, {"PITCH", 6} },        3, true  },
    { "LFO",   PageKind::List,      { {"LFO 1", 7}, {"LFO 2", 8} },                     2, false },
    // FX: section 9 is split by group mask (see above); REVERB is section 15.
    { "FX",    PageKind::List,      { {"EFFECTS", kFxSection, kFxMainGroups},
                                      {"DELAY",   kFxSection, kFxDelayGroups},
                                      {"REVERB",  15} },                                3, false },
    { "SEQ",   PageKind::Sequencer, { {"STEPS", 13} },                                  1, false },
    { "VOICE", PageKind::List,      { {"VOICE", 14}, {"VELOCITY", 10} },                2, false },
    { "PERF",  PageKind::List,      { {"PERFORM", 11}, {"CLOCK", 12}, {"MASTER", 16} }, 3, false },
};

const Page& page(uint8_t idx) {
    return kPages[(idx < kPageCount) ? idx : 0];
}

// ─────────────────────────────────────────────────────────────────────────────
// Visibility
//
// Compares OPTION INDICES, not strings — the generator emitted the indices, so
// this costs one integer compare per candidate and no strcmp in the render loop.
// ─────────────────────────────────────────────────────────────────────────────

bool isVisible(const ParamDesc& d, const JtParam::Store& store) {
    // kNoVisDep, NOT zero. ParamID 0x0000 is osc1.wave — a real parameter that
    // four other rows genuinely depend on. Using 0 as the "no condition"
    // sentinel silently disabled every one of those rules while the filter's
    // rules kept working, which is a very quiet way to be wrong.
    if (d.visIf == kNoVisDep) return true;

    const ParamDesc* dep = JtParam::descOf(d.visIf);
    if (!dep || !d.visOpts) return true;   // malformed rule: fail OPEN, not shut

    const uint8_t cur = JtParam::normToIndex(*dep, store.getById(d.visIf));
    for (uint8_t i = 0; i < d.visCount; ++i) {
        if (d.visOpts[i] == cur) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rows
// ─────────────────────────────────────────────────────────────────────────────

void collectRows(uint8_t section, const JtParam::Store& store, RowSet& out,
                 RowSkipFn skip, uint8_t groupMask) {
    out.count = 0;
    for (size_t i = 0; i < kParamCount && out.count < kMaxRows; ++i) {
        const ParamDesc& d = kParams[i];
        if (d.section != section) continue;
        // One AND per candidate. kAllGroups has every bit set, so tabs without
        // a mask take this branch exactly as they did before. Runs only when
        // the row set is rebuilt (tab change / visible_when dependency), never
        // per frame.
        if (!(groupMask & groupBit(d.group))) continue;
        if (skip && skip(d.id)) continue;      // before the cap — see the header
        if (!isVisible(d, store)) continue;
        out.ordinal[out.count++] = static_cast<uint16_t>(i);
    }
}

} // namespace JtNav
