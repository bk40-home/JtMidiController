// =============================================================================
// PageManager.cpp
// =============================================================================
#include "PageManager.h"

void PageManager::begin() {
    // Populate CC cache with defaults from the master parameter table.
    // Every ParamDef carries its own defaultVal — no hardcoded list needed.
    memset(ccState_, 0, sizeof(ccState_));
    for (size_t i = 0; i < kParamCount; ++i) {
        if (kParams[i].cc < Config::CC_STATE_SIZE) {
            ccState_[kParams[i].cc] = kParams[i].defaultVal;
        }
    }

    // Seed pickup targets from HOME page mapping
    refreshPickupTargets();
}

// ── Per-loop update ─────────────────────────────────────────────────────────

void PageManager::update(Angle8Unit& angle, Encoder8Unit& encoder,
                         ByteButtonUnit& buttons) {
    // 1. Page navigation (ByteButton presses)
    handlePageButtons(buttons);

    // 2. Scene switches (Angle8 and Encoder8 toggle switches)
    handleSceneSwitches(angle.switchOn(), encoder.switchOn());

    // 3. Pot input → CC output (with pickup mode)
    handlePots(angle);

    // 4. Encoder input → CC output (with push actions)
    handleEncoders(encoder);
}

// ── Active mapping lookup ───────────────────────────────────────────────────

const PageMapping& PageManager::activeMapping() const {
    return PageMap::getMapping(page_, subPage_);
}

uint8_t PageManager::getCCValue(uint8_t cc) const {
    return (cc < Config::CC_STATE_SIZE) ? ccState_[cc] : 0;
}

uint8_t PageManager::getStepValue(uint8_t step) const {
    return (step < 16) ? stepValues_[step] : 0;
}

// ── Incoming CC from Teensy ─────────────────────────────────────────────────

void PageManager::onReceiveCC(uint8_t cc, uint8_t value) {
    if (cc >= Config::CC_STATE_SIZE) return;
    ccState_[cc] = value;

    // Track step sequencer values from Teensy (select + value pairs)
    if (cc == CC::SEQ_STEP_SELECT) {
        lastStepSelect_ = value;
    } else if (cc == CC::SEQ_STEP_VALUE && lastStepSelect_ < 16) {
        stepValues_[lastStepSelect_] = value;
    }

    // Update pickup target if this CC is on the current pot mapping
    const auto& map = activeMapping();
    const ControlSlot* pots = (potScene_ == Scene::A) ? map.potsA : map.potsB;
    for (uint8_t i = 0; i < 8; ++i) {
        if (pots[i].isActive() && pots[i].cc == cc) {
            pickup_.setTarget(i, value);
        }
    }
}

// ── Page navigation ─────────────────────────────────────────────────────────

void PageManager::handlePageButtons(ByteButtonUnit& buttons) {
    // ── Long-press pass (any button) ────────────────────────────────────────
    // Checked FIRST, across every button, before any short-press logic runs.
    // This is the only way to reach PTCH — there's no dedicated button index
    // for it, by design (see PageDefs.h PageID::PTCH comment).
    for (uint8_t i = 0; i < ByteButtonUnit::kNumButtons; ++i) {
        if (!buttons.longPressed(i)) continue;
        buttons.clearLongPress(i);

        // A long-press always also has a pending short-press latched from
        // the same rising edge (ByteButtonUnit doesn't know in advance how
        // long a hold will last). Consume it here WITHOUT acting on it —
        // otherwise releasing after a long-press would also fire a page
        // switch on whichever button was held.
        buttons.clearPress(i);

        if (page_ != PageID::PTCH) {
            openPatchManager();
            Serial.printf("[PAGE] Long-press btn %u -> PTCH\n", i);
        }
        // If already in PTCH, a second long-press is a no-op — short-press
        // is the documented close gesture, consistent with every other page.
        return;  // one page action per poll cycle, same as the short-press loop below
    }

    // ── Short-press pass ─────────────────────────────────────────────────────
    for (uint8_t i = 0; i < ByteButtonUnit::kNumButtons; ++i) {
        if (!buttons.pressed(i)) continue;
        buttons.clearPress(i);

        // ByteSwitch buttons 0..7 map to pages 1..8 (OSC..PERF). PTCH (9) is
        // intentionally unreachable here — long-press only, handled above.
        selectPage(static_cast<PageID>(i + 1));

        // One page action per poll cycle — see selectPage()'s PTCH note for
        // why this early return matters while PTCH is open.
        return;
    }
}

