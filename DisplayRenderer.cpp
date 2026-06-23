// =============================================================================
// DisplayRenderer.cpp
// =============================================================================
#include "DisplayRenderer.h"
#include "ParamDefs.h"   // findByCC() for SELECT option text lookup
#include <math.h>        // powf — ESP32 has hardware FPU

// ── TCA9554 reset sequence — matches the working Waveshare demo exactly ─────

void DisplayRenderer::resetDisplay() {
    tca_.begin();
    tca_.pinMode1(Config::TCA_RST_PIN, OUTPUT);

    // Reset pulse: high → low → high (demo sequence)
    tca_.write1(Config::TCA_RST_PIN, 1); delay(10);
    tca_.write1(Config::TCA_RST_PIN, 0); delay(10);
    tca_.write1(Config::TCA_RST_PIN, 1); delay(200);
}

// ── Display initialisation ──────────────────────────────────────────────────

bool DisplayRenderer::begin() {
    resetDisplay();

    // SPI bus — pin config matches the working Waveshare demo
    bus_ = new Arduino_ESP32SPI(
        Config::DISP_DC,     // DC = 3
        Config::DISP_CS,     // CS = -1 (directly wired)
        Config::DISP_SCLK,   // SCLK = 5
        Config::DISP_MOSI,   // MOSI = 1
        Config::DISP_MISO    // MISO = 2
    );

    // ST7796 — IPS = true, native 320×480 portrait.
    // Matches demo: Arduino_ST7796(bus_, -1, rotation, true, 320, 480)
    gfx_ = new Arduino_ST7796(
        bus_,
        Config::DISP_RST,    // RST = -1 (via TCA9554)
        3,                    // rotation (landscape: 480 wide × 320 tall)
        true,                 // IPS = true (matches working demo)
        Config::DISP_WIDTH,   // 320 (native width)
        Config::DISP_HEIGHT   // 480 (native height)
    );

    if (!gfx_->begin()) {
        Serial.println("[DISPLAY] Init failed");
        return false;
    }

    gfx_->fillScreen(COL_BG);

    // Enable backlight
    pinMode(Config::DISP_BL, OUTPUT);
    digitalWrite(Config::DISP_BL, HIGH);

    ready_ = true;
    Serial.println("[DISPLAY] ST7796 480x320 ready");
    return true;
}

// =============================================================================
// Full page redraw
// =============================================================================
// Called on page/scene/sub-page change. Routes to either normal grid or
// envelope curve renderer depending on the active page.

void DisplayRenderer::drawPage(const PageManager& pages) {
    if (!ready_) return;

    gfx_->fillScreen(COL_BG);
    drawHeader(pages);
    drawFooter(pages);

    // ENV pages get the ADSR curve visualisation instead of the grid
    if (pages.currentPage() == PageID::ENV) {
        drawEnvelopePage(pages);
        return;
    }

    // SEQ pages get the step sequencer bar visualisation
    if (pages.currentPage() == PageID::SEQ) {
        drawSequencerPage(pages);
        return;
    }

    // PTCH (Patch Manager) is drawn entirely by the PatchManager-aware
    // overload below. If this plain overload is called while PTCH is
    // active (e.g. a call site that hasn't been updated to pass a
    // PatchManager — shouldn't happen post-wiring, but safer than drawing
    // a blank param grid), fall back to a minimal placeholder rather than
    // silently rendering nothing useful.
    if (pages.currentPage() == PageID::PTCH) {
        gfx_->setTextColor(COL_LABEL);
        gfx_->setTextSize(1);
        gfx_->setCursor(8, PARAM_AREA_Y + 8);
        gfx_->print("Patch Manager unavailable — call drawPage(pages, patches)");
        return;
    }

    const auto& map = pages.activeMapping();
    const uint16_t accent = toRgb565(map.ledColour);

    // Pot and encoder arrays for the active scenes
    const ControlSlot* pots = (pages.potScene() == Scene::A)
                              ? map.potsA : map.potsB;
    const ControlSlot* encs = (pages.encScene() == Scene::A)
                              ? map.encsA : map.encsB;

    // Collect active encoders (skip SUB_SEL and NONE) for filling empty cells.
    // The queue preserves encoder order so params appear in a logical sequence.
    uint8_t encQueue[8];
    uint8_t encCount = 0;
    for (uint8_t i = 0; i < 8 && encCount < 8; ++i) {
        if (encs[i].isActive() && !encs[i].isSubSel()) {
            encQueue[encCount++] = i;
        }
    }

    uint8_t nextEnc = 0;  // next encoder to place in an empty cell

    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t col = i % 4;
        const uint8_t row = i / 4;

        if (pots[i].isActive()) {
            // Pot cell — standard rendering
            const uint8_t val = pages.getCCValue(pots[i].cc);
            const bool seeking = pages.pickup().isSeeking(i);
            drawParamCell(col, row, pots[i], val, seeking, accent, false);
            prevValues_[i] = val;
        } else if (nextEnc < encCount) {
            // Empty pot slot — fill with next available encoder value
            const ControlSlot& enc = encs[encQueue[nextEnc++]];
            const uint8_t val = pages.getCCValue(enc.cc);
            drawParamCell(col, row, enc, val, false, accent, true);
            prevValues_[i] = val;
        } else {
            // Truly empty — leave blank
            prevValues_[i] = 0xFF;
        }
    }
}

// =============================================================================
// Full page redraw — PatchManager-aware overload
// =============================================================================
// Identical to the plain overload for every page except PTCH, where it
// routes to drawPatchPage() instead of falling back to the placeholder
// text. This is the version the .ino should call from now on.

void DisplayRenderer::drawPage(const PageManager& pages,
                               const PatchManager& patches) {
    if (pages.currentPage() == PageID::PTCH) {
        if (!ready_) return;
        gfx_->fillScreen(COL_BG);
        drawHeader(pages);
        drawFooter(pages);
        drawPatchPage(patches);
        return;
    }
    drawPage(pages);
}

// =============================================================================
// Lightweight value refresh
// =============================================================================
// Called every frame between full redraws. Only touches pixels that changed.

void DisplayRenderer::refreshValues(const PageManager& pages) {
    if (!ready_) return;

    // ENV pages: check all envelope CCs, redraw curve if any moved
    if (pages.currentPage() == PageID::ENV) {
        refreshEnvelopePage(pages);
        return;
    }

    // SEQ pages: check step values, redraw changed bars
    if (pages.currentPage() == PageID::SEQ) {
        refreshSequencerPage(pages);
        return;
    }

    // PTCH has no CC-driven cells — ACTION slots all carry cc=0, so letting
    // this fall through to the generic cell loop below would spuriously
    // treat every ACTION encoder as "CC 0 changed" the first time CC 0's
    // cached value differs from prevValues_[i]'s sentinel. Bail out; the
    // PatchManager-aware overload is what actually drives this page.
    if (pages.currentPage() == PageID::PTCH) return;

    // Normal pages: rebuild cell layout (same logic as drawPage) and
    // redraw only cells whose CC value changed since last frame.
    const auto& map = pages.activeMapping();
    const uint16_t accent = toRgb565(map.ledColour);
    const ControlSlot* pots = (pages.potScene() == Scene::A)
                              ? map.potsA : map.potsB;
    const ControlSlot* encs = (pages.encScene() == Scene::A)
                              ? map.encsA : map.encsB;

    // Rebuild encoder queue (must match drawPage order exactly)
    uint8_t encQueue[8];
    uint8_t encCount = 0;
    for (uint8_t i = 0; i < 8 && encCount < 8; ++i) {
        if (encs[i].isActive() && !encs[i].isSubSel()) {
            encQueue[encCount++] = i;
        }
    }

    uint8_t nextEnc = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        const ControlSlot* slot = nullptr;
        bool isEnc = false;
        bool seeking = false;

        if (pots[i].isActive()) {
            slot = &pots[i];
            seeking = pages.pickup().isSeeking(i);
        } else if (nextEnc < encCount) {
            slot = &encs[encQueue[nextEnc++]];
            isEnc = true;
        }

        if (!slot) continue;  // truly empty cell

        const uint8_t val = pages.getCCValue(slot->cc);
        if (val != prevValues_[i]) {
            prevValues_[i] = val;
            // labelStays=true → cell label is left intact, only the value
            // text and bar gauge are erased + redrawn. Eliminates the brief
            // label flicker that happened on every value change.
            drawParamCell(i % 4, i / 4, *slot, val, seeking, accent, isEnc,
                          /*labelStays=*/true);
        }
    }
}

