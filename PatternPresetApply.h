// =============================================================================
// PatternPresetApply.h — apply a generated pattern preset to the param store
// =============================================================================
// WHAT
//   Writes one preset from PatternPresets.h (generated from params.yaml by
//   jt8000-params/tools/emit_presets.py) into the controller's Store, through
//   the SAME setter a knob turn uses. Nothing preset-specific reaches the wire:
//   the store's dirty set is drained by ViewController::flushDirty(), which
//   emits one ordinary NRPN per changed parameter.
//
// DIFF-ONLY FOR FREE
//   Store::set() exact-compares before marking dirty, so writes that match the
//   current value cost nothing on the UART. A full arp preset is ~55 writes;
//   moving between two Chord presets typically sends 10-25 of them.
//
// WHY ENGINEERING UNITS ARE CONVERTED HERE
//   The table holds engineering values (what the YAML author wrote). The store
//   is normalised, so each write goes through JtParam::toNorm — the one curve
//   implementation the controller already trusts for every other edit.
//
// LAYERS
//   Arp params are scope `patch` (one copy per layer); seq params are
//   `patch_shared`. Either way the flush addresses the CURRENT edit layer, so a
//   preset applied with layer B selected lands on B — exactly like a knob edit.
//
// COST
//   Control plane only, on a user action. No per-frame work, no RAM beyond the
//   stack: the tables are constexpr in flash.
//
// NO ARDUINO / GFX DEPENDENCY, deliberately: this file and its tables compile
// and test on the host. Which lane browses which family is a UI decision and
// lives in ViewController.
// =============================================================================
#pragma once

#include <stdint.h>

#include "JtParamStore.h"
#include "PatternPresets.h"

namespace JtPresets {

using JT::PatternPresets::FamilyId;

// Result of one apply — for the caller's status line / debug log.
struct ApplyResult {
    uint16_t writes  = 0;   // writes in the preset
    uint16_t changed = 0;   // writes that actually changed a value (= NRPNs sent)
    uint16_t missing = 0;   // ParamIDs this controller's table does not know
};

// Apply preset `index` of family `fam`. Out-of-range index/family is a no-op
// returning an all-zero result. Missing IDs are skipped, never fatal: a
// controller flashed with an older ParamTable.h still applies everything it
// can, and `missing` says the tables are out of step.
ApplyResult apply(JtParam::Store& store, FamilyId fam, uint8_t index);

} // namespace JtPresets
