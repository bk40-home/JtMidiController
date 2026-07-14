// =============================================================================
// PatchStore.h — patch storage on ESP32 flash via FFat (Phase D, format v2)
// =============================================================================
// FORMAT CHANGE — v1 PATCHES WILL NOT LOAD
//   v1 stored a raw CC_STATE_SIZE byte dump, indexed by 7-bit CC number. That
//   is dead: only 19 of the 140 parameters ever had a CC binding, so 121 of
//   them were never in a saved patch at all.
//
//   v2 stores kParamCount normalised floats, ordered by TABLE ORDINAL, behind a
//   header carrying a magic, a version and the param count. A file whose count
//   does not match the running table is REFUSED rather than loaded — a silently
//   shifted patch is worse than no patch.
//
//   Accepted by Kris: only test patches existed; they will be re-initialised.
//
// WHY NORMALISED FLOATS AND NOT ENGINEERING UNITS
//   Because the curve lives in params.yaml. Storing Hz would freeze today's
//   curve into every saved file, and changing a range in the yaml would then
//   silently move every stored patch. Storing t (0..1) means a patch keeps its
//   MEANING — "75% of the way up the cutoff sweep" — whatever the curve does
//   later.
//
// FILES
//   /patches/NNN.bin   header + kParamCount floats
//   /patches/index.txt one name per line, up to MAX_PATCHES
//   /patches/perf.txt  the single performance name
// =============================================================================
#pragma once

#include <Arduino.h>

#include "Config.h"
#include "JtParamModel.h"

class PatchStore {
public:
    // Bumped from v1. A v1 file has no magic at all, so it fails the header
    // check and is refused — it cannot be misread as a short v2 file.
    static constexpr uint32_t kMagic   = 0x4A543830;   // "JT80"
    static constexpr uint16_t kVersion = 2;

    struct Header {
        uint32_t magic;
        uint16_t version;
        uint16_t paramCount;   // must equal JT::Params::kParamCount
    };

    PatchStore() = default;

    bool begin();

    // Save the whole store. `values` must be kParamCount normalised floats —
    // JtParam::Store::raw() gives exactly that.
    bool save(uint8_t slot, const float* values, size_t count,
              const char* name = nullptr);

    // Load into `values`. Returns false — and leaves `values` UNTOUCHED — if
    // the file is missing, has a bad header, or was written against a different
    // param count.
    bool load(uint8_t slot, float* values, size_t count);

    bool        exists(uint8_t slot) const;
    const char* getName(uint8_t slot) const;
    bool        setName(uint8_t slot, const char* name);
    bool        remove(uint8_t slot);
    uint8_t     count() const;

    // Single performance name, ESP32-local (the synth's performance state lives
    // on the Teensy; this is the label NameEditor edits).
    const char* getPerfName() const { return perfName_; }
    bool        setPerfName(const char* name);

private:
    bool mounted_ = false;
    char names_[Config::MAX_PATCHES][17] = {};
    char perfName_[17] = {};

    static void slotPath(uint8_t slot, char* buf, size_t bufLen);

    void loadIndex();
    void saveIndex();
    void loadPerfName();
    void savePerfName();
};