// Switch to `target`, applying the shared re-press-to-close convention used by
// every page-entry path (ByteButtons and now touch tabs). Extracted so touch
// and buttons drive ONE code path — they can't drift out of sync.
void PageManager::selectPage(PageID target) {
    if (page_ == target || page_ == PageID::PTCH) {
        // Re-selecting the active page, OR any selection while PTCH is open,
        // closes back to HOME — the same convention every page uses.
        page_    = PageID::HOME;
        subPage_ = 0;
    } else {
        page_    = target;
        subPage_ = 0;  // always start at the first sub-page
    }

    // Page changed — refresh pickup targets for the new mapping.
    refreshPickupTargets();

    Serial.printf("[PAGE] → %s (sub %u)\n", activeMapping().tab, subPage_);
}

void PageManager::openPatchManager() {
    page_    = PageID::PTCH;
    subPage_ = 0;
    // PTCH has no pots, so pickup targets are irrelevant, but refreshing
    // keeps state consistent if the user backs out without scrolling.
    refreshPickupTargets();
}

// ── Scene switch handling ───────────────────────────────────────────────────

void PageManager::handleSceneSwitches(bool potSwitch, bool encSwitch) {
    // Angle8 switch: edge detect
    if (potSwitch != prevPotSwitch_) {
        prevPotSwitch_ = potSwitch;
        potScene_ = potSwitch ? Scene::B : Scene::A;
        refreshPickupTargets();
        Serial.printf("[SCENE] Pots → %s\n",
                      potScene_ == Scene::A ? "A" : "B");
    }

    // Encoder8 switch: edge detect
    if (encSwitch != prevEncSwitch_) {
        prevEncSwitch_ = encSwitch;
        encScene_ = encSwitch ? Scene::B : Scene::A;
        Serial.printf("[SCENE] Encoders → %s\n",
                      encScene_ == Scene::A ? "A" : "B");
    }
}

// ── Pot handling ────────────────────────────────────────────────────────────

void PageManager::handlePots(Angle8Unit& angle) {
    if (!angle.isPresent()) return;

    const auto& map = activeMapping();
    const ControlSlot* pots = (potScene_ == Scene::A) ? map.potsA : map.potsB;

    for (uint8_t i = 0; i < Angle8Unit::kNumPots; ++i) {
        // Skip unmapped slots
        if (!pots[i].isActive()) continue;
        // Skip if the CC value hasn't changed
        if (!angle.ccChanged(i)) continue;

        const uint8_t ccVal = angle.ccValue(i);
        angle.clearChanged(i);

        // Step sequencer pots need special handling (step select + value)
        if (pots[i].type == CtrlType::STEP_VAL) {
            // Only send if pickup allows it
            if (pickup_.process(i, ccVal)) {
                handleStepPot(i, ccVal);
            }
            continue;
        }

        // Standard pot: apply pickup mode
        if (!pickup_.process(i, ccVal)) continue;

        // Pickup passed — send the CC
        emitCC(pots[i].cc, ccVal);
    }
}

// ── Encoder handling ────────────────────────────────────────────────────────