// =============================================================================
// Lightweight value refresh — PatchManager-aware overload
// =============================================================================
// Identical to the plain overload for every page except PTCH, where it
// routes to refreshPatchPage() instead of bailing out with no redraw.

void DisplayRenderer::refreshValues(const PageManager& pages,
                                    const PatchManager& patches) {
    if (pages.currentPage() == PageID::PTCH) {
        if (!ready_) return;
        refreshPatchPage(patches);
        return;
    }
    refreshValues(pages);
}

// =============================================================================
// Header
// =============================================================================

void DisplayRenderer::drawHeader(const PageManager& pages) {
    const auto& map = pages.activeMapping();
    const uint16_t accent = toRgb565(map.ledColour);

    gfx_->fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);

    // Page name — on ENV pages, show e.g. "AMP ENVELOPE"
    gfx_->setTextColor(accent);
    gfx_->setTextSize(2);
    gfx_->setCursor(8, 6);
    if (pages.currentPage() == PageID::ENV) {
        gfx_->print(map.name);
        gfx_->print(" ENVELOPE");
    } else {
        gfx_->print(map.name);
    }

    // Scene indicator (right)
    gfx_->setTextColor(COL_TEXT);
    gfx_->setCursor(SCREEN_W - 50, 6);
    gfx_->print("S:");
    gfx_->print(pages.potScene() == Scene::A ? "A" : "B");
}

void DisplayRenderer::updateHeader(const PageManager& pages) {
    drawHeader(pages);
}

// =============================================================================
// Footer
// =============================================================================

void DisplayRenderer::drawFooter(const PageManager& pages) {
    const uint16_t y = SCREEN_H - FOOTER_H;
    gfx_->fillRect(0, y, SCREEN_W, FOOTER_H, COL_HEADER);

    // Sub-page tabs (if applicable)
    if (PageMap::hasSubPages(pages.currentPage())) {
        const uint8_t count = PageMap::subPageCount(pages.currentPage());
        const uint16_t tabW = 80;
        const uint16_t startX = (SCREEN_W - count * tabW) / 2;

        for (uint8_t i = 0; i < count; ++i) {
            const auto& subMap = PageMap::getSubMapping(pages.currentPage(), i);
            const bool active = (i == pages.currentSubPage());
            const uint16_t tx = startX + i * tabW;

            if (active) {
                gfx_->fillRoundRect(tx, y + 2, tabW - 4, FOOTER_H - 4,
                                    4, toRgb565(subMap.ledColour));
                gfx_->setTextColor(COL_BG);
            } else {
                gfx_->drawRoundRect(tx, y + 2, tabW - 4, FOOTER_H - 4,
                                    4, toRgb565(subMap.ledColour));
                gfx_->setTextColor(COL_LABEL);
            }
            gfx_->setTextSize(1);
            gfx_->setCursor(tx + 8, y + 10);
            gfx_->print(subMap.tab);
        }
    }
}

// =============================================================================
// Parameter cell (normal pages)
// =============================================================================
// Renders one cell of the 2×4 grid.  Pot cells get standard styling;
// encoder cells get a subtly darker background and muted text.
// CONT / ENV / BIPOLAR types show a vertical bar on the right edge.
// SELECT / TOGGLE types show only the value text (no gauge — discrete values
// don't benefit from a position indicator).

void DisplayRenderer::drawParamCell(uint8_t col, uint8_t row,
                                    const ControlSlot& slot,
                                    uint8_t value, bool seeking,
                                    uint16_t accentColour,
                                    bool isEncoder, bool labelStays) {
    const uint16_t x = col * CELL_W;
    const uint16_t y = PARAM_AREA_Y + row * CELL_H;

    // Cell background — slightly darker for encoder cells
    const uint16_t bgCol   = isEncoder ? COL_ENC_BG  : COL_BG;
    const uint16_t lblCol  = isEncoder ? COL_ENC_LABEL : COL_LABEL;
    const uint16_t valCol  = seeking   ? COL_SEEKING
                           : isEncoder ? COL_ENC_TEXT : COL_TEXT;

    // Refresh path (labelStays=true) only erases the value + bar area below
    // the label row — the static label stays put. Full paint (labelStays=
    // false) erases the whole cell and redraws the label.
    if (labelStays) {
        gfx_->fillRect(x + 1, y + 14, CELL_W - 2, CELL_H - 15, bgCol);
    } else {
        gfx_->fillRect(x + 1, y + 1, CELL_W - 2, CELL_H - 2, bgCol);
    }

    if (!slot.isActive()) return;  // unmapped slot — leave blank

    // Label only on the full-paint path
    if (!labelStays) {
        gfx_->setTextColor(lblCol);
        gfx_->setTextSize(1);
        gfx_->setCursor(x + 4, y + 4);
        gfx_->print(slot.label);
    }

    // Value display — varies by type
    gfx_->setTextColor(valCol);

    const bool showBar = (slot.type == CtrlType::CONT
                       || slot.type == CtrlType::ENV
                       || slot.type == CtrlType::BIPOLAR);

    if (slot.type == CtrlType::TOGGLE) {
        // Large centred ON/OFF — no gauge
        gfx_->setTextSize(3);
        gfx_->setCursor(x + 4, y + CELL_H / 2 - 8);
        gfx_->print(value > 63 ? "ON" : "OFF");

    } else if (slot.type == CtrlType::SELECT) {
        // Look up option text from ParamDefs bucket tables.
        // Converts CC value → bucket index → option name string.
        const ParamDef* pd = ParamDefs::findByCC(slot.cc);
        if (pd && pd->optionCount > 0 && pd->options) {
            uint8_t idx = (uint32_t)value * pd->optionCount / 128;
            if (idx >= pd->optionCount) idx = pd->optionCount - 1;
            gfx_->setTextSize(2);
            gfx_->setCursor(x + 4, y + CELL_H / 2 - 6);
            gfx_->print(pd->options[idx]);
        } else {
            // Fallback — no option table found, show raw number
            gfx_->setTextSize(3);
            gfx_->setCursor(x + 4, y + CELL_H / 2 - 8);
            gfx_->print(value);
        }

    } else if (slot.type == CtrlType::BIPOLAR) {
        gfx_->setTextSize(3);
        gfx_->setCursor(x + 4, y + 20);
        const int8_t centred = (int8_t)value - 64;
        if (centred > 0) gfx_->print("+");
        gfx_->print(centred);

    } else {
        // CONT, ENV — numeric value
        gfx_->setTextSize(3);
        gfx_->setCursor(x + 4, y + 20);
        gfx_->print(value);
    }

    // Vertical bar gauge (right edge of cell) — only for continuous types
    if (showBar) {
        const uint16_t barX = x + CELL_W - 12;  // 12px from right edge
        const uint16_t barY = y + 6;
        const uint16_t barW = 6;
        const uint16_t barH = CELL_H - 12;

        // Background track
        gfx_->fillRoundRect(barX, barY, barW, barH, 3, COL_HEADER);

        if (slot.type == CtrlType::BIPOLAR) {
            // Centre tick
            const uint16_t midY = barY + barH / 2;
            gfx_->drawFastHLine(barX - 2, midY, barW + 4, COL_GRID);

            if (value > 64) {
                // Positive: fill upward from centre
                const uint16_t fillH = (uint32_t)(value - 64) * (barH / 2) / 63;
                if (fillH > 0) {
                    gfx_->fillRoundRect(barX, midY - fillH, barW, fillH,
                                        2, accentColour);
                }
            } else if (value < 64) {
                // Negative: fill downward from centre
                const uint16_t fillH = (uint32_t)(64 - value) * (barH / 2) / 64;
                if (fillH > 0) {
                    gfx_->fillRoundRect(barX, midY, barW, fillH,
                                        2, accentColour);
                }
            }
        } else {
            // CONT / ENV: fill from bottom proportional to value
            const uint16_t fillH = (uint32_t)value * barH / 127;
            if (fillH > 0) {
                gfx_->fillRoundRect(barX, barY + barH - fillH, barW, fillH,
                                    2, accentColour);
            }
        }
    }

    // Seeking arrow indicator
    if (seeking) {
        gfx_->setTextColor(COL_ACCENT);
        gfx_->setTextSize(2);
        gfx_->setCursor(x + CELL_W - 20, y + 20);
        gfx_->print(value < 64 ? "\x18" : "\x19");
    }
}

