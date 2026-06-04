# JT-8000 Controller — Complete Parameter Map

## Proposed Page Structure

```
Button 0: HOME     (overview — quick access to key params)
Button 1: OSC      (sub-pages: OSC1-Main, OSC1-Adv, OSC2-Main, OSC2-Adv)
Button 2: MIX      (7 params — single page)
Button 3: FLT      (11 params — may need sub-pages)
Button 4: ENV      (sub-pages: AMP, FILT, PITCH — curve viz)
Button 5: LFO      (sub-pages: LFO1, LFO2)
Button 6: FX       (sub-pages: Effects, Reverb)
Button 7: SEQ      (step sequencer — bar viz)
Button 8: PERF     (voice/glide/clock — single page)
```

---

## SELECT Parameters — Text Values (bucket-mapped, NOT 0–127)

| CC# | Name           | Options (count) | Text Values |
|-----|----------------|-----------------|-------------|
|  21 | OSC1 Wave      | 14 | SIN, SAW, SQR, TRI, ARB, PLS, rSAW, S&H, vTRI, BLS, rBLS, BLSQ, BLP, SSAW |
|  22 | OSC2 Wave      | 14 | SIN, SAW, SQR, TRI, ARB, PLS, rSAW, S&H, vTRI, BLS, rBLS, BLSQ, BLP, SSAW |
|  41 | OSC1 Pitch     |  5 | -24, -12, 0, +12, +24 |
|  42 | OSC2 Pitch     |  5 | -24, -12, 0, +12, +24 |
| 113 | Filter Engine  |  2 | OBXa, VA |
| 112 | Filter Mode    |  6 | 4-Pole, 2-Pole, 2P BP, 2P Push, Xpander, Xpander M |
| 115 | VA Filter Type | 13 | SVF LP2, SVF HP2, SVF BP2, SVF NOTCH, SVF AP, Moog LP4, Moog LP2, Moog BP2, Diode LP4, Korg35 LP, Korg35 HP, TPT1 LP, TPT1 HP |
| 114 | Xpander Mode   | 15 | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 |
|  62 | LFO1 Wave      |  6 | SIN, TRI, SAW, SQR, S&H, NOISE |
|  63 | LFO2 Wave      |  6 | SIN, TRI, SAW, SQR, S&H, NOISE |
|  56 | LFO1 Dest      |  5 | None, Pitch, Filter, PWM, Amp |
|  53 | LFO2 Dest      |  5 | None, Pitch, Filter, PWM, Amp |
| 120 | LFO1 Sync      | 12 | Free, 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T |
| 121 | LFO2 Sync      | 12 | Free, 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T |
|  14 | Poly Mode      |  3 | Poly, Mono, Unison |
| 103 | Mod FX Type    | 12 | OFF, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 |
| 107 | Delay FX Type  |  6 | OFF, 0, 1, 2, 3, 4 |
|  16 | Drive          |  3 | OFF, Soft, Hard |
|   6 | Seq Direction  |  4 | Fwd, Rev, Bounce, Random |
|   9 | Seq Dest       |  5 | None, Pitch, Filter, PWM, Amp |
| 116 | Seq Sync       | 12 | Free, 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T |
| 122 | Delay Sync     | 12 | Free, 4 Bars, 2 Bars, 1 Bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T |
| 140 | Perf Mode      |  3 | Single, Layer, Split |
| 144 | Edit Target    |  3 | A, B, Both |
| 118 | Clock Source   |  2 | Internal, Ext MIDI |

## TOGGLE Parameters — Show ON/OFF text

| CC# | Name           | OFF (0) | ON (127) |
|-----|----------------|---------|----------|
|  20 | Osc Sync       | OFF     | ON       |
|  81 | Glide Enable   | OFF     | ON       |
|  94 | Reverb Bypass  | OFF     | ON       |
|  96 | Reverb Freeze  | OFF     | ON       |
|   2 | Seq Enable     | OFF     | ON       |
|  13 | Seq Retrigger  | OFF     | ON       |