void PageManager::handleEncoders(Encoder8Unit& encoder) {
    if (!encoder.isPresent()) return;

    const auto& map = activeMapping();
    const ControlSlot* encs = (encScene_ == Scene::A) ? map.encsA : map.encsB;

    for (uint8_t i = 0; i < Encoder8Unit::kNumEncoders; ++i) {
        const ControlSlot& slot = encs[i];
        if (!slot.isActive() && !slot.isSubSel()) continue;

        // ── Push button handling ────────────────────────────────────────
        if (encoder.pressed(i)) {
            encoder.clearPress(i);

            if (slot.isSubSel()) {
                // Sub-page selector: switch sub-page
                handleSubPageSelect(i);
            } else if (slot.type == CtrlType::ACTION) {
                // Patch Manager command (LOAD/SAVE/IMPORT push). No CC,
                // no local cache update — entirely PatchManager's concern.
                if (actionCB_) actionCB_(i, /*isPush=*/true, /*delta=*/0);
            } else if (slot.type == CtrlType::TOGGLE) {
                // Toggle: flip between 0 and 127
                const uint8_t current = getCCValue(slot.cc);
                emitCC(slot.cc, (current > 63) ? 0 : 127);
            } else if (slot.type == CtrlType::BIPOLAR) {
                // Bipolar reset: push → centre (64)
                emitCC(slot.cc, 64);
            } else if (slot.type == CtrlType::SELECT) {
                // Select cycle: push advances by one option
                applyEncoderDelta(slot, 1);
            }
        }

        // ── Rotation handling ───────────────────────────────────────────
        const int32_t d = encoder.delta(i);
        if (d != 0 && slot.type == CtrlType::ACTION) {
            // Patch Manager scroll. Routed directly — never touches CC
            // dispatch or applyEncoderDelta's 0..127 clamping, since this
            // is a list index, not a parameter value.
            if (actionCB_) actionCB_(i, /*isPush=*/false, d);
        } else if (d != 0 && slot.isActive()) {
            applyEncoderDelta(slot, d);
        }
    }
}

// ── Sub-page selection ──────────────────────────────────────────────────────

void PageManager::handleSubPageSelect(uint8_t encIdx) {
    if (!PageMap::hasSubPages(page_)) return;

    const uint8_t maxSub = PageMap::subPageCount(page_);
    if (encIdx >= maxSub) return;

    if (subPage_ != encIdx) {
        subPage_ = encIdx;
        refreshPickupTargets();
        Serial.printf("[SUBPAGE] → %s\n", activeMapping().name);
    }
}

// ── Touch-to-edit (Phase 2) ─────────────────────────────────────────────────

const ControlSlot* PageManager::slotForCell(uint8_t cellIdx,
                                             bool& outIsEncoder) const {
    outIsEncoder = false;
    if (cellIdx >= 8) return nullptr;

    const PageMapping& map = activeMapping();
    const ControlSlot* pots = (potScene_ == Scene::A) ? map.potsA : map.potsB;
    const ControlSlot* encs = (encScene_ == Scene::A) ? map.encsA : map.encsB;

    // Rebuild the SAME encoder queue drawPage()/refreshValues() use so cell
    // indices line up exactly with what's on screen.
    uint8_t encQueue[8];
    uint8_t encCount = 0;
    for (uint8_t i = 0; i < 8 && encCount < 8; ++i) {
        if (encs[i].isActive() && !encs[i].isSubSel()) encQueue[encCount++] = i;
    }

    uint8_t nextEnc = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (pots[i].isActive()) {
            if (i == cellIdx) return &pots[i];
        } else if (nextEnc < encCount) {
            const ControlSlot& enc = encs[encQueue[nextEnc++]];
            if (i == cellIdx) { outIsEncoder = true; return &enc; }
        }
        // empty cell — nothing placed here
        if (i == cellIdx) return nullptr;
    }
    return nullptr;
}