// =============================================================================
// ENV page — read parameters
// =============================================================================
// Extracts all ADSR + curve + depth CCs from the active page mapping.
// ADSR on pots 0–3.  Curves + depth on pots 4–7 (moved from encoders).
// Scanning by CtrlType keeps this in sync with PageMappings.h automatically.

EnvParams DisplayRenderer::readEnvParams(const PageManager& pages) const {
    const auto& map = pages.activeMapping();
    const ControlSlot* pots = (pages.potScene() == Scene::A)
                              ? map.potsA : map.potsB;

    EnvParams p = {};

    // ADSR from pots 0–3 (no inversion — adcToCC already handles direction)
    p.a = pots[0].isActive() ? pages.getCCValue(pots[0].cc) : 0;
    p.d = pots[1].isActive() ? pages.getCCValue(pots[1].cc) : 0;
    p.s = pots[2].isActive() ? pages.getCCValue(pots[2].cc) : 0;
    p.r = pots[3].isActive() ? pages.getCCValue(pots[3].cc) : 0;

    // Curves + depth from pots 4–7
    // Scan by type: CONT = curve CC, BIPOLAR = depth (PITCH sub-page only)
    uint8_t curveSlots[3] = {64, 64, 64};  // default to linear if missing
    uint8_t curveIdx = 0;
    p.hasDepth = false;

    for (uint8_t i = 4; i < 8; ++i) {
        if (!pots[i].isActive()) continue;
        if (pots[i].type == CtrlType::BIPOLAR) {
            p.depth    = (int8_t)pages.getCCValue(pots[i].cc) - 64;
            p.hasDepth = true;
        } else if (pots[i].type == CtrlType::CONT && curveIdx < 3) {
            curveSlots[curveIdx++] = pages.getCCValue(pots[i].cc);
        }
    }
    p.crvA = curveSlots[0];
    p.crvD = curveSlots[1];
    p.crvR = curveSlots[2];

    return p;
}

// =============================================================================
// ENV page — full draw
// =============================================================================

void DisplayRenderer::drawEnvelopePage(const PageManager& pages) {
    const auto& map  = pages.activeMapping();
    const uint16_t accent = toRgb565(map.ledColour);

    prevEnv_ = readEnvParams(pages);
    drawEnvelopeCurve(prevEnv_, accent);
}

// =============================================================================
// ENV page — differential refresh with partial redraw
// =============================================================================
// Three update categories from cheapest to most expensive:
//   1. Depth-only change (PITCH page): repaint the depth-text rect only.
//   2. Curve-only change (one of crvA/crvD/crvR, or sustain level): erase the
//      affected segment by retracing it in COL_BG with the OLD exponent, then
//      paint the new segment in accent with the NEW exponent. Adjacent vertex
//      dots are repainted to restore any pixels the erase clipped.
//   3. Geometry change (any of a/d/r): segment x positions move → full redraw.
//
// The cumulative-equality short-circuit at the top means an idle page costs
// one EnvParams snapshot + one struct compare per frame.

void DisplayRenderer::refreshEnvelopePage(const PageManager& pages) {
    const EnvParams now = readEnvParams(pages);
    if (now == prevEnv_) return;  // nothing changed — skip entirely

    const uint16_t accent = toRgb565(pages.activeMapping().ledColour);

    // a / d / r change moves the segment X positions — only a full redraw
    // can put everything in the right place.
    if (now.a != prevEnv_.a || now.d != prevEnv_.d || now.r != prevEnv_.r) {
        gfx_->fillRect(0, PARAM_AREA_Y, SCREEN_W, PARAM_AREA_H, COL_BG);
        drawEnvelopeCurve(now, accent);
        prevEnv_ = now;
        return;
    }

    // No x-geometry change — partial redraw path.
    // Sustain change moves the D→R vertex y, so both D and R need a redraw.
    const bool sChanged   = (now.s    != prevEnv_.s);
    const bool aDirty     = (now.crvA != prevEnv_.crvA);
    const bool dDirty     = (now.crvD != prevEnv_.crvD) || sChanged;
    const bool rDirty     = (now.crvR != prevEnv_.crvR) || sChanged;
    const bool depthDirty = now.hasDepth && (now.depth != prevEnv_.depth);

    // Per-segment erase + redraw. Erase uses prevEnv_ + OLD exponent so it
    // traces the exact pixel path the old segment drew; redraw uses `now` +
    // NEW exponent. Each segment is independent so order doesn't matter.
    if (aDirty) {
        drawEnvelopeSegment(0, prevEnv_, ccToExponent(prevEnv_.crvA), COL_BG);
        drawEnvelopeSegment(0, now,      ccToExponent(now.crvA),      accent);
    }
    if (dDirty) {
        drawEnvelopeSegment(1, prevEnv_, ccToExponent(prevEnv_.crvD), COL_BG);
        drawEnvelopeSegment(1, now,      ccToExponent(now.crvD),      accent);
    }
    if (rDirty) {
        drawEnvelopeSegment(2, prevEnv_, ccToExponent(prevEnv_.crvR), COL_BG);
        drawEnvelopeSegment(2, now,      ccToExponent(now.crvR),      accent);
    }

    // The COL_BG erase pass can clip pixels of any vertex dot the segment
    // brushed past — repaint all four to restore them. Four fillCircle calls,
    // negligible cost.
    if (aDirty || dDirty || rDirty) {
        drawEnvelopeDots(now, accent);
    }

    // Depth-only change: erase + redraw just the depth-label rect at top-right.
    if (depthDirty) {
        const uint16_t drawR = SCREEN_W - ENV_PAD_X;
        gfx_->fillRect(drawR - 110, PARAM_AREA_Y + 4, 110, 16, COL_BG);
        gfx_->setTextColor(dimColour(accent, 1));
        gfx_->setTextSize(2);
        gfx_->setCursor(drawR - 110, PARAM_AREA_Y + 4);
        gfx_->print("DEPTH ");
        if (now.depth > 0) gfx_->print('+');
        gfx_->print(now.depth);
    }

    prevEnv_ = now;
}

// =============================================================================
// ENV page — single segment painter (used by both full draw and partial redraw)
// =============================================================================
// Geometry is recomputed from `env` so the function works for both the OLD
// erase pass (env = prevEnv_) and the NEW paint pass (env = now). Used for
// per-segment partial redraw; drawEnvelopeCurve still does its own segment
// drawing inline because it has everything in scope.