---

## Page-by-Page Parameter Layout

### OSC1 — Main (sub-page 0)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | WAVE       |  21 | SELECT    | SIN/SAW/SQR/etc  |
| Pot1 | PITCH      |  41 | SELECT    | -24/-12/0/+12/+24|
| Pot2 | FINE       |  45 | BIPOLAR   | -63..+63 bar     |
| Pot3 | DETUNE     |  43 | BIPOLAR   | -63..+63 bar     |
| Pot4 | SS DET     |  77 | CONT      | 0–127 bar        |
| Pot5 | SS MIX     |  78 | CONT      | 0–127 bar        |
| Pot6 | FB AMT     | 123 | CONT      | 0–127 bar        |
| Pot7 | FB MIX     | 125 | CONT      | 0–127 bar        |

### OSC1 — Advanced (sub-page 1)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | FREQ DC    |  86 | BIPOLAR   | -63..+63 bar     |
| Pot1 | SHAPE DC   |  87 | BIPOLAR   | -63..+63 bar     |
| Pot2 | RING MIX   |  91 | CONT      | 0–127 bar        |
| Pot3 | ARB BANK   | 101 | CONT      | 0–127 bar        |
| Pot4 | ARB IDX    |  83 | CONT      | 0–127 bar        |
| Pot5 | —          |  —  | NONE      |                  |
| Pot6 | —          |  —  | NONE      |                  |
| Pot7 | —          |  —  | NONE      |                  |

### OSC2 — Main (sub-page 2)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | WAVE       |  22 | SELECT    | SIN/SAW/SQR/etc  |
| Pot1 | PITCH      |  42 | SELECT    | -24/-12/0/+12/+24|
| Pot2 | FINE       |  46 | BIPOLAR   | -63..+63 bar     |
| Pot3 | DETUNE     |  44 | BIPOLAR   | -63..+63 bar     |
| Pot4 | SS DET     |  79 | CONT      | 0–127 bar        |
| Pot5 | SS MIX     |  80 | CONT      | 0–127 bar        |
| Pot6 | FB AMT     | 124 | CONT      | 0–127 bar        |
| Pot7 | FB MIX     | 126 | CONT      | 0–127 bar        |

### OSC2 — Advanced (sub-page 3)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | FREQ DC    |  88 | BIPOLAR   | -63..+63 bar     |
| Pot1 | SHAPE DC   |  89 | BIPOLAR   | -63..+63 bar     |
| Pot2 | RING MIX   |  92 | CONT      | 0–127 bar        |
| Pot3 | ARB BANK   | 102 | CONT      | 0–127 bar        |
| Pot4 | ARB IDX    |  85 | CONT      | 0–127 bar        |
| Pot5 | —          |  —  | NONE      |                  |
| Pot6 | —          |  —  | NONE      |                  |
| Pot7 | —          |  —  | NONE      |                  |

### MIX (single page)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | OSC1 MIX   |  60 | CONT      | 0–127 bar        |
| Pot1 | OSC2 MIX   |  61 | CONT      | 0–127 bar        |
| Pot2 | SUB MIX    |  58 | CONT      | 0–127 bar        |
| Pot3 | NOISE MIX  |  59 | CONT      | 0–127 bar        |
| Pot4 | OSC BAL    |  47 | BIPOLAR   | -63..+63 bar     |
| Pot5 | XMOD       |  19 | CONT      | 0–127 bar        |
| Pot6 | SYNC       |  20 | TOGGLE    | ON/OFF text      |
| Pot7 | —          |  —  | NONE      |                  |

