// =============================================================================
// PatchStore.cpp — see PatchStore.h for the v3/v2 formats and their rationale.
// =============================================================================
// Uses FFat (FAT filesystem), NOT LittleFS. The board's partition scheme is
// app3M_fat9M_16MB, whose data partition is subtype FAT; LittleFS.begin() looks
// for a "spiffs" label and fails on it. FFat defaults to the "ffat" label and
// shares the same fs::FS / File base classes, so the calls below are otherwise
// identical.
// =============================================================================
#include "PatchStore.h"
#include "PatchLegacyIds.h"     // frozen ID lists for 250/252/303/304

#include <FFat.h>

static const char* kPatchDir  = "/patches";
static const char* kIndexPath = "/patches/index.txt";
static const char* kPerfPath  = "/patches/perf.txt";

// Records are moved through a small stack buffer: one FFat call per 32
// records instead of one per parameter (305 tiny writes -> 10), without a
// 2.4 KB heap or static buffer.  256 bytes of stack, control-plane only.
static constexpr size_t kRecChunk = 32;

// ─────────────────────────────────────────────────────────────────────────────

bool PatchStore::begin() {
    if (!FFat.begin(true)) {          // true = format on first use
        Serial.println("[PATCH] FFat mount failed");
        mounted_ = false;
        return false;
    }
    if (!FFat.exists(kPatchDir)) FFat.mkdir(kPatchDir);

    mounted_ = true;
    loadIndex();
    loadPerfName();

    Serial.printf("[PATCH] FFat mounted, format v%u, %u params/patch\r\n",
                  static_cast<unsigned>(kVersion),
                  static_cast<unsigned>(JT::Params::kParamCount));
    return true;
}

void PatchStore::slotPath(uint8_t slot, char* buf, size_t bufLen) {
    snprintf(buf, bufLen, "%s/%03u.bin", kPatchDir, static_cast<unsigned>(slot));
}

// ─────────────────────────────────────────────────────────────────────────────
// Save / load
// ─────────────────────────────────────────────────────────────────────────────

bool PatchStore::save(uint8_t slot, const float* values, size_t count,
                      const char* name) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;
    if (!values || count != JT::Params::kParamCount) return false;

    char path[32];
    slotPath(slot, path, sizeof path);

    File f = FFat.open(path, "w");
    if (!f) {
        Serial.printf("[PATCH] save: cannot open %s\r\n", path);
        return false;
    }

    // v3: header, then one {ParamID, 0, value} Record per param in table
    // order.  Table order is not required by the reader, but it lets load()
    // resolve every ordinal with one compare (see loadV3) on the usual case.
    const Header h = {
        kMagic, kVersion,
        static_cast<uint16_t>(JT::Params::kParamCount)
    };

    bool ok = (f.write(reinterpret_cast<const uint8_t*>(&h), sizeof h) == sizeof h);

    Record buf[kRecChunk];
    for (size_t base = 0; ok && base < count; base += kRecChunk) {
        const size_t n = (count - base < kRecChunk) ? (count - base) : kRecChunk;
        for (size_t k = 0; k < n; ++k) {
            buf[k].id       = JT::Params::kParams[base + k].id;
            buf[k].reserved = 0;
            buf[k].norm     = values[base + k];
        }
        const size_t bytes = n * sizeof(Record);
        ok = (f.write(reinterpret_cast<const uint8_t*>(buf), bytes) == bytes);
    }
    f.close();

    if (!ok) {
        Serial.printf("[PATCH] save: short write on %s\r\n", path);
        return false;
    }

    if (name) setName(slot, name);
    return true;
}