void DisplayRenderer::drawEnvelopeSegment(uint8_t segIdx, const EnvParams& env,
                                          float exponent, uint16_t colour) {
    // Geometry — kept in sync with drawEnvelopeCurve. Any change there must
    // be mirrored here or the partial-redraw erase pass will leak pixels.
    const uint16_t drawL  = ENV_PAD_X;
    const uint16_t drawR  = SCREEN_W - ENV_PAD_X;
    const uint16_t drawW  = drawR - drawL;
    const uint16_t topY   = PARAM_AREA_Y + ENV_PAD_TOP;
    const uint16_t botY   = PARAM_AREA_Y + PARAM_AREA_H - ENV_PAD_BOT;
    const uint16_t drawH  = botY - topY;
    const int16_t  baseY  = (int16_t)botY;

    const uint16_t wTot = (uint16_t)env.a + env.d + env.r;
    uint16_t aW = 0, dW = 0, rW = 0;
    if (wTot > 0) {
        aW = (uint32_t)env.a * drawW / wTot;
        dW = (uint32_t)env.d * drawW / wTot;
        rW = drawW - aW - dW;
    }
    const int16_t xA   = (int16_t)drawL;
    const int16_t xD   = xA + (int16_t)aW;
    const int16_t xR   = xD + (int16_t)dW;
    const int16_t xEnd = xR + (int16_t)rW;

    const uint16_t sustPx = (uint32_t)env.s * drawH / 127;
    const int16_t  peakY  = baseY - (int16_t)drawH;
    const int16_t  sustY  = baseY - (int16_t)sustPx;

    // Pick segment endpoints
    int16_t x0, y0, x1, y1;
    switch (segIdx) {
        case 0: x0 = xA; y0 = baseY; x1 = xD;   y1 = peakY; break;  // attack
        case 1: x0 = xD; y0 = peakY; x1 = xR;   y1 = sustY; break;  // decay
        case 2: x0 = xR; y0 = sustY; x1 = xEnd; y1 = baseY; break;  // release
        default: return;
    }

    const int16_t segW = x1 - x0;

    if (segW <= 0) {
        // Zero-width: vertical line between the two y levels (same handling
        // as the inline drawSegment lambda in drawEnvelopeCurve).
        const int16_t yLo = constrain(min(y0, y1), (int16_t)topY, (int16_t)(botY - 1));
        const int16_t yHi = constrain(max(y0, y1), (int16_t)topY, (int16_t)(botY - 1));
        gfx_->drawFastVLine(x0, yLo, (yHi - yLo) + 1, colour);
        return;
    }

    const uint8_t samples = (uint8_t)min((uint16_t)PTS_PER_SEG, (uint16_t)segW);

    int16_t prevX = x0;
    int16_t prevY = constrain(y0, (int16_t)topY, (int16_t)(botY - 1));

    for (uint8_t i = 1; i <= samples; ++i) {
        const float t      = (float)i / samples;
        const float shaped = powf(t, exponent);
        const int16_t x    = x0 + (int16_t)(t * segW);
        int16_t y = y0 + (int16_t)(shaped * (y1 - y0));
        y = constrain(y, (int16_t)topY, (int16_t)(botY - 1));
        gfx_->drawLine(prevX, prevY, x, y, colour);
        prevX = x; prevY = y;
    }
}

// =============================================================================
// ENV page — repaint the four vertex dots
// =============================================================================
// Called after partial-segment redraws to restore any dot pixels the COL_BG
// erase pass clipped. Four fillCircle calls — much cheaper than a full curve
// redraw.

void DisplayRenderer::drawEnvelopeDots(const EnvParams& env,
                                       uint16_t accentColour) {
    // Same geometry as drawEnvelopeCurve / drawEnvelopeSegment.
    const uint16_t drawL  = ENV_PAD_X;
    const uint16_t drawR  = SCREEN_W - ENV_PAD_X;
    const uint16_t drawW  = drawR - drawL;
    const uint16_t topY   = PARAM_AREA_Y + ENV_PAD_TOP;
    const uint16_t botY   = PARAM_AREA_Y + PARAM_AREA_H - ENV_PAD_BOT;
    const uint16_t drawH  = botY - topY;
    const int16_t  baseY  = (int16_t)botY;

    const uint16_t wTot = (uint16_t)env.a + env.d + env.r;
    uint16_t aW = 0, dW = 0, rW = 0;
    if (wTot > 0) {
        aW = (uint32_t)env.a * drawW / wTot;
        dW = (uint32_t)env.d * drawW / wTot;
        rW = drawW - aW - dW;
    }
    const int16_t xA   = (int16_t)drawL;
    const int16_t xD   = xA + (int16_t)aW;
    const int16_t xR   = xD + (int16_t)dW;
    const int16_t xEnd = xR + (int16_t)rW;

    const uint16_t sustPx = (uint32_t)env.s * drawH / 127;
    const int16_t  peakY  = baseY - (int16_t)drawH;
    const int16_t  sustY  = baseY - (int16_t)sustPx;

    const int16_t peakYc = constrain(peakY, (int16_t)topY, (int16_t)(botY - 1));
    const int16_t sustYc = constrain(sustY, (int16_t)topY, (int16_t)(botY - 1));
    const uint16_t dimAccent = dimColour(accentColour, 1);

    gfx_->fillCircle(xA,   baseY,  3, dimAccent);     // origin
    gfx_->fillCircle(xD,   peakYc, 5, accentColour);  // peak  (A→D)
    gfx_->fillCircle(xR,   sustYc, 5, accentColour);  // sustain (D→R)
    gfx_->fillCircle(xEnd, baseY,  3, dimAccent);     // end
}

// =============================================================================
// ENV page — ADSR curve drawing
// =============================================================================
// Three back-to-back curves only: Attack (baseline → peak), Decay (peak →
// sustain level), Release (sustain level → baseline). Total width = drawW.
// Sustain is conveyed by the y-vertex at the D→R boundary plus the vertex
// dot there — there is no horizontal hold segment. Time CCs share drawW
// proportionally; a CC of 0 collapses that segment to zero width (vertical
// jump). The curve CC for a collapsed segment is a visual no-op, which is
// exactly what you want for instant-attack / instant-release patches.
//
// PITCH sub-page conveys depth via the "DEPTH +N" / "DEPTH -N" text at the
// top-right only — the curve shape is identical to AMP/FILTER so all three
// sub-pages are directly comparable at a glance.
//
// OUTLINE: single-pixel drawLine per sample (clean diagonals, ~half the SPI
// cost of any thickened variant).
// FILL:    per-sample fillRect spanning prevX → x at curve y, gap-free
//          horizontally. Step pattern in the top edge is masked visually by
//          the outline riding on top, and the fill is at ~12% brightness.
// CLAMP:   every sample y is clamped to [topY, botY-1] before drawing so the
//          curve can never spill into the stage-label area at extreme params.

