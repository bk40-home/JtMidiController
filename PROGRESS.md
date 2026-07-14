# Phase E — progress

## Verified so far

### The 140 are intact
You asked about "120 vs 140". **140 is correct** — the ~120 in the spec was my sloppy
shorthand for "params landing in the Tier-1 list" (i.e. minus the graphical pages). Tested:
**all 140 land on exactly one page, no orphans, no section on two pages.**

The check caught a real error while doing it: my first page table had **Global Reverb under
PERF and Velocity under FX** because I guessed the section indices instead of reading them.
Corrected and asserted.

### `visible_when` — dynamic pages, declared in the yaml
13 rules applied, generated into `ParamTable.h`, tested:

| state | rows |
|---|---|
| FILT · OBXa · 4-Pole | 9 of 11 |
| FILT · OBXa · Xpander | 10 (xpander mode appears) |
| FILT · VA | 7 (all OBXa rows gone, VA TYPE appears) |
| OSC1 · SAW | 9 of 13 |
| OSC1 · SSAW | 11 (supersaw rows appear) |
| OSC1 · ARB | 11 (wavetable rows appear) |

**Hidden ≠ disabled** — tested: a value set while visible survives a round-trip through a
wave change and comes back intact. The engine keeps it, patches store it.

**Applied:** filter engine split, xpander modes, supersaw (SSAW), wavetable (ARB).
**Not applied per your instruction:** delay time/sync and LFO freq/sync stay both on screen;
LFO pwm_depth stays (no variable wave type yet).

### Xpander modes named
From your `kObxaPoleMix[15][5]`, verbatim:
`LP4, LP3, LP2, LP1, HP3, HP2, HP1, BP4, BP2, N2, PH3, HP2+LP1, HP3+LP1, N2+LP1, PH3+LP1`

### Label collisions fixed (13)
The list makes them unacceptable — two rows both reading `TYPE`, two `MIX`, two `FB`.
Now `MOD TYPE`/`DLY TYPE`, `MOD MIX`/`DLY MIX`, `MOD FB`/`DLY FB`, `SS MIX`/`FB MIX`.
**Zero collisions across all 17 sections.**

### RowList — the value list
2 columns × 9 rows = 18 slots. Worst sub-tab (Effects, 14) fits with room spare. **Nothing
scrolls.** Idle frame = **0 draw ops**; one changed row = 4 ops.

## Two bugs caught by test

**1. Sentinel collision — `osc1.wave` IS ParamID `0x0000`.**
I used `0` for "no visibility condition". That silently disabled *every* oscillator rule while
the filter rules worked fine — supersaw/wavetable rows would simply never have hidden.
Sentinel changed to `0xFFFF` (`kNoVisDep`).

**2. Text punching holes through the focus bar.**
On the focused row the text sits *over* the value bar, whose colour varies along the row.
Opaque glyph backgrounds would have painted rectangles through it. Fixed: transparent glyphs
on the focused row only.

## Next

- `NavBar` — page title + drop-down, sub-tab strip, swipe
- Port `EnvPanel` + `SeqPanel` back from the deleted `DisplayRenderer`
- `FilterPanel` — response curve
- `HomePanel` — scope, voice dots, master volume
- Re-wire `ViewController` / `ViewRenderer`
- **Patch load/save + NameEditor keyboard retained** (unchanged from Phase D)
