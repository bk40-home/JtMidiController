// =============================================================================
// PatchStore.h — patch storage on ESP32 flash via FFat (format v3, reads v2)
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
// FORMAT v3 (2026-10-22) — SELF-DESCRIBING, WRITTEN BY save()
//   Same 8-byte header (magic, version = 3, recordCount), then one 8-byte
//   Record per parameter: { ParamID, reserved (0), normalised value }.
//   Because every value carries its own ParamID, a table change (params
//   added, removed or reordered) no longer breaks saved patches and no longer
//   needs a frozen ID list: load() starts from the table defaults and drops
//   each record onto wherever its ParamID lives now.  Unknown IDs (from a
//   NEWER table, or retired) are skipped and counted in the log.
//   Cost: 8 bytes/param instead of 4 (305 params ~ 2.4 KB/file, ~312 KB for
//   all 128 slots on a 9 MB FAT partition) — negligible.
//   Downgrade note: firmware older than this refuses a v3 file with a
//   "format v3/... refused" log line.  The file is not damaged; it loads
//   again once current firmware is back.
//
//   v2 files (below) are still READ, never written.  A v2 patch becomes v3
//   the next time it is saved.
//
// TABLE GROWTH FOR v2 FILES (ledger D-10, 2026-10-17; widened 2026-10-22)
//   v2 stored values by ORDINAL only, so older v2 tables need a frozen list.
//   Readable: the current count (direct) and a 250-, 252-, 303- or 304-parameter file
//   is loaded BY ParamID through the frozen lists in PatchLegacyIds.h (252 via
//   PatchLegacy252.h).  Parameters the old table did not have keep their
//   defaults.  The file is only rewritten (in the current format) when it is
//   saved again.  Files from any other count are still refused.
//   These lists are now FROZEN FOR GOOD: v3 files never need a new one.
//
// WHY NORMALISED FLOATS AND NOT ENGINEERING UNITS
//   Because the curve lives in params.yaml. Storing Hz would freeze today's
//   curve into every saved file, and changing a range in the yaml would then
//   silently move every stored patch. Storing t (0..1) means a patch keeps its
//   MEANING — "75% of the way up the cutoff sweep" — whatever the curve does
//   later.
//
// FILES
//   /patches/NNN.bin   header + recordCount Records (v3); v2: header + floats
//   /patches/index.txt one name per line, up to MAX_PATCHES
//   /patches/perf.txt  the single performance name
// =============================================================================
#pragma once

#include <Arduino.h>
#include <FS.h>          // fs::File, for the legacy-patch reader below

#include "Config.h"
#include "JtParamModel.h"

class PatchStore {
public:
    // A v1 file has no magic at all, so it fails the header check and is
    // refused — it cannot be misread as a short v2/v3 file.
    static constexpr uint32_t kMagic     = 0x4A543830;   // "JT80"
    static constexpr uint16_t kVersion   = 3;            // written by save()
    static constexpr uint16_t kVersionV2 = 2;            // read-only (older files)

    // Corruption guard for v3: a header claiming more records than this is
    // refused before any read.  ParamIDs are 14-bit (NRPN), so no real table
    // can exceed 16384; 4096 is ~13x today's table and caps a bad file's read
    // at 32 KB.
    static constexpr uint16_t kMaxRecords = 4096;

    struct Header {
        uint32_t magic;
        uint16_t version;
        uint16_t paramCount;   // v3: number of Records; v2: number of floats
    };
    static_assert(sizeof(Header) == 8, "on-flash header layout is 8 bytes");

    // v3 body element.  Natural alignment, no packing pragma needed: 2+2+4.
    struct Record {
        uint16_t id;           // ParamID (params.yaml), NOT an ordinal
        uint16_t reserved;     // written 0; ignored on read (room for flags)
        float    norm;         // normalised 0..1, clamped by loadAll() later
    };
    static_assert(sizeof(Record) == 8, "on-flash record layout is 8 bytes");

    PatchStore() = default;

    bool begin();

    // Save the whole store. `values` must be kParamCount normalised floats —
    // JtParam::Store::raw() gives exactly that.
    bool save(uint8_t slot, const float* values, size_t count,
              const char* name = nullptr);

    // Load into `values` (kParamCount floats, table order).  Returns false if
    // the file is missing, has a bad header, is a v2 file of an unknown count,
    // or is truncated.  `values` is untouched on a header failure; on a short
    // read it may be partly written, so callers must only apply it on true
    // (PatchManager loads into a scratch buffer and does exactly that).
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
    // D-10: read an older file body (header already consumed) into the
    // current table by ParamID.  `oldIds[i]` is the ParamID at old ordinal i,
    // `oldCount` entries long.  See PatchLegacyIds.h.
    bool loadLegacy(fs::File& f, const uint16_t* oldIds, size_t oldCount,
                    float* values, size_t count);

    // v3: read `recordCount` Records (header already consumed) into the
    // current table by ParamID.  Reports how many records matched a current
    // param and how many were skipped as unknown, for the load log.
    bool loadV3(fs::File& f, uint16_t recordCount, float* values, size_t count,
                size_t& matched, size_t& unknown);

    bool mounted_ = false;
    char names_[Config::MAX_PATCHES][17] = {};
    char perfName_[17] = {};

    static void slotPath(uint8_t slot, char* buf, size_t bufLen);

    void loadIndex();
    void saveIndex();
    void loadPerfName();
    void savePerfName();
};