void DisplayRenderer::drawEnvelopeCurve(const EnvParams& env,
                                        uint16_t accentColour) {
    // ── Layout geometry ─────────────────────────────────────────────────────
    const uint16_t drawL = ENV_PAD_X;
    const uint16_t drawR = SCREEN_W - ENV_PAD_X;
    const uint16_t drawW = drawR - drawL;
    const uint16_t topY  = PARAM_AREA_Y + ENV_PAD_TOP;
    const uint16_t botY  = PARAM_AREA_Y + PARAM_AREA_H - ENV_PAD_BOT;
    const uint16_t drawH = botY - topY;

    // Baseline at the bottom of the curve area, curve always upward — same
    // geometry for AMP/FILTER and PITCH sub-pages.
    const int16_t  baseY  = (int16_t)botY;
    const uint16_t curveH = drawH;

    // ── Segment widths ──────────────────────────────────────────────────────
    // A + D + R share drawW proportionally. Zero CC → zero width → vertical
    // jump at that boundary. Release absorbs integer-division rounding so the
    // total is exactly drawW.
    const uint16_t wA   = env.a;
    const uint16_t wD   = env.d;
    const uint16_t wR   = env.r;
    const uint16_t wTot = wA + wD + wR;

    uint16_t aW = 0, dW = 0, rW = 0;
    if (wTot > 0) {
        aW = (uint32_t)wA * drawW / wTot;
        dW = (uint32_t)wD * drawW / wTot;
        rW = drawW - aW - dW;       // absorbs remainder so xEnd == drawR
    }

    // Segment start X — no separate xS now that sustain has no width
    const int16_t xA   = (int16_t)drawL;
    const int16_t xD   = xA + (int16_t)aW;          // peak vertex
    const int16_t xR   = xD + (int16_t)dW;          // sustain vertex (D→R)
    const int16_t xEnd = xR + (int16_t)rW;          // end of release

    // Vertical levels
    const uint16_t sustPx = (uint32_t)env.s * curveH / 127;
    const int16_t  peakY  = baseY - (int16_t)curveH;
    const int16_t  sustY  = baseY - (int16_t)sustPx;

    // Curve exponents — one powf per sample inside drawSegment uses these
    const float expA = ccToExponent(env.crvA);
    const float expD = ccToExponent(env.crvD);
    const float expR = ccToExponent(env.crvR);

    // ── Baseline ────────────────────────────────────────────────────────────
    gfx_->drawFastHLine(drawL, baseY, drawW, COL_GRID);

    // ── Stage boundary dashed verticals ─────────────────────────────────────
    // Skip a marker if either adjacent segment is collapsed (the marker would
    // land on top of a vertex dot, looks messy).
    auto drawBoundary = [&](int16_t bx) {
        for (int16_t dy = (int16_t)topY; dy < (int16_t)botY; dy += 7) {
            gfx_->drawFastVLine(bx, dy,
                                min(4, (int)((int16_t)botY - dy)), COL_GRID);
        }
    };
    if (aW > 0 && dW > 0) drawBoundary(xD);
    if (dW > 0 && rW > 0) drawBoundary(xR);

    // ── Colours ─────────────────────────────────────────────────────────────
    const uint16_t dimAccent = dimColour(accentColour, 1);  // 50% accent

    // ── Curve segment helper ────────────────────────────────────────────────
    // Draws one parametric segment (x0,y0) → (x1,y1) along y = t^expC.
    // Zero-width is a clean vertical line. All draw calls bounded by clamp.
    auto drawSegment = [&](int16_t x0, int16_t y0,
                           int16_t x1, int16_t y1, float expC) {
        const int16_t segW = x1 - x0;

        if (segW <= 0) {
            // Zero-width: clean vertical between the two y levels. The curve
            // CC for this segment has no visible effect, which is correct —
            // a zero-time segment has no shape to apply.
            const int16_t yLo = constrain(min(y0, y1), (int16_t)topY, (int16_t)(botY - 1));
            const int16_t yHi = constrain(max(y0, y1), (int16_t)topY, (int16_t)(botY - 1));
            gfx_->drawFastVLine(x0, yLo, (yHi - yLo) + 1, accentColour);
            return;
        }

        // Cap samples to segment width — no overdraw on narrow segments
        const uint8_t samples = (uint8_t)min((uint16_t)PTS_PER_SEG, (uint16_t)segW);

        int16_t prevX = x0;
        int16_t prevY = constrain(y0, (int16_t)topY, (int16_t)(botY - 1));

        for (uint8_t i = 1; i <= samples; ++i) {
            const float t      = (float)i / samples;
            const float shaped = powf(t, expC);
            const int16_t x    = x0 + (int16_t)(t * segW);

            // Clamp y before any draw so we can never write outside the curve
            // area (was the root cause of the "lines across the screen" glitch
            // under extreme parameter combinations).
            int16_t y = y0 + (int16_t)(shaped * (y1 - y0));
            y = constrain(y, (int16_t)topY, (int16_t)(botY - 1));

            // Single-pixel diagonal line — no fill underneath any more.
            // (Fill was producing visible step patterns on ramps because
            // rectangle-based fill of a curve creates stair-step tops, and
            // the fill colour was bright enough that the steps showed
            // through the outline. Removing it gives a clean line graphic
            // that redraws cleanly under partial updates too.)
            gfx_->drawLine(prevX, prevY, x, y, accentColour);

            prevX = x;
            prevY = y;
        }
    };

    // ── Three back-to-back curve segments ───────────────────────────────────
    drawSegment(xA, baseY, xD, peakY, expA);    // ATTACK
    drawSegment(xD, peakY, xR, sustY, expD);    // DECAY (ends at sustain level)
    drawSegment(xR, sustY, xEnd, baseY, expR);  // RELEASE (starts at sustain)

    // ── Vertex dots ─────────────────────────────────────────────────────────
    // Four vertices total: origin, peak, sustain (single dot at D→R), end.
    // Was five with the old flat-sustain geometry (start and end of hold).
    const int16_t peakYc = constrain(peakY, (int16_t)topY, (int16_t)(botY - 1));
    const int16_t sustYc = constrain(sustY, (int16_t)topY, (int16_t)(botY - 1));

    gfx_->fillCircle(xA,   baseY,  3, dimAccent);    // origin
    gfx_->fillCircle(xD,   peakYc, 5, accentColour); // peak (A→D vertex)
    gfx_->fillCircle(xR,   sustYc, 5, accentColour); // sustain (D→R vertex)
    gfx_->fillCircle(xEnd, baseY,  3, dimAccent);    // end

    // ── Stage labels along the baseline ─────────────────────────────────────
    // No SUSTAIN label any more (no time region). Labels are skipped if the
    // segment is too narrow to fit the text — keeps the baseline tidy.
    gfx_->setTextColor(COL_GRID);
    gfx_->setTextSize(1);
    auto printCentred = [&](int16_t segX, uint16_t segW_, const char* text) {
        const uint16_t textW = strlen(text) * 6;  // 6 px per char at size 1
        if (segW_ < textW + 4) return;
        gfx_->setCursor(segX + (segW_ - textW) / 2, botY + 4);
        gfx_->print(text);
    };
    printCentred(xA, aW, "ATTACK");
    printCentred(xD, dW, "DECAY");
    printCentred(xR, rW, "RELEASE");

    // ── PITCH depth indicator (text only, size 2 for readability) ───────────
    // Sits in the strip between header and curve top, out of the way of any
    // curve drawing. Worst-case "DEPTH -63" = 9 chars × 12 px = 108 px wide.
    if (env.hasDepth) {
        gfx_->setTextColor(dimAccent);
        gfx_->setTextSize(2);
        gfx_->setCursor(drawR - 110, PARAM_AREA_Y + 4);
        gfx_->print("DEPTH ");
        if (env.depth > 0) gfx_->print('+');
        gfx_->print(env.depth);
    }
}

// =============================================================================
// SEQ page — read all params we depend on
// =============================================================================
// Pulls every CC the SEQ display reads from the cache so dirty-detection in
// refreshSequencerPage() can compare snapshots cheaply.

SeqParams DisplayRenderer::readSeqParams(const PageManager& pages) const {
    SeqParams p = {};
    p.enable    = pages.getCCValue(CC::SEQ_ENABLE);
    p.steps     = pages.getCCValue(CC::SEQ_STEPS);
    p.gate      = pages.getCCValue(CC::SEQ_GATE_LENGTH);
    p.slide     = pages.getCCValue(CC::SEQ_SLIDE);
    p.dir       = pages.getCCValue(CC::SEQ_DIRECTION);
    p.dest      = pages.getCCValue(CC::SEQ_DESTINATION);
    p.sync      = pages.getCCValue(CC::SEQ_TIMING_MODE);
    p.rate      = pages.getCCValue(CC::SEQ_RATE);
    p.depth     = (int8_t)pages.getCCValue(CC::SEQ_DEPTH) - 64;
    p.retrigger = pages.getCCValue(CC::SEQ_RETRIGGER);
    for (uint8_t i = 0; i < 16; ++i) p.values[i] = pages.getStepValue(i);
    return p;
}