void PageManager::applyTouchDrag(const ControlSlot& slot, uint16_t curY) {
    // ~1 value per 1.5 px (signed-off sensitivity). Up = increase, so the
    // delta is (startY - curY) because screen Y grows downward. We compute an
    // ABSOLUTE target from the grab baseline each frame, then emit the change
    // relative to the current cached value via applyEncoderDelta — this avoids
    // drift from accumulating per-frame rounding.
    const int dyPx     = (int)editStartY_ - (int)curY;       // up positive
    const int valDelta = (dyPx * 2) / 3;                     // 1 / 1.5 px

    if (slot.type == CtrlType::SELECT) {
        // One option step per ~12 px so discrete options don't blur past.
        const int step = dyPx / 12;
        const int target = (int)editStartVal_ + step;  // bucket space, clamped below
        const int cur = (int)getCCValue(slot.cc);
        const int d = target - cur;
        if (d != 0) applyEncoderDelta(slot, d);
        if (dyPx > 6 || dyPx < -6) editMoved_ = true;
        return;
    }

    // CONT / ENV / BIPOLAR: target = grabbed value + travel, clamped 0..127.
    int target = (int)editStartVal_ + valDelta;
    if (target < 0)   target = 0;
    if (target > 127) target = 127;
    const int cur = (int)getCCValue(slot.cc);
    const int d = target - cur;
    if (d != 0) applyEncoderDelta(slot, d);
    if (dyPx > 4 || dyPx < -4) editMoved_ = true;  // moved past tap threshold
}

void PageManager::handleContentTouch(uint8_t cellIdx, uint16_t curY,
                                     bool touched) {
    // Touch edits only make sense on the normal parameter grid. ENV/SEQ/PTCH/
    // HOME draw their own content with no editable cells, so ignore them.
    const bool editablePage = (page_ != PageID::HOME && page_ != PageID::ENV
                            && page_ != PageID::SEQ  && page_ != PageID::PTCH);

    // ── Press edge: grab the cell under the finger ──────────────────────────
    if (touched && !editPrevTouch_) {
        editPrevTouch_ = true;
        editMoved_     = false;
        if (editablePage && cellIdx != kNoCell) {
            bool isEnc = false;
            const ControlSlot* s = slotForCell(cellIdx, isEnc);
            if (s && s->isActive()) {
                editCell_     = cellIdx;
                editStartY_   = curY;
                editStartVal_ = getCCValue(s->cc);
            } else {
                editCell_ = kNoCell;   // empty/inactive cell — nothing to edit
            }
        } else {
            editCell_ = kNoCell;
        }
        return;
    }

    // ── Held: drag the grabbed cell ─────────────────────────────────────────
    if (touched && editCell_ != kNoCell) {
        bool isEnc = false;
        const ControlSlot* s = slotForCell(editCell_, isEnc);
        if (s && s->isActive() && s->type != CtrlType::TOGGLE) {
            applyTouchDrag(*s, curY);
        } else if (s && s->type == CtrlType::TOGGLE) {
            // TOGGLE: a drag does nothing; the flip happens on tap-release.
            if ((int)editStartY_ - (int)curY > 6 ||
                (int)curY - (int)editStartY_ > 6) {
                editMoved_ = true;   // moved too far → not a tap, cancel flip
            }
        }
        return;
    }

    // ── Release edge ────────────────────────────────────────────────────────
    if (!touched && editPrevTouch_) {
        editPrevTouch_ = false;
        if (editCell_ != kNoCell && !editMoved_) {
            // A tap that never moved. TOGGLE flips; other types do nothing
            // (their value already changed live during the drag).
            bool isEnc = false;
            const ControlSlot* s = slotForCell(editCell_, isEnc);
            if (s && s->type == CtrlType::TOGGLE) {
                const uint8_t cur = getCCValue(s->cc);
                emitCC(s->cc, (cur > 63) ? 0 : 127);
            }
        }
        // Keep editCell_ set so the renderer can show the last-focused cell.
        // It is re-grabbed or cleared on the next press edge.
    }
}

// ── Encoder delta application ───────────────────────────────────────────────

