# JtMidiController — Phase D. Complete folder replacement.

This folder is a **drop-in replacement**. Delete the old one and use this.

## Deleted (10 files, ~3,300 lines)

| file | why |
|---|---|
| `PageDefs.h` | CC-keyed `ControlSlot` — replaced by ParamID-keyed cells |
| `PageMappings.h` | 620 hand-maintained lines. **The layout is now generated from `params.yaml`.** |
| `PageManager.{h,cpp}` | → `ViewController` |
| `DisplayRenderer.{h,cpp}` | → `ViewRenderer` (+ `Display` for board bring-up) |
| `ParamFormat.{h,cpp}` | A **second, hand-written copy** of every option list. It had drifted: LFO waveform had 4 entries when the engine had 6. |
| `ParamDefs.h` | 174 legacy CC constants |
| `JtBridgeTable.h` | v1CC↔ParamID bridge. Nothing is CC-keyed any more. |

## Added

| file | role |
|---|---|
| `ParamTable.h` | **GENERATED** from `params.yaml` — the single source of truth |
| `JtParamModel.{h,cpp}` | norm↔eng, select index, stepping, formatting |
| `JtParamStore.{h,cpp}` | `float[140]` normalised, ordinal-indexed, dirty bitset |
| `JtNrpn.{h,cpp}` | 14-bit NRPN, ParamID-keyed |
| `ViewModel.{h,cpp}` | Lays out a section; binds hardware by derived policy |
| `ViewRenderer.{h,cpp}` | Draws it. Idle frame = **zero** draw calls |
| `ViewController.{h,cpp}` | Input routing + NRPN dispatch |
| `Display.{h,cpp}` | TFT bring-up, split out of `DisplayRenderer` |

## Rewritten

- **`UartMidi`** — now a **dumb byte pipe**. It used to own the NRPN protocol and take a `uint8_t value7`, re-quantising every value to 128 steps at that seam. It also *dropped* inbound params with no v1 CC (`master.volume`).
- **`SelectPopup`** — keyed by ParamID, options from the generated table. **It was dead code**: fully implemented, never instantiated. The `.ino` now collects the request.
- **`PatchStore`** — format **v2**: header + 140 normalised floats. ⚠️ **v1 patches will not load** (they have no magic and are refused, not misread).
- **`PatchManager`** — save/load via `Store::loadAll()` + `markAllDirty()`. The manual CC relay loop is gone.
- **`LedManager`** — section colours; encoders lit only when they drive something; **pots go red while seeking pickup**.
- **`JtMidiController.ino`** — new loop; wires `SelectPopup` into the modal slot.

## Unchanged (drop-in)

`Angle8Unit` · `Encoder8Unit` · `ByteButtonUnit` · `TouchInput` · `TCA9554` · `ColorUtils` · `PerfMonitor` · `NameEditor` · `Config.h` · `JT8000_OptFlags.h`

---

## Two bugs caught during integration

**1. NRPN address caching vs the RPN park — these are mutually exclusive.**

My emitter cached the NRPN address and sent only 2 CCs on a repeat. The firmware (`MidiParamTransport::handleControlChange`) treats CC101/100 as `Selected::Rpn`, which **deselects the NRPN**. Combining the two means every send after the first is silently swallowed — a pot would move once and then freeze.

**Resolved:** caching removed, park kept. Every send is a self-contained 6 CCs. Verified against a faithful port of the firmware state machine.

**2. `Angle8Unit::setLed` takes separate r/g/b + brightness**, not a packed `0xRRGGBB` like the Encoder8 and ByteButton. Caught by `-Werror`.

---

## Verified

All under `-Wall -Wextra -Wdouble-promotion -Werror`, against the **real** unit headers.

- **140 cells for 140 parameters.** Nothing hidden. (Was: 43 unreachable.)
- Every section fits one 480×320 screen — worst is 244 px of 298.
- NRPN round-trips through a port of the **real firmware state machine**.
- Full patch dump: 840 CCs = 2,520 bytes ≈ **25 ms** at 1 Mbaud.
- LFO waveform reaches S&H and NOISE.
- FX mod/delay carry canonical names, one detent = one option.

## Still to do

1. **HOME / performance screen** — deliberately deferred until the section screens are proven on hardware.
2. **Hardware gates** — ESP32 build, FLASH/RAM numbers. I cannot run these.
3. `ParamTable.h` is a **copy** of the generated file. Add it to `sync_cc_defs.py` so CI catches drift.
