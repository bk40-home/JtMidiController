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
    { "OSC",   PageKind::List,      { {"OSC 1", 0}, {"OSC 2", 1}, {"MIX", 2} },        3, false },
    { "FILT",  PageKind::Filter,    { {"FILTER", 3} },                                  1, false },
    // ENV declares 3 real sub-tabs; the 4th ("ALL") is the overlay and has no
    // section of its own — it draws all three curves at once. hasOverlayTab
    // rather than a magic 4th SubTab, so nothing can index it as a section.
    { "ENV",   PageKind::Envelope,  { {"AMP", 4}, {"FILTER", 5}, {"PITCH", 6} },        3, true  },
    { "LFO",   PageKind::List,      { {"LFO 1", 7}, {"LFO 2", 8} },                     2, false },
    { "FX",    PageKind::List,      { {"EFFECTS", 9}, {"REVERB", 15} },                 2, false },
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

void collectRows(uint8_t section, const JtParam::Store& store, RowSet& out) {
    out.count = 0;
    for (size_t i = 0; i < kParamCount && out.count < kMaxRows; ++i) {
        const ParamDesc& d = kParams[i];
        if (d.section != section) continue;
        if (!isVisible(d, store)) continue;
        out.ordinal[out.count++] = static_cast<uint16_t>(i);
    }
}

} // namespace JtNav