### FLT — Core (sub-page 0)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | CUTOFF     |  23 | CONT      | 0–127 bar        |
| Pot1 | RESO       |  24 | CONT      | 0–127 bar        |
| Pot2 | ENV AMT    |  48 | BIPOLAR   | -63..+63 bar     |
| Pot3 | KEY TRK    |  50 | BIPOLAR   | -63..+63 bar     |
| Pot4 | ENGINE     | 113 | SELECT    | OBXa/VA          |
| Pot5 | MODE       | 112 | SELECT    | 4-Pole/2-Pole/etc|
| Pot6 | VA TYPE    | 115 | SELECT    | SVF LP2/etc      |
| Pot7 | OCT CTRL   |  84 | CONT      | 0–127 bar        |

### FLT — Multimode (sub-page 1)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | XP MODE    | 114 | SELECT    | 0–14             |
| Pot1 | RES MOD    | 117 | CONT      | 0–127 bar        |
| Pot2 | MULTI      | 111 | CONT      | 0–127 bar        |
| Pot3 | —          |  —  | NONE      |                  |
| Pot4 | —          |  —  | NONE      |                  |
| Pot5 | —          |  —  | NONE      |                  |
| Pot6 | —          |  —  | NONE      |                  |
| Pot7 | —          |  —  | NONE      |                  |

### ENV — AMP (sub-page 0) [curve visualization]
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | ATTACK     |  25 | ENV       | curve segment    |
| Pot1 | DECAY      |  26 | ENV       | curve segment    |
| Pot2 | SUSTAIN    |  27 | ENV       | curve segment    |
| Pot3 | RELEASE    |  28 | ENV       | curve segment    |
| Pot4 | ATK CRV    | 147 | CONT      | curve shape      |
| Pot5 | DEC CRV    | 148 | CONT      | curve shape      |
| Pot6 | REL CRV    | 149 | CONT      | curve shape      |
| Pot7 | —          |  —  | NONE      |                  |

### ENV — FILTER (sub-page 1) [curve visualization]
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | ATTACK     |  29 | ENV       | curve segment    |
| Pot1 | DECAY      |  30 | ENV       | curve segment    |
| Pot2 | SUSTAIN    |  31 | ENV       | curve segment    |
| Pot3 | RELEASE    |  32 | ENV       | curve segment    |
| Pot4 | ATK CRV    | 150 | CONT      | curve shape      |
| Pot5 | DEC CRV    | 151 | CONT      | curve shape      |
| Pot6 | REL CRV    | 152 | CONT      | curve shape      |
| Pot7 | —          |  —  | NONE      |                  |

### ENV — PITCH (sub-page 2) [curve visualization]
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | ATTACK     |  65 | ENV       | curve segment    |
| Pot1 | DECAY      |  66 | ENV       | curve segment    |
| Pot2 | SUSTAIN    |  67 | ENV       | curve segment    |
| Pot3 | RELEASE    |  68 | ENV       | curve segment    |
| Pot4 | DEPTH      |  69 | BIPOLAR   | -63..+63 bar     |
| Pot5 | ATK CRV    | 153 | CONT      | curve shape      |
| Pot6 | DEC CRV    | 154 | CONT      | curve shape      |
| Pot7 | REL CRV    | 155 | CONT      | curve shape      |

### LFO1 (sub-page 0)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | WAVE       |  62 | SELECT    | SIN/TRI/SAW/etc  |
| Pot1 | RATE       |  54 | CONT      | 0–127 bar        |
| Pot2 | DEPTH      |  55 | CONT      | 0–127 bar        |
| Pot3 | DELAY      |  37 | CONT      | 0–127 bar        |
| Pot4 | PITCH DEP  |  33 | CONT      | 0–127 bar        |
| Pot5 | FILT DEP   |  34 | CONT      | 0–127 bar        |
| Pot6 | PWM DEP    |  35 | CONT      | 0–127 bar        |
| Pot7 | AMP DEP    |  36 | CONT      | 0–127 bar        |
| Enc  | DEST       |  56 | SELECT    | None/Pitch/etc   |
| Enc  | SYNC       | 120 | SELECT    | Free/1/4/etc     |

