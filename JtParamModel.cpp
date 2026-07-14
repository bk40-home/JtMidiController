// =============================================================================
// JtParamModel.cpp — see JtParamModel.h for the contract.
// =============================================================================
#include "JtParamModel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace JtParam {
namespace {

using JT::Params::kParams;
using JT::Params::kParamCount;

inline float clamp01(float t) {
    // NaN-safe by construction: both comparisons are false for NaN, so a NaN
    // falls through to the final `return t`... which would propagate it. Test
    // NaN explicitly instead — a single NaN reaching the value store would
    // poison every subsequent step() and clamp on that param.
    if (!(t == t)) return 0.0f;          // NaN -> 0
    if (t < 0.0f)  return 0.0f;
    if (t > 1.0f)  return 1.0f;
    return t;
}

// strcmp on short literals; avoids pulling <string> onto the ESP32.
inline bool unitIs(const char* u, const char* what) {
    return u && strcmp(u, what) == 0;
}

// Continuous params step by 1/128 of the normalised range, so a full sweep is
// ~128 detents. That deliberately matches the OLD 7-bit CC feel: the encoders
// were tuned against 0..127, and changing the detent size would change the
// muscle memory of every existing control. The VALUE is now a float — only the
// step granularity is inherited.
constexpr float kContStep = 1.0f / 128.0f;

const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Table access
// ─────────────────────────────────────────────────────────────────────────────

uint16_t ordinalOf(uint16_t paramId) {
    for (size_t i = 0; i < kParamCount; ++i) {
        if (kParams[i].id == paramId) return static_cast<uint16_t>(i);
    }
    return kNoOrdinal;
}

const ParamDesc* descAt(uint16_t ordinal) {
    return (ordinal < kParamCount) ? &kParams[ordinal] : nullptr;
}

const ParamDesc* descOf(uint16_t paramId) {
    return JT::Params::find(paramId);
}

// ─────────────────────────────────────────────────────────────────────────────
// Normalised <-> engineering
//
// These two are exact inverses. That matters more than it looks: the display
// reads back through toEng(), the wire sends t, and the firmware re-derives the
// same engineering value from the same t using the same curve. If toEng and
// toNorm ever disagreed, a value would drift every time it round-tripped
// through a patch save/load.
// ─────────────────────────────────────────────────────────────────────────────

float toEng(const ParamDesc& d, float t) {
    t = clamp01(t);

    switch (d.curve) {
        case Curve::Log:
            // min * (max/min)^t. The generator guarantees min > 0 for log
            // curves, so the division and logf are always defined.
            return d.min * powf(d.max / d.min, t);

        case Curve::Seg2: {
            // Two linear segments meeting at 'mid' when t == 0.5. This is v1's
            // envelope-slope shape (Mapping.h cc_to_curve): it gives fine
            // control around the neutral slope in the middle of the sweep.
            if (t <= 0.5f) return d.min + (d.mid - d.min) * (t * 2.0f);
            return d.mid + (d.max - d.mid) * ((t - 0.5f) * 2.0f);
        }

        case Curve::Lin:
        default:
            return d.min + (d.max - d.min) * t;
    }
}

float toNorm(const ParamDesc& d, float eng) {
    // Clamp into range first so the inverse maths cannot go outside 0..1.
    if (!(eng == eng)) return 0.0f;                  // NaN -> 0
    if (eng <= d.min)  return 0.0f;
    if (eng >= d.max)  return 1.0f;

    switch (d.curve) {
        case Curve::Log:
            // t = log(eng/min) / log(max/min)
            return logf(eng / d.min) / logf(d.max / d.min);

        case Curve::Seg2: {
            if (eng <= d.mid) {
                const float span = d.mid - d.min;
                return (span > 0.0f) ? ((eng - d.min) / span) * 0.5f : 0.0f;
            }
            const float span = d.max - d.mid;
            return (span > 0.0f) ? 0.5f + ((eng - d.mid) / span) * 0.5f : 0.5f;
        }

        case Curve::Lin:
        default: {
            const float span = d.max - d.min;
            return (span > 0.0f) ? (eng - d.min) / span : 0.0f;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Select helpers
//
// A select's engineering value IS the option index — the generator emits
// min=0, max=count-1, Curve::Lin for every select. So these are just toEng/
// toNorm with rounding, NOT a parallel bucket scheme. That is the whole reason
// the LFO-waveform bug cannot come back: there is no second count to get wrong.
// ─────────────────────────────────────────────────────────────────────────────

uint8_t normToIndex(const ParamDesc& d, float t) {
    if (d.optionCount == 0) return 0;
    // Round rather than truncate: a value that has been through a float
    // round-trip (store -> NRPN -> store) must land back on the same option.
    const float eng = toEng(d, t);
    int idx = static_cast<int>(eng + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > d.optionCount - 1) idx = d.optionCount - 1;
    return static_cast<uint8_t>(idx);
}

float indexToNorm(const ParamDesc& d, uint8_t index) {
    if (d.optionCount <= 1) return 0.0f;
    if (index > d.optionCount - 1) index = static_cast<uint8_t>(d.optionCount - 1);
    // max == optionCount-1, so this is an exact division: index N maps to a t
    // that normToIndex() returns exactly N for.
    return static_cast<float>(index) / static_cast<float>(d.optionCount - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Defaults
// ─────────────────────────────────────────────────────────────────────────────

float defaultNorm(const ParamDesc& d) {
    // 'def' is in engineering units for every type (select: option index,
    // toggle: 0/1, continuous: real units), so one conversion covers all three.
    return toNorm(d, d.def);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stepping
// ─────────────────────────────────────────────────────────────────────────────

float step(const ParamDesc& d, float t, int32_t steps) {
    if (steps == 0) return t;

    switch (d.type) {
        case Type::Select: {
            if (d.optionCount == 0) return t;
            const int n   = d.optionCount;
            int       idx = static_cast<int>(normToIndex(d, t)) + static_cast<int>(steps);
            // Wrap, matching the old SELECT encoder behaviour. Modulo on a
            // possibly-negative value needs the double-mod to land positive.
            idx = ((idx % n) + n) % n;
            return indexToNorm(d, static_cast<uint8_t>(idx));
        }

        case Type::Toggle:
            return setOn(!isOn(d, t));

        case Type::Continuous:
        default:
            return clamp01(t + static_cast<float>(steps) * kContStep);
    }
}

bool  isOn(const ParamDesc& d, float t) { (void)d; return t >= 0.5f; }
float setOn(bool on)                    { return on ? 1.0f : 0.0f; }

// ─────────────────────────────────────────────────────────────────────────────
// Display formatting
//
// Every branch below is selected by DATA from the table (type, then unit), not
// by a hand-maintained list of CC numbers. Adding a parameter to params.yaml
// gives it correct formatting for free, provided its unit is one of the ones
// handled here.
// ─────────────────────────────────────────────────────────────────────────────

void format(const ParamDesc& d, float t, char* buf, uint8_t len) {
    if (!buf || len == 0) return;
    buf[0] = '\0';

    // ---- Select: the option string, straight from the generated table -------
    if (d.type == Type::Select) {
        const uint8_t idx = normToIndex(d, t);
        const char* s = (d.options && idx < d.optionCount) ? d.options[idx] : nullptr;
        snprintf(buf, len, "%s", s ? s : "?");
        return;
    }

    // ---- Toggle ------------------------------------------------------------
    if (d.type == Type::Toggle) {
        snprintf(buf, len, "%s", isOn(d, t) ? "On" : "Off");
        return;
    }

    // ---- Continuous: dispatch on unit -------------------------------------
    const float v = toEng(d, t);
    const char* u = d.unit;

    // Note name — perf.split_note. 0..127 with 60 == C4.
    if (unitIs(u, "note")) {
        int n = static_cast<int>(v + 0.5f);
        if (n < 0)   n = 0;
        if (n > 127) n = 127;
        snprintf(buf, len, "%s%d", kNoteNames[n % 12], (n / 12) - 1);
        return;
    }

    // Frequency — switch to kHz above 1000 so cutoff reads "2.5 kHz" not
    // "2500 Hz" (the cell is ~60 px wide; the long form overflows).
    if (unitIs(u, "Hz")) {
        if (v >= 1000.0f) snprintf(buf, len, "%.1f kHz", static_cast<double>(v / 1000.0f));
        else if (v >= 10.0f) snprintf(buf, len, "%d Hz", static_cast<int>(v + 0.5f));
        else snprintf(buf, len, "%.2f Hz", static_cast<double>(v));  // LFO: 0.03 Hz
        return;
    }

    // Time — seconds above 1000 ms, same width reasoning as Hz.
    if (unitIs(u, "ms")) {
        if (v >= 1000.0f) snprintf(buf, len, "%.2f s", static_cast<double>(v / 1000.0f));
        else snprintf(buf, len, "%d ms", static_cast<int>(v + 0.5f));
        return;
    }

    if (unitIs(u, "BPM")) {
        snprintf(buf, len, "%d", static_cast<int>(v + 0.5f));
        return;
    }

    // Semitones — signed, so bend range / detune read "+12".
    if (unitIs(u, "st")) {
        snprintf(buf, len, "%+d", static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f));
        return;
    }

    // Envelope curve exponent — 2 dp; 1.00 is the linear/neutral slope.
    if (unitIs(u, "slope")) {
        snprintf(buf, len, "%.2f", static_cast<double>(v));
        return;
    }

    // "norm" — the engine owns the real scaling, so all the UI can honestly say
    // is where in the range you are. Bipolar params show a signed percentage so
    // the centre detent reads "0%" rather than "50%".
    if (unitIs(u, "norm")) {
        if (d.bipolarUi) {
            // Range is symmetric about 0 (generator-checked), so v itself is
            // already the signed quantity; scale it to +/-100%.
            const float span = (d.max > 0.0f) ? d.max : 1.0f;
            snprintf(buf, len, "%+d%%", static_cast<int>((v / span) * 100.0f));
        } else {
            snprintf(buf, len, "%d%%", static_cast<int>(clamp01(t) * 100.0f + 0.5f));
        }
        return;
    }

    // Unknown unit — a new unit was added to params.yaml without a case here.
    // Print the raw engineering value: correct, just not pretty. Add a case.
    snprintf(buf, len, "%.2f", static_cast<double>(v));
}

} // namespace JtParam