// =============================================================================
// SEQ page — geometry helpers
// =============================================================================
// Slot bounds. Even spacing of SEQ_VISIBLE (=8) slots across the usable width
// inside SEQ_PAD_X margins, separated by SEQ_SLOT_GAP pixels.

void DisplayRenderer::seqSlotBounds(uint8_t slotIdx,
                                    uint16_t& slotX, uint16_t& slotW) const {
    const uint16_t drawW  = SCREEN_W - 2 * SEQ_PAD_X;
    // (SEQ_VISIBLE - 1) gaps total, so slot width = (drawW - gaps) / SEQ_VISIBLE
    slotW = (drawW - (SEQ_VISIBLE - 1) * SEQ_SLOT_GAP) / SEQ_VISIBLE;
    slotX = SEQ_PAD_X + slotIdx * (slotW + SEQ_SLOT_GAP);
}

// Bar top y from step value (0..127 → top of bars area .. baseline).
// 1 px headroom below the bars area top so the very tallest bars don't merge
// with the bars-area top edge.

int16_t DisplayRenderer::seqBarTopY(uint8_t value) const {
    const uint16_t maxH = SEQ_BARS_H - 4;  // small top headroom
    const uint16_t h    = (uint32_t)value * maxH / 127;
    return (int16_t)(SEQ_BARS_Y + SEQ_BARS_H - h);
}

// =============================================================================
// SEQ page — info row (DEST / DIR / SYNC / DEPTH / RTG)
// =============================================================================
// Single 22-px-tall strip at the top of the param area. Five fixed-x slots,
// text size 1 so labels + values both fit. Repainted on its own when only
// info-row params change.

void DisplayRenderer::drawSeqInfoRow(const SeqParams& sp,
                                     uint16_t accentColour) {
    // Erase the row strip first (so this works as both initial paint and
    // refresh — caller doesn't need a separate erase step).
    gfx_->fillRect(0, SEQ_INFO_Y, SCREEN_W, SEQ_INFO_H, COL_BG);

    const bool enabled = (sp.enable >= 64);
    // ENABLE off dims the whole info row so the whole page reads "disabled".
    const uint16_t textCol = enabled ? COL_TEXT : dimColour(COL_TEXT, 2);

    gfx_->setTextColor(textCol);
    gfx_->setTextSize(1);
    const uint16_t y = SEQ_INFO_Y + 8;  // vertically centred-ish in 22 px row

    // DEST — bucket-midpoint mapping matches the SELECT decoder elsewhere
    const uint8_t destIdx = (uint32_t)sp.dest * kSeqDestCount / 128;
    const char* destName  = (destIdx < kSeqDestCount)
                            ? kSeqDestOptions[destIdx] : "?";
    gfx_->setCursor(SEQ_PAD_X, y);
    gfx_->print("DEST:");
    gfx_->print(destName);

    // DIR — arrow glyph from Adafruit GFX's CP437-style font:
    //   0x1A = →   0x1B = ←   0x18 = ↑   0x19 = ↓
    // Bounce and Random don't have single-glyph icons, so use short text.
    const uint8_t dirIdx = (uint32_t)sp.dir * kSeqDirCount / 128;
    gfx_->setCursor(SEQ_PAD_X + 110, y);
    gfx_->print("DIR:");
    switch (dirIdx) {
        case 0: gfx_->write(0x1A);                  break;  // Fwd  →
        case 1: gfx_->write(0x1B);                  break;  // Rev  ←
        case 2: gfx_->print("<>");                  break;  // Bounce
        case 3: gfx_->print("?");                   break;  // Random
        default: gfx_->print(sp.dir);
    }

    // SYNC — name from the timing-mode options table
    const uint8_t syncIdx = (uint32_t)sp.sync * kTimingModeCount / 128;
    const char* syncName  = (syncIdx < kTimingModeCount)
                            ? kTimingModeOptions[syncIdx] : "?";
    gfx_->setCursor(SEQ_PAD_X + 180, y);
    gfx_->print("SYNC:");
    gfx_->print(syncName);

    // DEPTH — signed
    gfx_->setCursor(SEQ_PAD_X + 290, y);
    gfx_->print("DEPTH:");
    if (sp.depth > 0) gfx_->print('+');
    gfx_->print(sp.depth);

    // RTG (RETRIG) — ON / OFF
    gfx_->setCursor(SEQ_PAD_X + 380, y);
    gfx_->print("RTG:");
    gfx_->print(sp.retrigger >= 64 ? "ON" : "OFF");
}

// =============================================================================
// SEQ page — one bar (used by full draw + per-step partial redraw)
// =============================================================================
// Bar width = gate length (4..slotW). Bar height = step value (0..maxBarH).
// Inactive steps (slotIdx + 1 > active count for this scene) render as a thin
// dim outline at the baseline so the user still sees where the slot is.

void DisplayRenderer::drawSeqBar(uint8_t slotIdx, uint8_t absStep,
                                 const SeqParams& sp, uint16_t accentColour) {
    uint16_t slotX, slotW;
    seqSlotBounds(slotIdx, slotX, slotW);

    // Decode active step count from SEQ_STEPS CC. The Teensy maps 0..127 to
    // 1..16 with the same rule: activeCount = 1 + (cc * 15 / 127).
    const uint8_t activeCount = 1 + ((uint32_t)sp.steps * 15 / 127);
    const bool    isActive    = (absStep <= activeCount);
    const bool    enabled     = (sp.enable >= 64);

    // Bar width grows with SEQ_GATE_LENGTH. Min 4 px so even gate=0 leaves a
    // visible sliver; max slotW so gate=127 fills the slot.
    const uint16_t minW = 4;
    const uint16_t barW = minW + (uint32_t)sp.gate * (slotW - minW) / 127;
    const uint16_t barX = slotX + (slotW - barW) / 2;  // centred in slot

    if (isActive && enabled) {
        const int16_t  topY = seqBarTopY(sp.values[absStep - 1]);
        const uint16_t h    = (uint16_t)(SEQ_BARS_Y + SEQ_BARS_H - topY);
        if (h > 0) gfx_->fillRect(barX, topY, barW, h, accentColour);
    } else {
        // Inactive (or disabled) → dim outline at the slot baseline. Shows
        // where the step would be without filling the slot vertically.
        const uint16_t dim = dimColour(accentColour, 2);
        gfx_->drawRect(barX, SEQ_BARS_Y + SEQ_BARS_H - 5, barW, 4, dim);
    }
}

// =============================================================================
// SEQ page — slide line between two adjacent bars
// =============================================================================
// Drawn only when sp.slide > 0 AND both endpoint steps are active. Passing
// `colour` explicitly lets the same function paint (with the accent) and
// erase (with COL_BG), used by the per-step partial-redraw path.

void DisplayRenderer::drawSeqSlideLine(uint8_t fromSlotIdx, uint8_t toSlotIdx,
                                       const SeqParams& sp, uint8_t baseStep,
                                       uint16_t colour) {
    if (sp.slide == 0) return;  // slide disabled

    const uint8_t absFrom = baseStep + fromSlotIdx + 1;
    const uint8_t absTo   = baseStep + toSlotIdx   + 1;
    const uint8_t activeCount = 1 + ((uint32_t)sp.steps * 15 / 127);
    if (absFrom > activeCount || absTo > activeCount) return;
    if (sp.enable < 64) return;  // whole page dim when disabled — no slide line either

    uint16_t fromX, fromW, toX, toW;
    seqSlotBounds(fromSlotIdx, fromX, fromW);
    seqSlotBounds(toSlotIdx,   toX,   toW);

    const int16_t fromMidX = (int16_t)(fromX + fromW / 2);
    const int16_t toMidX   = (int16_t)(toX   + toW   / 2);
    const int16_t fromTopY = seqBarTopY(sp.values[absFrom - 1]);
    const int16_t toTopY   = seqBarTopY(sp.values[absTo   - 1]);

    gfx_->drawLine(fromMidX, fromTopY, toMidX, toTopY, colour);
}

