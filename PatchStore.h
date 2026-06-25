// =============================================================================
// PatchStore.h — Patch storage on ESP32 flash via LittleFS
// =============================================================================
// Stores and recalls synth patches as binary CC state arrays. Each patch is
// CC_STATE_SIZE bytes matching the Teensy's ccState[] array.
//
// File format: /patches/NNN.bin (where NNN = 000..127, zero-padded)
// Each file is exactly CC_STATE_SIZE bytes — a raw dump of all CC values.
//
// WORKFLOW:
//   Save: PageManager::ccState_ → PatchStore → LittleFS
//   Load: LittleFS → PatchStore → send all CCs to Teensy via UART
//
// Patch names are stored in a separate index file: /patches/index.txt
// Format: one name per line, up to MAX_PATCHES lines.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "Config.h"

class PatchStore {
public:
    PatchStore() = default;

    // Mount LittleFS and create /patches directory if needed.
    bool begin();

    // Save CC state array to patch slot. Returns true on success.
    bool save(uint8_t slot, const uint8_t* ccState, const char* name = nullptr);

    // Load CC state array from patch slot. Returns true on success.
    bool load(uint8_t slot, uint8_t* ccState);

    // Check if a patch slot has data
    bool exists(uint8_t slot) const;

    // Get patch name (returns nullptr if no name set)
    const char* getName(uint8_t slot) const;

    // Set patch name only (without saving CC data)
    bool setName(uint8_t slot, const char* name);

    // ── Performance name (Phase 3 stub) ──────────────────────────────────────
    // A single 16-char performance name, ESP32-local for now (stored in its own
    // FFat file). The synth's performance/layer state lives on the Teensy; this
    // is a local label the NameEditor can edit and persist. When the cross-
    // codebase performance work lands, this is the slot that syncs to it.
    const char* getPerfName() const { return perfName_; }
    bool        setPerfName(const char* name);

    // Delete a patch slot
    bool remove(uint8_t slot);

    // Get the total number of saved patches
    uint8_t count() const;

private:
    bool mounted_ = false;
    char names_[Config::MAX_PATCHES][17] = {};  // 16 chars + null

    // Performance name (single entry) + its own backing file.
    char perfName_[17] = {};

    // Build file path for a given slot
    static void slotPath(uint8_t slot, char* buf, size_t bufLen);

    // Load/save the name index file
    void loadIndex();
    void saveIndex();

    // Load/save the performance-name file.
    void loadPerfName();
    void savePerfName();
};
