# JtMidiController — Phase E. Complete folder replacement.

**Drop-in replacement.** Delete the old folder, use this one.

## What Phase D got wrong, and why you saw no change

Phase D flattened all 140 parameters into one uniform grid of **knob arcs**. Three mistakes:

1. **Knob for everything.** A knob is right when the data maps onto a *circle*. Attack time
   (1–11880 ms, log) does not. An arc at "about 60%" is unreadable — you need `240 ms`.
2. **Deleted the specialised renderers** — the envelope curve, the 3-envelope overlay, the
   sequencer bar grid. For those, *the graphic IS the parameter*.
3. **Broke navigation** — 17 flat sections against 8 buttons, no touch nav. 9 unreachable.

The last message compiled `NavModel` and `RowList`, but **nothing used them** — the old
`ViewRenderer` knob grid was still driving the screen. That's why you saw no difference.
This completes the wiring.

## Deleted

| file | replaced by |
|---|---|
| `ViewModel.{h,cpp}` | `NavModel` (8 pages, sub-tabs, `visible_when`) |
| `ViewRenderer.{h,cpp}` | `NavBar` + `RowList` + `EnvPanel` + `SeqPanel` + `FilterPanel` |

## Added

| file | role |
|---|---|
| `NavModel.{h,cpp}` | 8 pages, sub-tabs, conditional row visibility |
| `NavBar.{h,cpp}` | Header, sub-tab strip, page drop-down, **all touch nav** |
| `RowList.{h,cpp}` | The default: two-column `LABEL ... VALUE`. **No knobs.** |
| `EnvPanel.{h,cpp}` | Envelope curve + ALL overlay — **ported back from the deleted renderer** |
| `SeqPanel.{h,cpp}` | Step bar grid — **ported back** |
| `FilterPanel.{h,cpp}` | Response curve, redrawn from the live mode |

## The layout

**8 pages = 8 ByteButtons** — but the buttons are now *shortcuts*. Everything is reachable by
touch: tap the page name for a drop-down, tap a sub-tab, swipe left/right to page.

| btn | page | sub-tabs |
|---|---|---|
| 1 | OSC | OSC 1 · OSC 2 · MIX |
| 2 | FILT | *(response curve)* |
| 3 | ENV | AMP · FILTER · PITCH · **ALL** |
| 4 | LFO | LFO 1 · LFO 2 |
| 5 | FX | EFFECTS · REVERB |
| 6 | SEQ | *(bar grid)* |
| 7 | VOICE | VOICE · VELOCITY |
| 8 | PERF | PERFORM · CLOCK · MASTER |

**140/140 params, no orphans, nothing scrolls.** Worst sub-tab (Effects, 14) fits 2×9=18 slots.

**Hardware binds to the visible rows:** pots take rows 0–7, encoders rows 8–15. A control with
no row is **dark** — an unlit knob genuinely does nothing.

**The focused row — and only it — gets a value bar.** Sweep feedback where you're looking.

## Dynamic pages (`visible_when`, declared in `params.yaml`)

| state | rows |
|---|---|
| FILT · OBXa | 9 of 11 |
| FILT · OBXa · Xpander | 10 |
| FILT · VA | 7 (OBXa rows gone, VA TYPE appears) |
| OSC1 · SAW | 9 of 13 |
| OSC1 · SSAW / ARB | 11 |

**Hidden ≠ disabled.** The engine keeps the value; patches store it. Flipping engine back and
forth does not destroy your settings.

## Also fixed

- **Xpander modes named** from your `kObxaPoleMix`: `LP4, LP3, LP2, LP1, HP3 … PH3+LP1`
- **13 label collisions** — the FX list showed `TYPE/MIX/FB` twice. Now `MOD TYPE`/`DLY TYPE` etc.

## Retained unchanged

Patch load/save, `NameEditor` keyboard, `PatchStore` v2, `PatchManager`, `SelectPopup`,
`UartMidi`, `JtNrpn`, `JtParamStore`, `PickupMode`, and your `I2C_CLOCK_HZ` / `loopBegin()` fixes.

## Two bugs caught by test

1. **`osc1.wave` IS ParamID `0x0000`** — and I'd used `0` as the "no visibility rule" sentinel.
   That silently disabled *every* oscillator rule while the filter's worked. Now `0xFFFF`.
2. **Text punching holes through the focus bar** — the value sits over the bar, whose colour
   varies along the row, so opaque glyph backgrounds would have painted rectangles through it.

## Verified

All under `-Wall -Wextra -Wdouble-promotion -Werror`.

- 8 pages render; all sub-tabs incl. the ENV overlay
- Touch nav: title menu, sub-tab strip, swipe — all three
- Dynamic rows change live with the engine
- Tap a select → popup request parked and collected
- **Idle frame = 0 draw ops**

---

# Phase E.1 — the two bugs you found

## 1. Touching the display flipped the page

**Cause:** the swipe test was `dx > 70 && !touchMoved_` — but `touchMoved_` is only ever set
when the finger lands *on a row*. On a graphical page (no rows under the graphic) or on any
blank area, that guard was **vacuous**, so a few pixels of touch jitter counted as a swipe.

**Fix:** a swipe now requires all three of —
1. **60 px** of travel (a gesture, not a jitter)
2. **mostly horizontal** — `|dx| >= 2 * |dy|`, so a vertical value drag that wanders sideways
   never pages
3. **no value was edited** during the gesture

*One further catch:* my first fix also rejected any swipe that merely **started on a row**.
That killed swiping on every list page, because rows cover the whole content area there —
left-to-right silently did nothing. A row drag is *vertical*; a swipe is *horizontal*. It is
direction that separates them, not the starting point.

## 2. Filter curve and sequencer grid were behind the text

**Cause:** the graphics and the row list **both started at y = 46**. The panels drew first,
then `RowList` painted nine rows straight over them.

**Fix:** the row origin is now a **per-page property**, not a constant.

| page kind | graphic | rows |
|---|---|---|
| **List** (OSC, LFO, FX, VOICE, PERF) | — | y=46, **30 px**, 9/col = **18 slots** |
| **Graphical** (FILT, ENV, SEQ) | y = **46–160** | y=**164**, **26 px**, 6/col = **12 slots** |

SEQ needs 12 rows → fits exactly. FILT needs 9 → fits.

The panels also had to stop over-clearing: `fillRect(..., kCurveH + 8)` and the ENV legend at
`kCurveY + kCurveH + 12` would both have wiped the rows below. Everything now stays inside its
own band.
