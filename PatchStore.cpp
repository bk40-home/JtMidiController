// =============================================================================
// PatchStore.cpp
// =============================================================================
#include "PatchStore.h"
#include <LittleFS.h>

static const char* kPatchDir  = "/patches";
static const char* kIndexFile = "/patches/index.txt";

bool PatchStore::begin() {
    if (!LittleFS.begin(true)) {  // true = format on first use
        Serial.println("[PATCH] LittleFS mount failed");
        return false;
    }

    // Create /patches directory if it doesn't exist
    if (!LittleFS.exists(kPatchDir)) {
        LittleFS.mkdir(kPatchDir);
    }

    // Initialise names to empty
    memset(names_, 0, sizeof(names_));
    loadIndex();

    mounted_ = true;
    Serial.printf("[PATCH] Store ready, %u patches found\n", count());
    return true;
}

bool PatchStore::save(uint8_t slot, const uint8_t* ccState, const char* name) {
    if (!mounted_ || slot >= Config::MAX_PATCHES) return false;

    char path[32];
    slotPath(slot, path, sizeof(path));

    File f = LittleFS.open(path, "w");
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

    File f = LittleFS.open(path, "r");
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
    return LittleFS.exists(path);
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
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
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
    if (!LittleFS.exists(kIndexFile)) return;

    File f = LittleFS.open(kIndexFile, "r");
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
    File f = LittleFS.open(kIndexFile, "w");
    if (!f) return;

    for (uint8_t i = 0; i < Config::MAX_PATCHES; ++i) {
        f.println(names_[i]);
    }
    f.close();
}
