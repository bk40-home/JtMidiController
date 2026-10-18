// =============================================================================
// NavModel.h — 8 pages, sub-tabs, and conditional row visibility (Phase E)
// =============================================================================
// WHAT PHASE D GOT WRONG
//   17 flat sections against 8 ByteButtons, and no touch navigation at all —
//   so 9 of the 17 were simply unreachable. It also flattened every parameter
//   into one uniform grid of knob arcs, which threw away the envelope curve and
//   the sequencer grid, and made every value hard to read.
//
// THE STRUCTURE
//   8 pages (= 8 ByteButtons, but the buttons are now SHORTCUTS, not the only
//   way in — every page and sub-tab is reachable by touch alone):
//
//     OSC    Osc 1 / Osc 2 / Mixer
//     FILT   (graphical: response curve)
//     ENV    Amp / Filter / Pitch / ALL   (graphical: curve + 3-way overlay)
//     LFO    LFO 1 / LFO 2
//     FX     Effects / Delay / Reverb
//     SEQ    (graphical: step grid)
//     VOICE  Voice Mode / Velocity
//     PERF   Performance / Clock / Master
//
//   All 140 parameters land on exactly one page. Verified by test, not by eye —
//   an earlier hand-written version of this table silently put Reverb under
//   PERF and Velocity under FX because the section indices were guessed.
//
// CONDITIONAL ROWS (visible_when)
//   A parameter that cannot do anything in the current engine state is not
//   DRAWN — the VA filter's 17 types are meaningless under the OBXa ladder, and
//   supersaw detune does nothing unless the wave IS a supersaw.
//
//   Hidden is NOT disabled. The engine keeps the value and a patch still stores
//   it, so flipping filter engine back and forth does not destroy your settings.
//
//   The rules are DECLARED in params.yaml and generated into ParamTable.h.
//   Hard-coding `if (engine == OBXa)` in the renderer would rebuild exactly the
//   drift trap that made the old ParamFormat disagree with the engine about how
//   many LFO waveforms exist.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "JtParamModel.h"
#include "JtParamStore.h"

namespace JtNav {

// How a page is drawn. The list is the default; the rest override it entirely,
// because a curve or a step grid IS the parameter in a way no list can be.
enum class PageKind : uint8_t {
    List,        // label + value rows (the default)
    Envelope,    // curve + values, with a 3-envelope overlay tab
    Sequencer,   // step bar grid
    Filter,      // response curve + values
    Home         // scope, voice dots, master volume — the one place bars belong
};

// ── Group masks ──────────────────────────────────────────────────────────────
// Every parameter carries a `group` (0..kMaxGroups-1) inside its section, and
// params.yaml names those groups (ParamTable.h kGroupNames). A sub-tab may show
// only some groups of a section: bit g set = group g is shown.
//
// Why: section 9 "Effects" holds EQ, drive, modulation, delay, tape echo and
// output — up to 20 rows, more than one list page holds (kMaxRows = 18). Two
// sub-tabs of the same section, each taking some of its groups, split it
// without touching params.yaml, the firmware, JUCE or saved patches.
//
// All bits set = every group, so a sub-tab that does not name a mask behaves
// exactly as before this change (see the default below).
static constexpr uint8_t kAllGroups = 0xFF;

constexpr uint8_t groupBit(uint8_t g) { return static_cast<uint8_t>(1u << g); }

// 8 bits in the mask; the generator caps groups per section at kMaxGroups.
static_assert(JT::Params::kMaxGroups <= 8, "group mask is 8 bits wide");

struct SubTab {
    const char* name;
    uint8_t     section;                 // index into JT::Params::kSectionNames
    uint8_t     groupMask = kAllGroups;  // which of the section's groups to show
};

struct Page {
    const char* name;
    PageKind    kind;
    SubTab      subs[4];
    uint8_t     subCount;
    bool        hasOverlayTab;   // ENV only: a 4th tab showing all three at once
};

static constexpr uint8_t kPageCount   = 9;   // HOME + the 8 editing pages
static constexpr uint8_t kOverlaySub  = 0xFE;   // the ENV "ALL" tab

const Page& page(uint8_t idx);

// ── Visibility ───────────────────────────────────────────────────────────────
// True if this parameter should be DRAWN given the current engine state.
// Reads ParamDesc::visIf / visOpts, which the generator filled from
// params.yaml's visible_when.
bool isVisible(const JT::Params::ParamDesc& d, const JtParam::Store& store);

// ── The rows of one sub-tab, after visibility filtering ──────────────────────
// 18 = 2 columns x 9 rows of the list layout (RowList kRowsPerCol). Effects
// used to reach 20 with the tape engine and silently lost its last two rows
// (DLY FLUT, DLY WOWR); it is now split across EFFECTS and DELAY.
static constexpr uint8_t kMaxRows = 18;

struct RowSet {
    uint16_t ordinal[kMaxRows];
    uint8_t  count;
};

// Collect the visible rows of `section`, in table order, keeping only the
// groups set in `groupMask` (kAllGroups = all of them).
//
// `skip` (optional) drops a parameter by ID BEFORE the kMaxRows cap is
// applied. That ordering matters: the sequencer and arp sections carry 32 and
// 48 per-step parameters that the grid edits and the list must never show, and
// they outnumber kMaxRows on their own. Filtering after the cap would work
// only because the generator appends — i.e. by luck about table order — and
// would silently truncate real rows the day anything is inserted.
//
// The group test also runs before the cap, for the same reason: rows of a
// group this tab does not show must not use up row slots.
using RowSkipFn = bool (*)(uint16_t id);
void collectRows(uint8_t section, const JtParam::Store& store, RowSet& out,
                 RowSkipFn skip = nullptr, uint8_t groupMask = kAllGroups);

} // namespace JtNav