// =============================================================================
// SEQ page — whole bars area (8 bars + slide lines + step number labels)
// =============================================================================
// Used on full draw and as the fallback when global params change (gate/
// slide-binary/active-step count/enable). Erases the bars area + label row
// first so callers don't need to.

void DisplayRenderer::drawSeqBarsArea(const SeqParams& sp, uint8_t baseStep,
                                      uint16_t accentColour) {
    // Erase bars area + step label row in one fillRect (single SPI window)
    gfx_->fillRect(0, SEQ_BARS_Y, SCREEN_W,
                   (SEQ_LABELS_Y + 12) - SEQ_BARS_Y, COL_BG);

    // Bars — left to right
    for (uint8_t slotIdx = 0; slotIdx < SEQ_VISIBLE; ++slotIdx) {
        const uint8_t absStep = baseStep + slotIdx + 1;
        drawSeqBar(slotIdx, absStep, sp, accentColour);
    }

    // Slide lines between adjacent active bars (skipped when slide==0)
    if (sp.slide > 0 && sp.enable >= 64) {
        const uint16_t slideCol = dimColour(accentColour, 1);
        for (uint8_t i = 0; i < SEQ_VISIBLE - 1; ++i) {
            drawSeqSlideLine(i, i + 1, sp, baseStep, slideCol);
        }
    }

    // Step number labels (1..16 mapped to current scene)
    const bool enabled = (sp.enable >= 64);
    gfx_->setTextColor(enabled ? COL_LABEL : dimColour(COL_LABEL, 1));
    gfx_->setTextSize(1);
    for (uint8_t slotIdx = 0; slotIdx < SEQ_VISIBLE; ++slotIdx) {
        const uint8_t  absStep = baseStep + slotIdx + 1;
        uint16_t slotX, slotW;
        seqSlotBounds(slotIdx, slotX, slotW);
        const uint16_t labelX = slotX + slotW / 2 - (absStep >= 10 ? 6 : 3);
        gfx_->setCursor(labelX, SEQ_LABELS_Y);
        gfx_->print(absStep);
    }
}

// =============================================================================
// SEQ page — full draw
// =============================================================================
// Called on page/scene change. Paints info row + bars area + labels and
// captures the current SeqParams as the dirty-detection baseline.

void DisplayRenderer::drawSequencerPage(const PageManager& pages) {
    const uint16_t accent = toRgb565(pages.activeMapping().ledColour);
    const uint8_t  baseStep = (pages.potScene() == Scene::B) ? 8 : 0;

    prevSeq_ = readSeqParams(pages);
    drawSeqInfoRow(prevSeq_, accent);
    drawSeqBarsArea(prevSeq_, baseStep, accent);
}

// =============================================================================
// SEQ page — differential refresh
// =============================================================================
// Three independent dirty regions:
//   - Info row    (dest/dir/sync/rate/depth/retrigger)  → repaint info row
//   - Bars area   (enable/steps/gate/slide-binary)      → repaint bars area
//   - Step values (per-step value change)               → per-bar partial
// More than one can be dirty in a single refresh; each is updated only if
// needed. enable-changed forces both info and bars repaint because both
// dim/un-dim together.

void DisplayRenderer::refreshSequencerPage(const PageManager& pages) {
    const SeqParams now = readSeqParams(pages);
    if (now == prevSeq_) return;

    const uint16_t accent   = toRgb565(pages.activeMapping().ledColour);
    const uint8_t  baseStep = (pages.potScene() == Scene::B) ? 8 : 0;

    // Categorise
    const bool enableChanged = (now.enable    != prevSeq_.enable);
    const bool infoChanged   = enableChanged
                            || (now.dest      != prevSeq_.dest)
                            || (now.dir       != prevSeq_.dir)
                            || (now.sync      != prevSeq_.sync)
                            || (now.rate      != prevSeq_.rate)
                            || (now.depth     != prevSeq_.depth)
                            || (now.retrigger != prevSeq_.retrigger);

    // "Slide binary" — show vs hide. Same line colour for any non-zero value,
    // so within-non-zero changes don't need a repaint.
    const bool slideBinaryChanged = ((now.slide == 0) != (prevSeq_.slide == 0));
    const bool barsGeomChanged    = enableChanged
                                 || (now.steps != prevSeq_.steps)
                                 || (now.gate  != prevSeq_.gate)
                                 || slideBinaryChanged;

    // Per-step value changes — only check the currently-visible scene's 8
    // steps. Off-scene changes are tracked but not painted (scene switch
    // forces a full redraw anyway).
    uint8_t stepChangeCount = 0;
    uint8_t lastChangedSlot = 0xFF;
    for (uint8_t i = 0; i < SEQ_VISIBLE; ++i) {
        if (now.values[baseStep + i] != prevSeq_.values[baseStep + i]) {
            stepChangeCount++;
            lastChangedSlot = i;
        }
    }

    // ── Repaint info row if needed ──────────────────────────────────────────
    if (infoChanged) drawSeqInfoRow(now, accent);

    // ── Repaint bars area if globals changed (overrides per-step partial) ──
    if (barsGeomChanged) {
        drawSeqBarsArea(now, baseStep, accent);
    } else if (stepChangeCount == 1) {
        // Single bar moved — cheapest path. Erase just that slot's column +
        // the slide-line segments either side of it, then repaint bar + lines.
        const uint8_t slotIdx = lastChangedSlot;
        uint16_t slotX, slotW;
        seqSlotBounds(slotIdx, slotX, slotW);

        // Erase old slide line segments to/from this bar (using prevSeq_ to
        // know where they were) so the OLD line pixels don't linger.
        if (prevSeq_.slide > 0 && prevSeq_.enable >= 64) {
            if (slotIdx > 0) {
                drawSeqSlideLine(slotIdx - 1, slotIdx, prevSeq_, baseStep, COL_BG);
            }
            if (slotIdx + 1 < SEQ_VISIBLE) {
                drawSeqSlideLine(slotIdx, slotIdx + 1, prevSeq_, baseStep, COL_BG);
            }
        }

        // Erase the slot column (bar + step-label area)
        gfx_->fillRect(slotX, SEQ_BARS_Y, slotW,
                       (SEQ_LABELS_Y + 12) - SEQ_BARS_Y, COL_BG);

        // Redraw bar
        drawSeqBar(slotIdx, baseStep + slotIdx + 1, now, accent);

        // Redraw new slide lines on either side
        if (now.slide > 0 && now.enable >= 64) {
            const uint16_t slideCol = dimColour(accent, 1);
            if (slotIdx > 0) {
                drawSeqSlideLine(slotIdx - 1, slotIdx, now, baseStep, slideCol);
            }
            if (slotIdx + 1 < SEQ_VISIBLE) {
                drawSeqSlideLine(slotIdx, slotIdx + 1, now, baseStep, slideCol);
            }
        }

        // Repaint step number label (was erased with the slot column)
        const bool enabled = (now.enable >= 64);
        gfx_->setTextColor(enabled ? COL_LABEL : dimColour(COL_LABEL, 1));
        gfx_->setTextSize(1);
        const uint8_t  absStep = baseStep + slotIdx + 1;
        const uint16_t labelX  = slotX + slotW / 2 - (absStep >= 10 ? 6 : 3);
        gfx_->setCursor(labelX, SEQ_LABELS_Y);
        gfx_->print(absStep);

    } else if (stepChangeCount > 1) {
        // Multiple step values changed (typically a patch load) — single full
        // bars-area repaint is cheaper than N per-bar redraws.
        drawSeqBarsArea(now, baseStep, accent);
    }

    prevSeq_ = now;
}

