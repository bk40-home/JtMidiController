# CHANGELOG — Phase F4.1: SEQ grid tidy-up (hardware feedback)

Changed files (drop-in, CRLF): `SeqPanel.h`, `SeqPanel.cpp`,
`ViewController.h`, `ViewController.cpp`. No table or firmware change; no
`.ino` change.

---

## 1. Bars are now UNIPOLAR — height is the value

The Phase C drawing grew bars up/down from the mid-line (faithful to
"0.5 = no modulation" on bipolar destinations, but hard to edit by eye: a
half-value tap produced a near-empty bar). Bars now grow from the baseline —
what the finger taps is what it gets — and `valueFromY()` is the exact
inverse (bottom = 0.0, top = 1.0). The mid-line stays, drawn over the bars,
as the at-a-glance "no modulation" reference for bipolar destinations.

## 2. Stranded yellow highlights — root cause

The focus outline was drawn ONE PIXEL OUTSIDE the bar rect, and `drawBar()`
erases only the bar rect — so the halo was outside everything that could ever
clean it. Both outlines (focus and playhead) now sit INSIDE the bar rect,
where the erase covers them. Additionally, the highlight was fed `focusRow_`
— a LIST-ROW index (0..17) — as a STEP index, so it highlighted whichever bar
happened to share a number with a touched row and never followed the edit
cursor. It now shows the SELECTED STEP.

## 3. SEL selects, VAL fine-tunes — exactly as you expected

Both rows always reached the engine; neither was wired to the grid, and VAL
never showed the selected step's value — so both looked dead. Now, once per
loop on the SEQ page:

- **SEL change** (encoder, tap on a bar, or inbound): the grid highlight
  moves there and the VAL row LOADS that step's value — quietly (`setQuiet`:
  no dirty mark, no NRPN), because selecting a step is a read; stamping the
  previous step's value onto the new one just by browsing would corrupt the
  pattern. Pot pickup targets are refreshed so the pot bound to VAL seeks the
  newly shown value, not the previous step's.
- **VAL change** (pot, drag, encoder, or inbound NRPN): written through to
  the grid, so the bar follows the fine-tune live. The engine hears it via
  the normal dirty flush.

This also closes the F4 ledger item "inbound step edits don't update the
grid" for the selected step — a DAW editing SEL then VAL over NRPN now moves
the bars.

## Verification

Host compile `-Wall -Wextra -Werror -fsyntax-only`: all touched and
previously-gated translation units clean. No table change — F4's firmware
gates stand.

## Hardware test checklist

1. Tap bars at several heights — bar height lands where the finger is;
   bottom = 0, top = full.
2. Tap step 3, then step 9 — the yellow outline MOVES (no stranded outline
   on 3); it also follows the SEL encoder.
3. Turn SEL — VAL row updates to each step's stored value as you browse;
   browsing alone changes NO step (listen: pattern unchanged).
4. On a chosen step, fine-tune VAL by pot and by drag — the bar tracks live,
   the engine follows.
5. Grab the VAL pot after browsing SEL — pickup engages against the SHOWN
   value (no jump).
6. Regression: taps between bars/outside the grid do nothing; STEPS encoder,
   tap-grid D-5 case (same height on several steps) still fine.

Phase F5 (status feed + HOME + live playhead) remains signed off and queued.
