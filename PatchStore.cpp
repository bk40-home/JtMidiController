// =============================================================================
// PatchStore.cpp — see PatchStore.h for the v2 format and its rationale.
// =============================================================================
// Uses FFat (FAT filesystem), NOT LittleFS. The board's partition scheme is
// app3M_fat9M_16MB, whose data partition is subtype FAT; LittleFS.begin() looks
// for a "spiffs" label and fails on it. FFat defaults to the "ffat" label and
// shares the same fs::FS / File base classes, so the calls below are otherwise
// identical.
// =============================================================================
#include "PatchStore.h"

#include <FFat.h>

static const char* kPatchDir  = "/patches";
static const char* kIndexPath = "/patches/index.txt";
static const char* kPerfPath  = "/patches/perf.txt";

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

    const Header h = {
        kMagic, kVersion,
        static_cast<uint16_t>(JT::Params::kParamCount)
    };

    bool ok = (f.write(reinterpret_cast<const uint8_t*>(&h), sizeof h) == sizeof h);
    if (ok) {
        const size_t bytes = count * sizeof(float);
        ok = (f.write(reinterpret_cast<const uint8_t*>(values), bytes) == bytes);
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
    if (h.version != kVersion || h.paramCount != JT::Params::kParamCount) {
        Serial.printf("[PATCH] slot %u: format v%u/%u params, expected v%u/%u — refused\r\n",
                      static_cast<unsigned>(slot),
                      static_cast<unsigned>(h.version),
                      static_cast<unsigned>(h.paramCount),
                      static_cast<unsigned>(kVersion),
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