### LFO2 (sub-page 1)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | WAVE       |  63 | SELECT    | SIN/TRI/SAW/etc  |
| Pot1 | RATE       |  51 | CONT      | 0–127 bar        |
| Pot2 | DEPTH      |  52 | CONT      | 0–127 bar        |
| Pot3 | DELAY      |  64 | CONT      | 0–127 bar        |
| Pot4 | PITCH DEP  |  38 | CONT      | 0–127 bar        |
| Pot5 | FILT DEP   |  39 | CONT      | 0–127 bar        |
| Pot6 | PWM DEP    |  40 | CONT      | 0–127 bar        |
| Pot7 | AMP DEP    |  57 | CONT      | 0–127 bar        |
| Enc  | DEST       |  53 | SELECT    | None/Pitch/etc   |
| Enc  | SYNC       | 121 | SELECT    | Free/1/4/etc     |

### FX — Effects (sub-page 0)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | MOD TYPE   | 103 | SELECT    | OFF/0/1/../10    |
| Pot1 | MOD MIX    | 104 | CONT      | 0–127 bar        |
| Pot2 | MOD RATE   | 105 | CONT      | 0–127 bar        |
| Pot3 | MOD FB     | 106 | CONT      | 0–127 bar        |
| Pot4 | DLY TYPE   | 107 | SELECT    | OFF/0/1/2/3/4    |
| Pot5 | DLY TIME   | 110 | CONT      | 0–127 bar        |
| Pot6 | DLY MIX    | 108 | CONT      | 0–127 bar        |
| Pot7 | DLY FB     | 109 | CONT      | 0–127 bar        |
| Enc  | DRIVE      |  16 | SELECT    | OFF/Soft/Hard    |
| Enc  | DLY SYNC   | 122 | SELECT    | Free/1/4/etc     |
| Enc  | BASS       |  99 | BIPOLAR   | -63..+63         |
| Enc  | TREBLE     | 100 | BIPOLAR   | -63..+63         |
| Enc  | DRY MIX    |  74 | CONT      | 0–127            |
| Enc  | JPFX MIX   |  76 | CONT      | 0–127            |

### FX — Reverb (sub-page 1)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | SIZE       |  70 | CONT      | 0–127 bar        |
| Pot1 | HI DAMP    |  71 | CONT      | 0–127 bar        |
| Pot2 | LO DAMP    |  93 | CONT      | 0–127 bar        |
| Pot3 | MIX        |  75 | CONT      | 0–127 bar        |
| Pot4 | SHIMMER    |  95 | CONT      | 0–127 bar        |
| Pot5 | LO PASS    |  97 | CONT      | 0–127 bar        |
| Pot6 | HI PASS    |  98 | CONT      | 0–127 bar        |
| Pot7 | —          |  —  | NONE      |                  |
| Enc  | BYPASS     |  94 | TOGGLE    | ON/OFF           |
| Enc  | FREEZE     |  96 | TOGGLE    | ON/OFF           |

### SEQ (step sequencer — bar visualization for steps)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0–7 | Steps 1–8 | 17/18 | STEP_VAL | Bar height     |
| (Scene B: Steps 9–16)                                   |
| Enc  | ENABLE     |   2 | TOGGLE    | ON/OFF           |
| Enc  | STEPS      |   3 | CONT      | 0–127            |
| Enc  | GATE       |   4 | CONT      | 0–127            |
| Enc  | SLIDE      |   5 | CONT      | 0–127            |
| Enc  | DIR        |   6 | SELECT    | Fwd/Rev/etc      |
| Enc  | DEST       |   9 | SELECT    | None/Pitch/etc   |
| Enc  | DEPTH      |   8 | BIPOLAR   | -63..+63         |
| Enc  | RETRIG     |  13 | TOGGLE    | ON/OFF           |
| Enc  | RATE       |   7 | CONT      | 0–127            |
| Enc  | SYNC       | 116 | SELECT    | Free/1/4/etc     |

