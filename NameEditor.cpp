// =============================================================================
// NameEditor.cpp
// =============================================================================
#include "NameEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Layout constants (480×320 landscape). Shared by draw() and cellAt() so the
// drawn cells and the touch hit zones are always identical.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    constexpr uint16_t SCREEN_W = 480;
    constexpr uint16_t SCREEN_H = 320;

    // Title bar + name preview.
    constexpr uint16_t TITLE_H   = 30;
    constexpr uint16_t NAME_Y    = 36;
    constexpr uint16_t NAME_H    = 32;

    // Character grid: 10 cols × 4 rows, 40px pitch (36px cell + 4px gap).
    constexpr uint16_t GRID_X0   = 6;
    constexpr uint16_t GRID_Y0   = 78;
    constexpr uint16_t CELL_PITCH = 40;
    constexpr uint16_t CELL_DRAW  = 36;   // drawn size; hit zone is full pitch
    constexpr uint8_t  GRID_COLS = 10;
    constexpr uint8_t  GRID_ROWS = 4;

    // Action column (right side).
    constexpr uint16_t ACT_X     = 410;
    constexpr uint16_t ACT_W     = 64;
    constexpr uint16_t ACT_HALF  = 30;    // half-width for the ◄ ► pair

    // Bottom bar (CANCEL).
    constexpr uint16_t BAR_Y     = 292;
    constexpr uint16_t BAR_H     = 28;
    constexpr uint16_t CANCEL_X  = 6;
    constexpr uint16_t CANCEL_W  = 80;

    // Colours (RGB565) — match DisplayRenderer's orange/navy theme.
    constexpr uint16_t COL_BG      = 0x10A2;  // dark navy (editor backdrop)
    constexpr uint16_t COL_CELL    = 0x18E3;  // cell fill
    constexpr uint16_t COL_CELLTX  = 0xCE79;  // cell text (light grey)
    constexpr uint16_t COL_ACCENT  = 0xFC00;  // orange
    constexpr uint16_t COL_HILITE  = 0xFC00;  // highlight border (orange)
    constexpr uint16_t COL_TEXT    = 0xFFFF;  // white
    constexpr uint16_t COL_DIM     = 0x52AA;  // dim grey (cursor keys, hints)
    constexpr uint16_t COL_TITLEBG = 0x18E3;

    // The 40 grid characters, row-major (A–J, K–T, U–3, 4–space).
    const char* const kGridChars =
        "ABCDEFGHIJ"
        "KLMNOPQRST"
        "UVWXYZ0123"
        "456789-_. ";   // last cell is space
}

