# PHASE9_ARP_ESP_NOTE.md — ESP32 Controller Arpeggiator UI

Answers the question "does the ESP need more than the ParamTable?" and delivers
the arp UI. Short answer: **the table alone isn't enough**, because the ESP's
navigation hardcodes which sections are reachable — but the change is small and
does NOT touch NavModel.

## What the ParamTable alone gives you (and doesn't)

`ParamTable.h` is generated identically for all repos, so dropping in the
regenerated table (160 params, 18 sections, section 17 = "Arpeggiator") makes
the arp params exist and be sendable. But the ESP builds its 8 editing pages
from a hardcoded `kPages[]` in `NavModel.cpp`; a section is only reachable if a
page lists it. Section 17 is in no page, so with the table alone the arp params
would be present but **unreachable** — nothing would draw them.

## Approach taken: a third SEQ-page flip lane (Gate -> Aux -> Arp)

Rather than spend a NavModel page/sub-tab, the arp rides the SEQ page's existing
"press the page button to flip lane" mechanism. It was 2-state (Gate/Aux); it is
now 3-state (Gate -> Aux -> Arp -> Gate). This matches the muscle memory that's
already there and keeps `NavModel.cpp` **untouched**.

On the Arp flip state:
- The shared 16-bar grid edits the arp's per-step **accent** (`arp.step_accent`),
  drawn in magenta (gate=orange, aux=cyan, arp=magenta).
- The arp's **global** params (enable, mode, octaves, latch, rate, free, gate,
  swing, steps) render generically as the value-list below the grid.
- **On/off** and **ratchet** per step render as list rows too (the low-risk
  substitute for a bespoke tri-lane grid — see "Deferred").

## Files changed

- **`ParamTable.h`** — regenerated (drop-in; adds section 17 + the 13 arp params).
  Normalised to LF to match the repo (the generator emits CRLF; the committed
  ESP copy was LF — flagged below).
- **`SeqPanel.h/.cpp`** — `Lane` enum 2 -> 3 (`Arp`); `toggleLane()` cycles three;
  `arpSteps_[16]` cache + 3-way `active()`/`cacheFor()`; magenta arp bar colour.
- **`ViewController.h/.cpp`** — resolve `ARP_STEP_SELECT`/`ARP_STEP_ACCENT`
  ordinals; `currentSection()` returns 17 on the Arp lane; grid edit + touch
  writes route to the arp SEL/ACCENT pair; row filter hides the grid-driven
  select/accent from the list while keeping on/off + ratchet as rows.

`NavModel.cpp` is deliberately unchanged.

## Verification (structural — Arduino/ESP not compilable in my env; build in the IDE)

- Brace/paren balance clean on all five edited files.
- The regenerated table's `ARP_STEP_SELECT/ONOFF/ACCENT/RATCHET/ENABLE` ID names
  match exactly what the ViewController references (checked against the firmware
  regen output — same generator).
- No other 2-way `Lane::Aux ? :` ternary remains that would mis-route the Arp
  lane (the bar-colour and both edit-routing sites are now 3-way).
- Line endings: sources LF (match repo); table normalised LF (see flag).

## To confirm when you build

1. **Build in the Arduino IDE / ESP toolchain** — this is the real gate. The
   `unit test asserts all N parameters map to a real section` note in
   `NavModel.cpp` refers to a test that counts params; if that test exists in
   your CI, bump its expected count (147 -> 160) and section count (17 -> 18).
2. The arp lane is indicated by **magenta** bars (no text label — same
   convention as gate/aux). If you want a text lane label, that's a small
   SeqPanel addition.
3. The arp accent cache starts at 0 and fills as you browse/edit steps (same as
   gate/aux — this repo has no per-lane patch-load push).

## Deferred (logged)

- **Bespoke tri-lane arp grid** (on/off + accent + ratchet all on the grid at
  once) — this pass uses the accent bar-grid + on/off/ratchet list rows to keep
  the working sequencer panel low-risk. The tri-lane grid should be its own
  `ArpGridPanel` component (not a SeqPanel mutation) so the sequencer stays
  isolated. The JUCE editor already has the full tri-lane `ArpPatternGrid` if you
  want the pattern shaped there meanwhile.
- Per-lane patch-load push for the arp accent cache.
- LED tint / text label for the active lane.