### PERF (performance + voice + clock)
| Slot | Label      | CC# | Type      | Display          |
|------|------------|-----|-----------|------------------|
| Pot0 | GLIDE ON   |  81 | TOGGLE    | ON/OFF           |
| Pot1 | GLIDE TIME |  82 | CONT      | 0–127 bar        |
| Pot2 | POLY MODE  |  14 | SELECT    | Poly/Mono/Unison |
| Pot3 | UNI DET    |  15 | CONT      | 0–127 bar        |
| Pot4 | BEND RANGE | 127 | CONT      | 0–127 bar        |
| Pot5 | AMP LVL    |  90 | CONT      | 0–127 bar        |
| Pot6 | BPM        | 119 | CONT      | 0–127 bar        |
| Pot7 | CLK SRC    | 118 | SELECT    | Internal/Ext MIDI|
| Enc  | PERF MODE  | 140 | SELECT    | Single/Layer/Split|
| Enc  | EDIT TGT   | 144 | SELECT    | A/B/Both         |
| Enc  | VEL AMP    |  10 | CONT      | 0–127            |
| Enc  | VEL FILT   |  11 | CONT      | 0–127            |
| Enc  | VEL ENV    |  12 | CONT      | 0–127            |

---

## Coverage Check — All Parameters Accounted For

### ✅ On a page (123 params):
- OSC1: 13 (WAVE, PITCH, FINE, DETUNE, SS_DET, SS_MIX, FB_AMT, FB_MIX, FREQ_DC, SHAPE_DC, RING_MIX, ARB_BANK, ARB_IDX)
- OSC2: 13 (same structure)
- MIX: 7 (OSC1/2_MIX, SUB, NOISE, BAL, XMOD, SYNC)
- FLT: 11 (CUTOFF, RESO, ENV, KEY, ENGINE, MODE, VA, OCT, RES_MOD, MULTI, XPANDER)
- ENV AMP: 7 (ADSR + 3 curves)
- ENV FLT: 7 (ADSR + 3 curves)
- ENV PITCH: 8 (ADSR + DEPTH + 3 curves)
- LFO1: 10 (WAVE, RATE, DEPTH, DELAY, 4× per-dest, DEST, SYNC)
- LFO2: 10 (same)
- FX Effects: 14 (MOD type/mix/rate/fb, DLY type/time/mix/fb, DRIVE, DLY_SYNC, BASS, TREBLE, DRY, JPFX)
- FX Reverb: 9 (SIZE, HI/LO DAMP, MIX, SHIMMER, LO/HI PASS, BYPASS, FREEZE)
- SEQ: 12 (ENABLE, STEPS, GATE, SLIDE, DIR, DEST, DEPTH, RETRIG, RATE, SYNC, STEP_SEL, STEP_VAL)
- PERF: 11 (GLIDE_ON, GLIDE_TIME, POLY_MODE, UNI_DET, BEND, AMP_LVL, BPM, CLK_SRC, PERF_MODE, EDIT_TGT, VEL×3)

### ❌ NOT on any page (internal/advanced — deferred):
- PERF_VOICE_SPLIT (141), PERF_SPLIT_NOTE (142), PERF_BALANCE (143)
- PERF_MIDI_CHANNEL_A (145), PERF_MIDI_CHANNEL_B (146)

These 5 are INTERNAL CCs (>127) used by the dual-layer system.
They can be added to a PERF sub-page later when UART is wired.

---

## Key Implementation Changes Required

1. **Text for SELECT**: `drawParamCell` looks up `ParamDefs::findByCC(cc)` →
   if `optionCount > 0`, convert CC value → bucket index → options[index]
   Bucket: `index = cc_value * optionCount / 128`

2. **Text for TOGGLE**: already shows ON/OFF (confirmed working)

3. **OSC1/OSC2 sub-pages**: add 4 sub-pages to OSC page in PageMappings.h
   (same pattern as ENV's 3 sub-pages)

4. **FLT sub-pages**: split into Core (8 params) + Multimode (3 params)

5. **PERF expanded**: move VEL, BPM, CLK to PERF page (currently separate)