char NameEditor::gridChar(uint8_t gridIdx) {
    return (gridIdx < kGridCount) ? kGridChars[gridIdx] : ' ';
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void NameEditor::open(Target target, uint8_t slot, const char* currentName) {
    active_ = true;
    target_ = target;
    slot_   = slot;

    // Seed buffer from currentName, clamped to kMaxLen, trailing spaces kept
    // as typed (the owner can trim on commit if desired).
    len_ = 0;
    if (currentName) {
        while (currentName[len_] != '\0' && len_ < kMaxLen) {
            buf_[len_] = currentName[len_];
            ++len_;
        }
    }
    buf_[len_] = '\0';
    cursor_    = len_;        // start cursor at the end
    hilite_    = 0;

    dirtyAll_   = true;
    dirtyName_  = true;
    prevHilite_ = 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer editing
// ─────────────────────────────────────────────────────────────────────────────
void NameEditor::insertChar(char c) {
    if (len_ >= kMaxLen) return;            // full
    // Shift right from the end down to the cursor to open a gap.
    for (uint8_t i = len_; i > cursor_; --i) buf_[i] = buf_[i - 1];
    buf_[cursor_] = c;
    ++len_;
    ++cursor_;
    buf_[len_] = '\0';
    dirtyName_ = true;
}

void NameEditor::backspace() {
    if (cursor_ == 0) return;               // nothing before cursor
    for (uint8_t i = cursor_ - 1; i < len_ - 1; ++i) buf_[i] = buf_[i + 1];
    --len_;
    --cursor_;
    buf_[len_] = '\0';
    dirtyName_ = true;
}

void NameEditor::clearAll() {
    len_ = 0;
    cursor_ = 0;
    buf_[0] = '\0';
    dirtyName_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell activation
// ─────────────────────────────────────────────────────────────────────────────
NameEditor::Action NameEditor::activateCell(uint8_t cellIdx) {
    if (cellIdx < kGridCount) {
        insertChar(gridChar(cellIdx));
        return Action::NONE;
    }
    switch (cellIdx) {
        case ACT_DEL:    backspace();                       return Action::NONE;
        case ACT_CLR:    clearAll();                        return Action::NONE;
        case ACT_LEFT:   if (cursor_ > 0)    { --cursor_; dirtyName_ = true; } return Action::NONE;
        case ACT_RIGHT:  if (cursor_ < len_) { ++cursor_; dirtyName_ = true; } return Action::NONE;
        case ACT_OK:                                        return Action::COMMIT;
        case ACT_CANCEL:                                    return Action::CANCEL;
        default:                                            return Action::NONE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Input handlers
// ─────────────────────────────────────────────────────────────────────────────
NameEditor::Action NameEditor::handleTouch(uint16_t x, uint16_t y) {
    if (!active_) return Action::NONE;
    const uint8_t cell = cellAt(x, y);
    if (cell == 0xFF) return Action::NONE;     // tapped a gap — ignore
    // Move the encoder highlight to the tapped cell so the two stay in sync.
    hilite_ = cell;
    return activateCell(cell);
}

NameEditor::Action NameEditor::handleEncoder(int32_t delta, bool push) {
    if (!active_) return Action::NONE;

    if (push) {
        return activateCell(hilite_);
    }
    if (delta != 0) {
        // Wrap the highlight through all cells (grid + actions).
        int32_t h = (int32_t)hilite_ + delta;
        const int32_t n = (int32_t)kCellCount;
        h %= n;
        if (h < 0) h += n;
        hilite_ = (uint8_t)h;
        // No buffer change — only the highlight moved (cheap repaint).
    }
    return Action::NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry / hit-test
// ─────────────────────────────────────────────────────────────────────────────
bool NameEditor::cellRect(uint8_t cellIdx, uint16_t& x, uint16_t& y,
                          uint16_t& w, uint16_t& h) {
    if (cellIdx < kGridCount) {
        const uint8_t col = cellIdx % GRID_COLS;
        const uint8_t row = cellIdx / GRID_COLS;
        x = GRID_X0 + col * CELL_PITCH;
        y = GRID_Y0 + row * CELL_PITCH;
        w = CELL_DRAW;
        h = CELL_DRAW;
        return true;
    }
    // Action keys: stacked in the right column, same row pitch as the grid.
    switch (cellIdx) {
        case ACT_DEL:   x = ACT_X; y = GRID_Y0 + 0 * CELL_PITCH; w = ACT_W; h = CELL_DRAW; return true;
        case ACT_CLR:   x = ACT_X; y = GRID_Y0 + 1 * CELL_PITCH; w = ACT_W; h = CELL_DRAW; return true;
        case ACT_LEFT:  x = ACT_X; y = GRID_Y0 + 2 * CELL_PITCH; w = ACT_HALF; h = CELL_DRAW; return true;
        case ACT_RIGHT: x = ACT_X + ACT_W - ACT_HALF; y = GRID_Y0 + 2 * CELL_PITCH; w = ACT_HALF; h = CELL_DRAW; return true;
        case ACT_OK:    x = ACT_X; y = GRID_Y0 + 3 * CELL_PITCH; w = ACT_W; h = CELL_DRAW; return true;
        case ACT_CANCEL:x = CANCEL_X; y = BAR_Y + 3; w = CANCEL_W; h = BAR_H - 6; return true;
        default: return false;
    }
}

uint8_t NameEditor::cellAt(uint16_t x, uint16_t y) {
    // Character grid: hit zone is the full pitch (CELL_PITCH), not just the
    // drawn 36px, so taps in the inter-cell gaps still register on the nearer
    // cell — easier to hit. Bound the grid region first.
    if (x >= GRID_X0 && y >= GRID_Y0 &&
        x < GRID_X0 + GRID_COLS * CELL_PITCH &&
        y < GRID_Y0 + GRID_ROWS * CELL_PITCH) {
        const uint8_t col = (x - GRID_X0) / CELL_PITCH;
        const uint8_t row = (y - GRID_Y0) / CELL_PITCH;
        if (col < GRID_COLS && row < GRID_ROWS) {
            return (uint8_t)(row * GRID_COLS + col);
        }
    }

    // Action keys — test each rect explicitly (few of them).
    for (uint8_t c = ACT_DEL; c < kCellCount; ++c) {
        uint16_t rx, ry, rw, rh;
        if (cellRect(c, rx, ry, rw, rh) &&
            x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
            return c;
        }
    }
    return 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────
void NameEditor::draw(Arduino_GFX* gfx) {
    if (!active_ || !gfx) return;

    if (dirtyAll_) {
        // Full paint: backdrop, title, name field, every cell.
        gfx->fillScreen(COL_BG);

        // Title bar
        gfx->fillRect(0, 0, SCREEN_W, TITLE_H, COL_TITLEBG);
        gfx->setTextColor(COL_ACCENT);
        gfx->setTextSize(2);
        gfx->setCursor(10, 7);
        gfx->print(target_ == Target::PERFORMANCE ? "RENAME PERF"
                                                   : "RENAME SLOT");
        if (target_ == Target::PATCH) {
            gfx->print(" ");
            gfx->print(slot_);
        }

        drawNameField(gfx);

        // All grid + action cells.
        for (uint8_t c = 0; c < kCellCount; ++c) {
            drawCell(gfx, c, c == hilite_);
        }
        prevHilite_ = hilite_;

        dirtyAll_  = false;
        dirtyName_ = false;
        return;
    }

    // Incremental: name field if text/cursor moved.
    if (dirtyName_) {
        drawNameField(gfx);
        dirtyName_ = false;
    }

    // Highlight moved: repaint just the two affected cells.
    if (hilite_ != prevHilite_) {
        if (prevHilite_ < kCellCount) drawCell(gfx, prevHilite_, false);
        drawCell(gfx, hilite_, true);
        prevHilite_ = hilite_;
    }
}

void NameEditor::drawNameField(Arduino_GFX* gfx) {
    // Clear + framed box.
    gfx->fillRect(8, NAME_Y, SCREEN_W - 16, NAME_H, COL_BG);
    gfx->drawRoundRect(10, NAME_Y, SCREEN_W - 20, NAME_H, 4, COL_ACCENT);

    // Name text (size 2). 6px*2*char ≈ 12px/char; 16 chars ≈ 192px — fits.
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(20, NAME_Y + 9);
    gfx->print(buf_);

    // Cursor caret: a vertical bar at the cursor's character column.
    const uint16_t caretX = 20 + (uint16_t)cursor_ * 12;
    gfx->fillRect(caretX, NAME_Y + 6, 2, 20, COL_ACCENT);

    // Length readout, right side.
    gfx->setTextColor(COL_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(SCREEN_W - 56, NAME_Y + 12);
    gfx->print(len_);
    gfx->print("/");
    gfx->print(kMaxLen);
}

void NameEditor::drawCell(Arduino_GFX* gfx, uint8_t cellIdx, bool highlighted) {
    uint16_t x, y, w, h;
    if (!cellRect(cellIdx, x, y, w, h)) return;

    // Background + fill differ for char cells vs action keys.
    const bool isAction = (cellIdx >= kGridCount);
    const uint16_t fill = isAction ? COL_BG : COL_CELL;
    gfx->fillRoundRect(x, y, w, h, 3, fill);

    // Border: highlight overrides; OK is always accent-filled.
    if (cellIdx == ACT_OK) {
        gfx->fillRoundRect(x, y, w, h, 3, COL_ACCENT);
    } else if (highlighted) {
        gfx->drawRoundRect(x, y, w, h, 3, COL_HILITE);
        gfx->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 3, COL_HILITE);
    } else if (isAction) {
        gfx->drawRoundRect(x, y, w, h, 3, COL_DIM);
    }

    // Label.
    gfx->setTextSize(isAction ? 1 : 2);
    uint16_t tx = x + (isAction ? 6 : (w / 2 - 6));
    uint16_t ty = y + (isAction ? (h / 2 - 4) : (h / 2 - 7));

    const char* lbl = nullptr;
    char one[2] = { 0, 0 };
    switch (cellIdx) {
        case ACT_DEL:    lbl = "DEL";   break;
        case ACT_CLR:    lbl = "CLR";   break;
        case ACT_LEFT:   lbl = "<";     break;
        case ACT_RIGHT:  lbl = ">";     break;
        case ACT_OK:     lbl = "OK";    break;
        case ACT_CANCEL: lbl = "CANCEL";break;
        default:
            one[0] = gridChar(cellIdx);
            // Show space as a small underscore so an empty-looking cell is
            // still visibly tappable.
            if (one[0] == ' ') { lbl = "SP"; }
            else               { lbl = one; }
            break;
    }

    if (cellIdx == ACT_OK)           gfx->setTextColor(COL_BG);       // dark on orange
    else if (cellIdx == ACT_CANCEL)  gfx->setTextColor(COL_DIM);
    else if (isAction)               gfx->setTextColor(COL_ACCENT);
    else                             gfx->setTextColor(COL_CELLTX);

    gfx->setCursor(tx, ty);
    gfx->print(lbl);
}