// =============================================================================
// Colour utilities
// =============================================================================

uint16_t DisplayRenderer::toRgb565(uint32_t rgb) {
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8)  & 0xFF;
    const uint8_t b =  rgb        & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Reduce RGB565 brightness by right-shifting each channel.
// shift=1 → ~50%, shift=2 → ~25%, shift=3 → ~12%.
uint16_t DisplayRenderer::dimColour(uint16_t c, uint8_t shift) {
    const uint16_t r = ((c >> 11) & 0x1F) >> shift;
    const uint16_t g = ((c >> 5)  & 0x3F) >> shift;
    const uint16_t b = (c         & 0x1F) >> shift;
    return (r << 11) | (g << 5) | b;
}

// =============================================================================
// Curve exponent mapping
// =============================================================================
// Maps CC 0–127 to a power-law exponent for envelope segment shaping.
// Called at most 3 times per curve redraw (once per shaped segment).
//
//   CC  0   → 0.15  (logarithmic: fast initial rise, slow to finish)
//   CC 64   → 1.0   (linear: constant rate)
//   CC 127  → 5.0   (exponential: slow to start, fast to finish)
//
// The split at CC 64 gives finer control in the log range (more useful
// for envelopes) while still allowing extreme exponential curves.

float DisplayRenderer::ccToExponent(uint8_t cc) {
    if (cc <= 64) {
        // 0..64 → 0.15..1.0 (log range — most musically useful)
        return 0.15f + (cc * (0.85f / 64.0f));
    }
    // 65..127 → 1.0..5.0 (exponential range)
    return 1.0f + ((cc - 64) * (4.0f / 63.0f));
}

// =============================================================================
// PTCH page — Patch Manager browse list + banner
// =============================================================================
// No CC values, no pots, no sub-pages. Drawing is just:
//   1. Config::PTCH_VISIBLE_ROWS rows, each "SLOT NNN  Name" (or empty)
//   2. A banner strip overlaid at the bottom when PatchManager has an
//      active message (LOADED / SAVED / SAVE_ARMED / etc.)
//
// The visible window always centres on the highlighted slot where possible,
// clamping at the top/bottom of the 0..MAX_PATCHES-1 range so partial rows
// never need to be drawn.

void DisplayRenderer::drawPatchPage(const PatchManager& patches) {
    if (!ready_) return;

    const uint8_t highlight = patches.highlightedSlot();

    // Centre the window on the highlighted slot, clamped to valid range.
    // Integer-only math — no float needed for a window this small.
    int16_t windowStart = static_cast<int16_t>(highlight)
                         - (Config::PTCH_VISIBLE_ROWS / 2);
    if (windowStart < 0) windowStart = 0;
    const int16_t maxStart = static_cast<int16_t>(Config::MAX_PATCHES)
                            - Config::PTCH_VISIBLE_ROWS;
    if (windowStart > maxStart) windowStart = (maxStart > 0) ? maxStart : 0;

    for (uint8_t row = 0; row < Config::PTCH_VISIBLE_ROWS; ++row) {
        const uint8_t slot = static_cast<uint8_t>(windowStart + row);
        drawPtchRow(patches, row, slot, slot == highlight);
    }

    prevPtchHighlight_ = highlight;
    prevPtchBanner_    = PtchBannerType::NONE;  // force first refresh to draw it

    drawPtchBanner(patches);
}

void DisplayRenderer::refreshPatchPage(const PatchManager& patches) {
    if (!ready_) return;

    const uint8_t highlight = patches.highlightedSlot();

    // Highlight moved — full row repaint is simplest and still cheap (5
    // rows × one fillRect + two prints each, well within the 33 ms frame
    // budget). A more surgical erase/redraw of just the two affected rows
    // would save a handful of SPI calls but isn't worth the bookkeeping
    // for a 5-row list that redraws at most a few times a second.
    if (highlight != prevPtchHighlight_) {
        drawPatchPage(patches);
        return;
    }

    // Highlight unchanged — only the banner might need updating.
    if (patches.bannerType() != prevPtchBanner_) {
        if (patches.bannerType() == PtchBannerType::NONE) {
            erasePtchBanner(patches);
        } else {
            drawPtchBanner(patches);
        }
        prevPtchBanner_ = patches.bannerType();
    }
}

void DisplayRenderer::drawPtchRow(const PatchManager& patches,
                                  uint8_t rowIdx, uint8_t slot,
                                  bool highlighted) {
    const uint16_t y = PTCH_ROW_Y + rowIdx * PTCH_ROW_H;

    gfx_->fillRect(0, y, SCREEN_W, PTCH_ROW_H, COL_BG);

    if (highlighted) {
        gfx_->fillRoundRect(PTCH_PAD_X - 4, y + 2, SCREEN_W - 2 * (PTCH_PAD_X - 4),
                            PTCH_ROW_H - 4, 4, COL_ENC_BG);
    }

    const uint16_t textCol = highlighted ? COL_ACCENT : COL_TEXT;
    gfx_->setTextColor(textCol);
    gfx_->setTextSize(1);
    gfx_->setCursor(PTCH_PAD_X, y + (PTCH_ROW_H / 2) - 4);

    // nullptr from slotName() is PatchStore's documented contract for an
    // empty/unset slot (see PatchStore::getName()'s header comment).
    char line[40];
    const char* name = patches.slotName(slot);

    if (name) {
        snprintf(line, sizeof(line), "SLOT %03u  %s", slot, name);
    } else {
        snprintf(line, sizeof(line), "SLOT %03u  -- empty --", slot);
    }
    gfx_->print(line);
}

void DisplayRenderer::drawPtchBanner(const PatchManager& patches) {
    gfx_->fillRect(0, PTCH_BANNER_Y, SCREEN_W, PTCH_BANNER_H, COL_HEADER);

    char text[48];
    const uint8_t slot = patches.bannerSlot();

    switch (patches.bannerType()) {
        case PtchBannerType::LOADED:
            snprintf(text, sizeof(text), "LOADED: %s", patches.loadedName());
            break;
        case PtchBannerType::SAVED:
            snprintf(text, sizeof(text), "SAVED - SLOT %03u", slot);
            break;
        case PtchBannerType::SAVE_ARMED:
            snprintf(text, sizeof(text), "SAVE SLOT %03u? PUSH AGAIN", slot);
            break;
        case PtchBannerType::SAVE_CANCELLED:
            snprintf(text, sizeof(text), "SAVE CANCELLED");
            break;
        case PtchBannerType::SAVE_FAILED:
            snprintf(text, sizeof(text), "SAVE FAILED - SEE SERIAL LOG");
            break;
        case PtchBannerType::EMPTY_SLOT:
            snprintf(text, sizeof(text), "SLOT %03u EMPTY - NOTHING TO LOAD", slot);
            break;
        case PtchBannerType::COMING_SOON:
            snprintf(text, sizeof(text), "SYX IMPORT - COMING SOON");
            break;
        case PtchBannerType::NONE:
        default:
            return;  // nothing to draw
    }

    gfx_->setTextColor(COL_ACCENT);
    gfx_->setTextSize(1);
    gfx_->setCursor(PTCH_PAD_X, PTCH_BANNER_Y + (PTCH_BANNER_H / 2) - 4);
    gfx_->print(text);
}

void DisplayRenderer::erasePtchBanner(const PatchManager& /*patches*/) {
    gfx_->fillRect(0, PTCH_BANNER_Y, SCREEN_W, PTCH_BANNER_H, COL_BG);
}