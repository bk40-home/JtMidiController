// =============================================================================
// JtParamModel.h — normalised parameter model + display formatting
// =============================================================================
// Phase C. The ONE runtime layer between the generated table (ParamTable.h) and
// everything else on the ESP32: PageManager, DisplayRenderer, SelectPopup,
// PatchStore.
//
// WHY THIS EXISTS
//   Before Phase C the controller was 7-bit CC-keyed: values lived in a
//   uint8_t ccState_[160] indexed by CC number, and a hand-written
//   ParamFormat.cpp carried its own copy of every option list. That copy
//   drifted from params.yaml — LFO waveform decoded against 4 options when the
//   engine had 6, so every non-zero value picked the wrong waveform. This file
//   replaces both. Values are normalised floats keyed by ParamID; option lists,
//   ranges, curves and units all come from the generated table. There is no
//   second table to drift.
//
// THE MODEL
//   t   "normalised"    0..1 float. The canonical transport + storage form.
//                       This is what travels as NRPN and what PatchStore saves.
//   v   "engineering"   Hz, ms, semitones, option index... what the DSP and the
//                       user actually mean. Derived from t via the param's curve.
//
//   Only t is stored. v is computed on demand — it is never the source of
//   truth, so the two can never disagree.
//
// SELECT PARAMS
//   A select's engineering value IS its option index (0..optionCount-1). The
//   table encodes this as min=0, max=count-1, Curve::Lin. So the generic
//   toEng()/toNorm() handle selects with no special case — the bucket maths
//   that used to live in ParamFormat is gone entirely. indexToNorm()/normToIndex()
//   are thin, exactly-rounding wrappers, not a parallel code path.
//
// NO STATE
//   Everything here is a pure function of (ParamDesc, value). No engine reads,
//   no globals, no statics (except the note-name pool, which is const). Safe to
//   call for every visible cell in one redraw pass.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "ParamTable.h"   // GENERATED — the single source of truth

namespace JtParam {

using JT::Params::ParamDesc;
using JT::Params::Type;
using JT::Params::Curve;

// ── Ordinal <-> ParamID ──────────────────────────────────────────────────────
// The value store is a flat array indexed by ORDINAL (table row 0..kParamCount-1),
// not by ParamID — ParamIDs are sparse (0x0000, 0x0080, 0x0100...) so indexing by
// them directly would need a 2 KB array that is 93% holes.
//
// ordinalOf() is O(n) and is called ONCE per control at page-change time, never
// in a hot loop. Returns kNoOrdinal if the id is not in the table.
static constexpr uint16_t kNoOrdinal = 0xFFFF;

uint16_t ordinalOf(uint16_t paramId);

// Direct row access by ordinal. Returns nullptr if out of range.
const ParamDesc* descAt(uint16_t ordinal);

// Convenience: row by ParamID (nullptr if absent). Wraps JT::Params::find().
const ParamDesc* descOf(uint16_t paramId);

// ── Normalised <-> engineering ───────────────────────────────────────────────
// toEng: t (0..1) -> engineering units, honouring the param's curve.
//   Lin : min + t*(max-min)
//   Log : min * (max/min)^t          — min > 0 guaranteed by the generator
//   Seg2: two linear segments meeting at 'mid' when t == 0.5 (envelope slopes)
// Input t is clamped to 0..1, so a bad caller cannot produce a wild value.
float toEng(const ParamDesc& d, float t);

// toNorm: engineering units -> t (0..1). Exact inverse of toEng for all three
// curves. Input is clamped to [min,max] first.
float toNorm(const ParamDesc& d, float eng);

// ── Select helpers ───────────────────────────────────────────────────────────
// A select's engineering value is its option index. These round to the nearest
// exact index rather than truncating, so a normalised value that has been
// through a float round-trip still lands on the option it started on.
uint8_t normToIndex(const ParamDesc& d, float t);
float   indexToNorm(const ParamDesc& d, uint8_t index);

// ── Defaults ─────────────────────────────────────────────────────────────────
// The table stores 'def' in ENGINEERING units. The store wants normalised.
float defaultNorm(const ParamDesc& d);

// ── Stepping (encoder / touch drag) ──────────────────────────────────────────
// Apply `steps` detents to a normalised value and return the new normalised
// value. Behaviour is type-aware, which is what makes the encoders feel right:
//
//   Select : one detent == exactly one option. Wraps around the ends, matching
//            the old SELECT behaviour. NOT a 1/127 nudge that may or may not
//            cross a bucket edge.
//   Toggle : any non-zero delta flips.
//   Cont   : one detent == 1/128 of the normalised range (so a full sweep is
//            still ~128 detents, preserving the old feel), clamped 0..1.
//
// This is the single place stepping semantics are defined — PageManager's
// encoder path and the touch-drag path both call it, so they cannot diverge.
float step(const ParamDesc& d, float t, int32_t steps);

// Toggle convenience — reads as a bool, writes 0.0/1.0.
bool  isOn(const ParamDesc& d, float t);
float setOn(bool on);

// ── Display ──────────────────────────────────────────────────────────────────
// Format a normalised value as the text shown on screen. Always NUL-terminates.
// buf should be >= 16 bytes to cover every case.
//
// Dispatch is driven by the table, in this order:
//   Select        -> the option string  ("Chorus 1", "S&H", "4+4", "Ch 2")
//   Toggle        -> "On" / "Off"
//   unit "note"   -> note name          ("C4")     — perf.split_note
//   unit "Hz"     -> "440 Hz" / "2.5 kHz"
//   unit "ms"     -> "250 ms" / "2.4 s"
//   unit "BPM"    -> "120"
//   unit "st"     -> "+12"              (semitones)
//   unit "slope"  -> "1.00"             (envelope curve exponent)
//   unit "norm"   -> "75%", or "+25%" when bipolarUi is set
//
// The unit vocabulary is closed — it is exactly the set params.yaml uses. A new
// unit in the yaml with no case here falls back to a plain number, which is
// safe but plain, so add a case when you add a unit.
void format(const ParamDesc& d, float t, char* buf, uint8_t len);

} // namespace JtParam