void PageManager::applyEncoderDelta(const ControlSlot& slot, int32_t delta) {
    const uint8_t current = getCCValue(slot.cc);
    uint8_t newVal;

    switch (slot.type) {
        case CtrlType::CONT:
        case CtrlType::ENV:
            // Continuous: clamp 0..127
            newVal = clamp7((int)current + (int)delta);
            break;

        case CtrlType::BIPOLAR:
            // Bipolar: clamp 0..127 (64 = centre, no dead zone needed here
            // because the dead-zone logic is on the Teensy receiving end)
            newVal = clamp7((int)current + (int)delta);
            break;

        case CtrlType::SELECT: {
            // Select: wrap around. Each detent = one option step.
            // We don't know the option count here, so wrap at 0..127.
            // The Teensy side uses bucket decoding to map CC to option index.
            int val = (int)current + (int)delta;
            if (val < 0)   val = 127;
            if (val > 127) val = 0;
            newVal = (uint8_t)val;
            break;
        }

        default:
            return;  // TOGGLE, SUB_SEL handled by push, not rotation
    }

    if (newVal != current) {
        emitCC(slot.cc, newVal);
    }
}

// ── CC emit + cache update ──────────────────────────────────────────────────

void PageManager::emitCC(uint8_t cc, uint8_t value) {
    // Update local cache
    if (cc < Config::CC_STATE_SIZE) {
        ccState_[cc] = value;
    }

    // Debug: show every CC emit in serial monitor
    Serial.printf("[CC] %u = %u\n", cc, value);

    // Send via registered callback (→ UartMidi → Teensy)
    if (sendCC_) {
        sendCC_(cc, value);
    }
}

// ── Pickup target refresh ───────────────────────────────────────────────────

void PageManager::refreshPickupTargets() {
    const auto& map = activeMapping();
    const ControlSlot* pots = (potScene_ == Scene::A) ? map.potsA : map.potsB;

    uint8_t targets[8];
    for (uint8_t i = 0; i < 8; ++i) {
        if (pots[i].isActive()) {
            targets[i] = getCCValue(pots[i].cc);
        } else {
            targets[i] = 0;
        }
    }
    pickup_.onPageChange(targets);
}

// ── Step sequencer pot handling ─────────────────────────────────────────────

void PageManager::handleStepPot(uint8_t potIdx, uint8_t value) {
    // Step sequencer addressing: pot position (0..7) + scene state
    // Scene A = steps 0..7, Scene B = steps 8..15
    const uint8_t stepOffset = (potScene_ == Scene::B) ? 8 : 0;
    const uint8_t stepIndex  = potIdx + stepOffset;

    // Cache for display
    if (stepIndex < 16) stepValues_[stepIndex] = value;

    // Send step select first, then step value (Teensy expects this pair)
    emitCC(CC::SEQ_STEP_SELECT, stepIndex);
    emitCC(CC::SEQ_STEP_VALUE, value);
}

// ── Utility ─────────────────────────────────────────────────────────────────

uint8_t PageManager::clamp7(int value) {
    if (value < 0)   return 0;
    if (value > 127) return 127;
    return (uint8_t)value;
}

// ── Patch Manager support ───────────────────────────────────────────────────

void PageManager::bulkLoadCC(const uint8_t* src, uint16_t size) {
    // Defensive: src must be the full CC-indexed buffer PatchStore::load()
    // produces. A mismatched size means the caller passed something else
    // (e.g. a packed kPatchableCCs-sized array by mistake) — bail rather
    // than read out of bounds or silently load garbage.
    if (size != Config::CC_STATE_SIZE) return;

    // Only copy the CCs that are actually patchable. The stored file is a
    // full 160-byte dump, but most of that range is unused padding — we
    // don't want to overwrite ccState_ entries for non-patchable CCs
    // (there shouldn't be meaningful data there, but staying scoped to
    // the schema is the safer contract regardless of what's in the file).
    for (int i = 0; i < PatchSchema::kPatchableCount; ++i) {
        const uint8_t cc = PatchSchema::kPatchableCCs[i];
        if (cc < Config::CC_STATE_SIZE) {
            ccState_[cc] = src[cc];
        }
    }

    // The currently-displayed page may have pots whose pickup target just
    // changed underneath it (e.g. loading a patch while OSC is open).
    // Refreshing here avoids a stale pickup window on the next pot touch.
    refreshPickupTargets();
}
