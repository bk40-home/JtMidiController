// =============================================================================
// PatchStore.cpp
// =============================================================================
// Uses FFat (FAT filesystem), not LittleFS. The board's recommended partition
// scheme ("16M Flash (3MB APP/9.9MB FATFS)" / app3M_fat9M_16MB) builds its
// data partition with subtype "fat" (visible in the boot diagnostic as
// `ffat : ... type: DATA, subtype: FAT`), not "spiffs". LittleFS.begin()
// specifically searches for a spiffs-subtype partition and fails with
// "Error: 261" (partition not found) on this board — confirmed by a verbose
// esp_littlefs trace showing zero esp_littlefs: lines, meaning the failure
// happens before that driver is ever reached.
//
// FFat is the Arduino-ESP32 core's own filesystem library for fat-subtype
// partitions (esp_partition_find_first(..., ESP_PARTITION_SUBTYPE_DATA_FAT,
// ...) — see FFat.cpp) and defaults to the exact partition label ("ffat")
// this board's scheme already provides, so no partition table change is
// needed. FFat shares the same fs::FS / File base classes as LittleFS, so
// every call below (open/exists/mkdir/remove, File::write/read/close/
// available/readStringUntil/println) is identical to before — only the
// filesystem object name changed.
#include "PatchStore.h"
#include <FFat.h>

static const char* kPatchDir  = "/patches";
static const char* kIndexFile = "/patches/index.txt";
static const char* kPerfFile  = "/patches/perf_name.txt";

bool PatchStore::begin() {
    if (!FFat.begin(true)) {  // true = format on first use
        Serial.println("[PATCH] FFat mount failed");
        return false;
    }

    // Create /patches directory if it doesn't exist
    if (!FFat.exists(kPatchDir)) {
        FFat.mkdir(kPatchDir);
    }

    // Initialise names to empty
    memset(names_, 0, sizeof(names_));
    loadIndex();
    loadPerfName();

    mounted_ = true;
    Serial.printf("[PATCH] Store ready, %u patches found\n", count());
    return true;
}

bool PatchStore::save(uint8_t slot, const uint8_t* ccState, const char* name) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;

    char path[32];
    slotPath(slot, path, sizeof(path));

    File f = FFat.open(path, "w");
    if (!f) {
        Serial.printf("[PATCH] Failed to open %s for writing\n", path);
        return false;
    }

    const size_t written = f.write(ccState, Config::CC_STATE_SIZE);
    f.close();

    if (written != Config::CC_STATE_SIZE) {
        Serial.printf("[PATCH] Write error: %u/%u bytes\n",
                      written, Config::CC_STATE_SIZE);
        return false;
    }

    // Update name if provided
    if (name) {
        strncpy(names_[slot], name, 16);
        names_[slot][16] = '\0';
        saveIndex();
    }

    Serial.printf("[PATCH] Saved slot %u (%s)\n", slot,
                  names_[slot][0] ? names_[slot] : "unnamed");
    return true;
}

bool PatchStore::load(uint8_t slot, uint8_t* ccState) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;

    char path[32];
    slotPath(slot, path, sizeof(path));

    File f = FFat.open(path, "r");
    if (!f) return false;

    const size_t bytesRead = f.read(ccState, Config::CC_STATE_SIZE);
    f.close();

    if (bytesRead != Config::CC_STATE_SIZE) {
        Serial.printf("[PATCH] Read error: %u/%u bytes\n",
                      bytesRead, Config::CC_STATE_SIZE);
        return false;
    }

    Serial.printf("[PATCH] Loaded slot %u (%s)\n", slot,
                  names_[slot][0] ? names_[slot] : "unnamed");
    return true;
}

bool PatchStore::exists(uint8_t slot) const {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;
    char path[32];
    slotPath(slot, path, sizeof(path));
    return FFat.exists(path);
}

const char* PatchStore::getName(uint8_t slot) const {
    if (slot >= Config::MAX_PATCHES || names_[slot][0] == '\0') return nullptr;
    return names_[slot];
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
    slotPath(slot, path, sizeof(path));
    if (FFat.exists(path)) {
        FFat.remove(path);
    }
    names_[slot][0] = '\0';
    saveIndex();
    return true;
}

uint8_t PatchStore::count() const {
    if (!mounted_) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < Config::MAX_PATCHES; ++i) {
        if (exists(i)) ++n;
    }
    return n;
}

// ── Internal helpers ────────────────────────────────────────────────────────

void PatchStore::slotPath(uint8_t slot, char* buf, size_t bufLen) {
    snprintf(buf, bufLen, "%s/%03u.bin", kPatchDir, slot);
}

void PatchStore::loadIndex() {
    if (!FFat.exists(kIndexFile)) return;

    File f = FFat.open(kIndexFile, "r");
    if (!f) return;

    uint8_t slot = 0;
    while (f.available() && slot < Config::MAX_PATCHES) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && line.length() <= 16) {
            strncpy(names_[slot], line.c_str(), 16);
            names_[slot][16] = '\0';
        }
        ++slot;
    }
    f.close();
}

void PatchStore::saveIndex() {
    File f = FFat.open(kIndexFile, "w");
    if (!f) return;

    for (uint8_t i = 0; i < Config::MAX_PATCHES; ++i) {
        f.println(names_[i]);
    }
    f.close();
}
// ── Performance name (Phase 3 stub) ─────────────────────────────────────────

bool PatchStore::setPerfName(const char* name) {
    if (!mounted_ || !name) return false;
    strncpy(perfName_, name, 16);
    perfName_[16] = '\0';
    savePerfName();
    return true;
}

void PatchStore::loadPerfName() {
    perfName_[0] = '\0';
    if (!FFat.exists(kPerfFile)) return;

    File f = FFat.open(kPerfFile, "r");
    if (!f) return;

    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0 && line.length() <= 16) {
        strncpy(perfName_, line.c_str(), 16);
        perfName_[16] = '\0';
    }
    f.close();
}

void PatchStore::savePerfName() {
    File f = FFat.open(kPerfFile, "w");
    if (!f) return;
    f.println(perfName_);
    f.close();
}
