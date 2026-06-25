// =============================================================================
// NameEditor.h — 16-char on-screen name editor (touch grid + encoder fallback)
// =============================================================================
// A self-contained modal overlay for editing a name (patch or performance).
// Primary input is the touch character grid; Encoder R is the fallback
// (rotate = move the grid highlight, push = activate the highlighted cell).
//
// DESIGN:
//   - Edits an internal 16-char buffer (+ null). Opens seeded with the current
//     name; commits the buffer to the caller on OK, discards on CANCEL.
//   - The character grid is 10 columns × 4 rows = 40 cells: A–Z, 0–9, then
//     '-', '_', '.', and space. An action column on the right holds DEL, CLR,
//     cursor ◄ ►, and OK; CANCEL sits in the bottom bar.
//   - Geometry constants are shared by the renderer and the touch hit-test
//     (single source of truth), mirroring the tab-bar approach.
//   - No dynamic allocation, no String — fixed buffers only (ESP32-friendly).
//
// LIFECYCLE (driven by the owner, e.g. PatchManager):
//   open(target, slot, currentName)  → becomes active, seeds the buffer
//   handleTouch(x, y)                → process one tap (returns an Action)
//   handleEncoder(delta, push)       → process encoder input (returns Action)
//   draw(gfx)                        → render the overlay (full or dirty)
//   isActive()                       → true while open
//   On OK/CANCEL the owner reads result() / buffer() and calls close().
//
// The editor does NOT touch storage itself — it only edits text and reports
// OK/CANCEL. The owner decides what to persist (PatchStore::setName / setPerf
// Name), keeping the editor reusable for any 16-char field.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Config.h"

class NameEditor {
public:
    static constexpr uint8_t kMaxLen = 16;   // name length (matches PatchStore)

    // What the editor is currently editing — lets the owner route the commit
    // to the right storage without the editor knowing about storage at all.
    enum class Target : uint8_t { PATCH, PERFORMANCE };

    // Result of a handleTouch()/handleEncoder() call — tells the owner whether
    // anything terminal happened. NONE = still editing (maybe redraw needed).
    enum class Action : uint8_t { NONE, COMMIT, CANCEL };

    NameEditor() = default;

    // Open the editor. target/slot are stored verbatim for the owner to read
    // back on COMMIT (the editor doesn't interpret them). currentName seeds the
    // buffer (may be nullptr/empty → starts blank). Forces a full redraw next
    // draw().
    void open(Target target, uint8_t slot, const char* currentName);

    // Mark the editor closed. The owner calls this after handling COMMIT/CANCEL.
    void close() { active_ = false; }
    bool isActive() const { return active_; }

    // ── Input ───────────────────────────────────────────────────────────────
    // Process a single tap at (x, y) in the 480×320 landscape frame. Returns
    // COMMIT if OK was tapped, CANCEL if CANCEL was tapped, else NONE.
    Action handleTouch(uint16_t x, uint16_t y);

    // Encoder fallback. delta moves the grid+action highlight (wrapping);
    // push activates the highlighted cell (same as tapping it). Returns the
    // same Action semantics as handleTouch.
    Action handleEncoder(int32_t delta, bool push);

    // ── Output (read by the owner on COMMIT) ─────────────────────────────────
    Target  target() const { return target_; }
    uint8_t slot()   const { return slot_; }
    const char* buffer() const { return buf_; }

    // ── Rendering ─────────────────────────────────────────────────────────────
    // Draw the overlay. The first call after open() (or any state change that
    // sets dirtyAll_) paints everything; subsequent calls repaint only what
    // changed (name field, highlighted cell) to keep SPI traffic low.
    void draw(Arduino_GFX* gfx);

private:
    // ── Editor state ──────────────────────────────────────────────────────────
    bool    active_  = false;
    Target  target_  = Target::PATCH;
    uint8_t slot_    = 0;
    char    buf_[kMaxLen + 1] = {};
    uint8_t len_     = 0;     // current string length
    uint8_t cursor_  = 0;     // insert position 0..len_
    uint8_t hilite_  = 0;     // highlighted cell index for encoder nav (0..kCellCount-1)

    // ── Dirty tracking ──────────────────────────────────────────────────────
    bool    dirtyAll_   = true;   // full repaint pending
    bool    dirtyName_  = true;   // name field needs repaint
    uint8_t prevHilite_ = 0xFF;   // last-drawn highlight cell (for cheap move)

    // ── Cell model ────────────────────────────────────────────────────────────
    // Cells 0..39  = character grid (kGridChars)
    // Cells 40..   = action keys, in this fixed order:
    enum CellAction : uint8_t {
        ACT_DEL = 40, ACT_CLR, ACT_LEFT, ACT_RIGHT, ACT_OK, ACT_CANCEL,
        kCellCount
    };
    static constexpr uint8_t kGridCount = 40;   // 10×4 character cells

    // Apply the effect of activating a cell (char append, DEL, etc.). Returns
    // an Action (COMMIT on OK, CANCEL on CANCEL, else NONE) and marks dirty.
    Action activateCell(uint8_t cellIdx);

    // Insert a character at the cursor (respecting kMaxLen); advance cursor.
    void insertChar(char c);
    void backspace();       // delete char before cursor
    void clearAll();        // wipe buffer

    // ── Geometry / hit-test (shared by draw + handleTouch) ────────────────────
    // Returns the cell index (0..kCellCount-1) at (x, y), or 0xFF for none.
    static uint8_t cellAt(uint16_t x, uint16_t y);
    // Bounding box of a cell index. Returns false if the index is out of range.
    static bool cellRect(uint8_t cellIdx, uint16_t& x, uint16_t& y,
                         uint16_t& w, uint16_t& h);
    // The display character for a grid cell (0..39).
    static char gridChar(uint8_t gridIdx);

    // Draw one cell (character or action) in its normal or highlighted state.
    void drawCell(Arduino_GFX* gfx, uint8_t cellIdx, bool highlighted);
    // Draw the name field + cursor.
    void drawNameField(Arduino_GFX* gfx);
};