bool PatchStore::load(uint8_t slot, float* values, size_t count) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;
    if (!values || count != JT::Params::kParamCount) return false;

    char path[32];
    slotPath(slot, path, sizeof path);

    File f = FFat.open(path, "r");
    if (!f) return false;

    Header h{};
    if (f.read(reinterpret_cast<uint8_t*>(&h), sizeof h) != sizeof h) {
        f.close();
        return false;
    }

    // Refuse anything that is not exactly this format for exactly this table.
    // A v1 patch has no magic and fails here — which is the point. Loading a
    // file whose parameter count differs would shift every value onto the wrong
    // parameter, and it would look like the synth had gone mad rather than like
    // a bad file.
    if (h.magic != kMagic) {
        Serial.printf("[PATCH] slot %u: not a JT-8000 patch (v1 file?) — refused\r\n",
                      static_cast<unsigned>(slot));
        f.close();
        return false;
    }
    // v3 (current): self-describing records, any table, any order.
    if (h.version == kVersion) {
        if (h.paramCount == 0 || h.paramCount > kMaxRecords) {
            Serial.printf("[PATCH] slot %u: v3 record count %u out of range — refused\r\n",
                          static_cast<unsigned>(slot),
                          static_cast<unsigned>(h.paramCount));
            f.close();
            return false;
        }
        size_t matched = 0, unknown = 0;
        const bool ok = loadV3(f, h.paramCount, values, count, matched, unknown);
        f.close();
        if (!ok) {
            Serial.printf("[PATCH] slot %u: v3 short read — refused\r\n",
                          static_cast<unsigned>(slot));
            return false;
        }
        // Silent on the normal case (same table): log only when the file and
        // the running table disagree, which is exactly when it is worth knowing.
        if (unknown != 0 || matched != count) {
            Serial.printf("[PATCH] slot %u: v3 loaded by ParamID — %u matched, "
                          "%u unknown skipped, %u defaulted\r\n",
                          static_cast<unsigned>(slot),
                          static_cast<unsigned>(matched),
                          static_cast<unsigned>(unknown),
                          // A corrupt file may repeat an ID; never underflow.
                          static_cast<unsigned>(matched >= count ? 0 : count - matched));
        }
        return true;
    }

    // ---- v2 (read-only from here down) -------------------------------------
    // D-10: a v2 file from an OLDER table (250/252/303/304 params).  Read it in
    // ITS order and move every value to where its ParamID lives now.  The
    // count check against the running table keeps a current-count v2 file on
    // the direct path below even if its count also appears in the list.
    const JtPatchLegacy::LegacyTable* legacy =
        (h.version == kVersionV2 && h.paramCount != JT::Params::kParamCount)
            ? JtPatchLegacy::findLegacyTable(h.paramCount)
            : nullptr;
    if (legacy) {
        const bool ok = loadLegacy(f, legacy->ids, legacy->count, values, count);
        f.close();
        if (ok) {
            Serial.printf("[PATCH] slot %u: %u-param patch migrated by ParamID\r\n",
                          static_cast<unsigned>(slot),
                          static_cast<unsigned>(h.paramCount));
        } else {
            Serial.printf("[PATCH] slot %u: legacy patch short read — refused\r\n",
                          static_cast<unsigned>(slot));
        }
        return ok;
    }

    if (h.version != kVersionV2 || h.paramCount != JT::Params::kParamCount) {
        Serial.printf("[PATCH] slot %u: format v%u/%u params, expected v%u, or v%u/%u — refused\r\n",
                      static_cast<unsigned>(slot),
                      static_cast<unsigned>(h.version),
                      static_cast<unsigned>(h.paramCount),
                      static_cast<unsigned>(kVersion),
                      static_cast<unsigned>(kVersionV2),
                      static_cast<unsigned>(JT::Params::kParamCount));
        f.close();
        return false;
    }

    const size_t bytes = count * sizeof(float);
    const bool ok = (f.read(reinterpret_cast<uint8_t*>(values), bytes) == bytes);
    f.close();

    if (!ok) {
        Serial.printf("[PATCH] slot %u: short read — refused\r\n",
                      static_cast<unsigned>(slot));
        return false;
    }

    // The caller (JtParam::Store::loadAll) clamps and NaN-guards each value, so
    // a corrupt file cannot poison the store.
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// v3 reader: {ParamID, reserved, value} records -> current table, by ParamID
// ─────────────────────────────────────────────────────────────────────────────
bool PatchStore::loadV3(fs::File& f, uint16_t recordCount, float* values, size_t count,
                        size_t& matched, size_t& unknown) {
    // Defaults first: any param the file does not mention (added to the table
    // after the patch was saved) keeps its default — the sound the file had.
    for (size_t i = 0; i < count; ++i) {
        values[i] = JtParam::defaultNorm(JT::Params::kParams[i]);
    }

    matched = 0;
    unknown = 0;
    Record buf[kRecChunk];
    for (size_t base = 0; base < recordCount; base += kRecChunk) {
        const size_t n = (recordCount - base < kRecChunk) ? (recordCount - base) : kRecChunk;
        const size_t bytes = n * sizeof(Record);
        if (f.read(reinterpret_cast<uint8_t*>(buf), bytes) != bytes) return false;

        for (size_t k = 0; k < n; ++k) {
            const size_t   idx = base + k;      // position in the FILE
            const uint16_t id  = buf[k].id;
            // Fast path: save() writes in table order, so with an unchanged
            // table record idx IS ordinal idx — one compare, no search.
            // Only after a table change does it fall back to ordinalOf()'s
            // linear scan (O(n), still well under 1 ms for a whole patch).
            const uint16_t ord = (idx < count && JT::Params::kParams[idx].id == id)
                               ? static_cast<uint16_t>(idx)
                               : JtParam::ordinalOf(id);
            if (ord < count) {
                values[ord] = buf[k].norm;      // loadAll() clamps / NaN-guards later
                ++matched;
            } else {
                ++unknown;                      // newer-table or retired ID
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// D-10 migration: older-table v2 file -> current table, by ParamID
// ─────────────────────────────────────────────────────────────────────────────
bool PatchStore::loadLegacy(fs::File& f, const uint16_t* oldIds, size_t oldCount,
                            float* values, size_t count) {
    // Start from the table defaults: anything the old table did not have
    // (bass sequencer, voice.legato, mix.xmod_type, master.usb_return_level)
    // takes its default, which is the behaviour that file was saved with.
    // NOTE: values[] is overwritten even if the read later fails; load()'s
    // "untouched on failure" promise is relaxed for legacy files exactly as
    // it was for the 252 reader (the caller only applies it on success).
    for (size_t i = 0; i < count; ++i) {
        values[i] = JtParam::defaultNorm(JT::Params::kParams[i]);
    }

    // Stream the old values one at a time — no second 1 KB buffer — and drop
    // each onto its ParamID's CURRENT ordinal.  An id that no longer exists
    // (retired) is simply skipped.
    for (size_t i = 0; i < oldCount; ++i) {
        float v = 0.0f;
        if (f.read(reinterpret_cast<uint8_t*>(&v), sizeof v) != sizeof v) return false;
        const uint16_t ord = JtParam::ordinalOf(oldIds[i]);
        if (ord < count) values[ord] = v;   // loadAll() clamps / NaN-guards later
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots / names
// ─────────────────────────────────────────────────────────────────────────────

bool PatchStore::exists(uint8_t slot) const {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;
    char path[32];
    slotPath(slot, path, sizeof path);
    return FFat.exists(path);
}

const char* PatchStore::getName(uint8_t slot) const {
    if (slot >= Config::MAX_PATCHES) return nullptr;
    return (names_[slot][0] != '\0') ? names_[slot] : nullptr;
}

bool PatchStore::setName(uint8_t slot, const char* name) {
    if (!mounted_ || slot >= Config::MAX_PATCHES || !name) return false;
    strncpy(names_[slot], name, 16);
    names_[slot][16] = '\0';
    saveIndex();
    return true;
}

bool PatchStore::remove(uint8_t slot) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;
    char path[32];
    slotPath(slot, path, sizeof path);
    if (FFat.exists(path)) FFat.remove(path);
    names_[slot][0] = '\0';
    saveIndex();
    return true;
}

uint8_t PatchStore::count() const {
    if (!mounted_) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < Config::MAX_PATCHES; ++i) if (exists(i)) ++n;
    return n;
}

bool PatchStore::setPerfName(const char* name) {
    if (!mounted_ || !name) return false;
    strncpy(perfName_, name, 16);
    perfName_[16] = '\0';
    savePerfName();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Index files
// ─────────────────────────────────────────────────────────────────────────────

void PatchStore::loadIndex() {
    File f = FFat.open(kIndexPath, "r");
    if (!f) return;
    for (uint8_t i = 0; i < Config::MAX_PATCHES && f.available(); ++i) {
        String line = f.readStringUntil('\n');
        line.trim();
        strncpy(names_[i], line.c_str(), 16);
        names_[i][16] = '\0';
    }
    f.close();
}

void PatchStore::saveIndex() {
    File f = FFat.open(kIndexPath, "w");
    if (!f) return;
    for (uint8_t i = 0; i < Config::MAX_PATCHES; ++i) {
        f.println(names_[i]);
    }
    f.close();
}

void PatchStore::loadPerfName() {
    File f = FFat.open(kPerfPath, "r");
    if (!f) return;
    String line = f.readStringUntil('\n');
    line.trim();
    strncpy(perfName_, line.c_str(), 16);
    perfName_[16] = '\0';
    f.close();
}

void PatchStore::savePerfName() {
    File f = FFat.open(kPerfPath, "w");
    if (!f) return;
    f.println(perfName_);
    f.close();
}
