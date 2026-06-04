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
            drawParamCell(i % 4, i / 4, *slot, val, seeking, accent, isEnc);
        }
    }
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
                                    bool isEncoder) {
    const uint16_t x = col * CELL_W;
    const uint16_t y = PARAM_AREA_Y + row * CELL_H;

    // Cell background — slightly darker for encoder cells
    const uint16_t bgCol   = isEncoder ? COL_ENC_BG  : COL_BG;
    const uint16_t lblCol  = isEncoder ? COL_ENC_LABEL : COL_LABEL;
    const uint16_t valCol  = seeking   ? COL_SEEKING
                           : isEncoder ? COL_ENC_TEXT : COL_TEXT;

    gfx_->fillRect(x + 1, y + 1, CELL_W - 2, CELL_H - 2, bgCol);

    if (!slot.isActive()) return;  // unmapped slot — leave blank

    // Label (top of cell)
    gfx_->setTextColor(lblCol);
    gfx_->setTextSize(1);
    gfx_->setCursor(x + 4, y + 4);
    gfx_->print(slot.label);

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
// ENV page — differential refresh
// =============================================================================
// Only redraws the curve when at least one of the 7+ CCs has changed.
// Avoids SPI traffic on idle frames.

void DisplayRenderer::refreshEnvelopePage(const PageManager& pages) {
    const EnvParams now = readEnvParams(pages);
    if (now == prevEnv_) return;  // nothing changed — skip entirely

    prevEnv_ = now;
    const uint16_t accent = toRgb565(pages.activeMapping().ledColour);

    // Erase the param area and redraw the curve
    gfx_->fillRect(0, PARAM_AREA_Y, SCREEN_W, PARAM_AREA_H, COL_BG);
    drawEnvelopeCurve(now, accent);
}

// =============================================================================
// ENV page — ADSR curve drawing
// =============================================================================
// Draws the full ADSR curve, translucent fill, vertex dots, stage labels,
// dots, stage labels, and (for PITCH) the depth indicator.
//
// SEGMENT WIDTHS:
//   A, D, R durations are proportional to their CC value (after pot inversion).
//   Sustain hold gets a fixed width — it represents a level, not a time.
//   A minimum width per segment prevents any segment from collapsing to zero.
//
// CURVE SHAPE:
//   Each segment's shape is t^exponent where t goes 0→1 across the segment.
//   Attack: y rises from 0 to peak.  Decay: y falls from peak to sustain.
//   Release: y falls from sustain to 0.  All shaped by their respective
//   curve CC exponent.
//
// PITCH DEPTH:
//   When depth is present, the baseline shifts to the vertical centre of the
//   curve area (representing zero pitch shift).  The curve is drawn above
//   (positive depth) or below (negative depth) the centre line, scaled by
//   the absolute depth value.

void DisplayRenderer::drawEnvelopeCurve(const EnvParams& env,
                                        uint16_t accentColour) {
    // ── Layout geometry ─────────────────────────────────────────────────────
    const uint16_t drawL = ENV_PAD_X;                          // left edge
    const uint16_t drawR = SCREEN_W - ENV_PAD_X;               // right edge
    const uint16_t drawW = drawR - drawL;                      // usable width
    const uint16_t topY  = PARAM_AREA_Y + ENV_PAD_TOP;         // top of curve area
    const uint16_t botY  = PARAM_AREA_Y + PARAM_AREA_H - ENV_PAD_BOT; // bottom
    const uint16_t drawH = botY - topY;                        // usable height

    // Baseline Y — for normal envelopes sits at botY.
    // For PITCH, it's the vertical centre (zero pitch shift).
    // int16_t so min(y, baseY) and abs(y - baseY) don't hit signed/unsigned mismatch.
    const int16_t baseY = env.hasDepth ? (topY + drawH / 2) : botY;

    // Curve height — for PITCH, scaled by absolute depth (0..63 → 0..full)
    // For AMP/FILTER, always full height.
    const uint16_t curveH = env.hasDepth
        ? (uint16_t)((uint32_t)abs(env.depth) * (drawH / 2) / 63)
        : drawH;

    // Direction: +1 draws upward from baseline (normal), -1 draws downward
    // (negative pitch depth).
    const int8_t direction = (env.hasDepth && env.depth < 0) ? -1 : 1;

    // ── Segment widths (proportional to time CCs) ───────────────────────────
    // Minimum 8px per segment so nothing vanishes.
    // Sustain hold = fixed 20% of draw width.
    static constexpr uint16_t MIN_SEG   = 8;
    static constexpr float    SUST_FRAC = 0.20f;

    const uint16_t sustW = (uint16_t)(drawW * SUST_FRAC);
    const uint16_t timeW = drawW - sustW;  // width budget for A + D + R

    // Sum the three time CCs to compute relative proportions
    const uint16_t aRaw = max((uint16_t)env.a, (uint16_t)1);
    const uint16_t dRaw = max((uint16_t)env.d, (uint16_t)1);
    const uint16_t rRaw = max((uint16_t)env.r, (uint16_t)1);
    const uint16_t timeSum = aRaw + dRaw + rRaw;

    uint16_t aW = max((uint16_t)((uint32_t)aRaw * timeW / timeSum), MIN_SEG);
    uint16_t dW = max((uint16_t)((uint32_t)dRaw * timeW / timeSum), MIN_SEG);
    uint16_t rW = max((uint16_t)((uint32_t)rRaw * timeW / timeSum), MIN_SEG);

    // Clamp total to avoid overflow (min segments may have pushed it)
    const uint16_t totalSeg = aW + dW + sustW + rW;
    if (totalSeg > drawW) {
        // Trim release to fit (least visually disruptive)
        rW = drawW - aW - dW - sustW;
    }

    // Segment start X positions
    const uint16_t xA = drawL;             // attack starts here
    const uint16_t xD = xA + aW;           // decay starts here (= peak)
    const uint16_t xS = xD + dW;           // sustain starts here
    const uint16_t xR = xS + sustW;        // release starts here
    const uint16_t xEnd = xR + rW;         // release ends here

    // Sustain level in pixels (0 = no sustain → curve returns to baseline)
    const uint16_t sustPx = (uint16_t)((uint32_t)env.s * curveH / 127);

    // Peak Y and sustain Y (pixel coords, accounting for direction)
    const int16_t peakY = baseY - direction * curveH;
    const int16_t sustY = baseY - direction * sustPx;

    // ── Curve exponents (computed once, used per-segment) ───────────────────
    const float expA = ccToExponent(env.crvA);
    const float expD = ccToExponent(env.crvD);
    const float expR = ccToExponent(env.crvR);

    // ── Draw grid references ────────────────────────────────────────────────
    // Baseline
    gfx_->drawFastHLine(drawL, baseY, drawW, COL_GRID);

    // 50% reference (dashed feel — draw every 6px with 4px gap)
    if (!env.hasDepth) {
        const int16_t halfY = topY + drawH / 2;
        for (uint16_t dx = drawL; dx < drawR; dx += 10) {
            gfx_->drawFastHLine(dx, halfY, min((uint16_t)6, (uint16_t)(drawR - dx)),
                                COL_GRID);
        }
    }

    // PITCH centre line label
    if (env.hasDepth) {
        gfx_->setTextColor(COL_GRID);
        gfx_->setTextSize(1);
        gfx_->setCursor(drawL - 12, baseY - 3);
        gfx_->print("0");
    }

    // Stage boundary markers (dashed vertical lines)
    const uint16_t bounds[] = {xD, xS, xR};
    for (uint8_t bi = 0; bi < 3; ++bi) {
        const uint16_t bx = bounds[bi];
        for (int16_t dy = topY; dy < botY; dy += 7) {
            gfx_->drawFastVLine(bx, dy, min(4, (int)(botY - dy)), COL_GRID);
        }
    }

    // ── Colours for fill and outline ────────────────────────────────────────
    const uint16_t fillCol = dimColour(accentColour, 3);  // ~12% brightness fill

    // ── Helper lambda: compute curve Y for a parametric t in [0,1] ──────────
    // Captures baseY, direction, and segment-specific start/end levels.
    // Inline to avoid function-call overhead in the tight loop.

    // ── Draw segments ───────────────────────────────────────────────────────
    // Each segment: compute points, draw fill (vlines) then outline (lines).
    // Drawing fill first ensures the outline sits on top.

    int16_t prevX = xA;
    int16_t prevDrawY = baseY;

    // --- ATTACK: baseline → peak, shaped by attack exponent ---
    for (uint8_t i = 1; i <= PTS_PER_SEG; ++i) {
        const float t = (float)i / PTS_PER_SEG;
        const float shaped = powf(t, expA);       // 0→1 shaped by curve
        const int16_t x = xA + (int16_t)(t * aW);
        // Interpolate from baseY toward peakY
        const int16_t y = baseY + (int16_t)(shaped * (peakY - baseY));

        // Fill: vertical stripe from curve point to baseline
        const int16_t fy = min(y, baseY);
        const int16_t fh = abs(y - baseY);
        if (fh > 0) gfx_->drawFastVLine(x, fy, fh, fillCol);

        // Outline: 2px thick (draw + 1px offset for visibility)
        gfx_->drawLine(prevX, prevDrawY, x, y, accentColour);
        gfx_->drawLine(prevX, prevDrawY + 1, x, y + 1, accentColour);

        prevX = x;
        prevDrawY = y;
    }

    // --- DECAY: peak → sustain level, shaped by decay exponent ---
    for (uint8_t i = 1; i <= PTS_PER_SEG; ++i) {
        const float t = (float)i / PTS_PER_SEG;
        const float shaped = powf(t, expD);
        const int16_t x = xD + (int16_t)(t * dW);
        // Interpolate from peakY toward sustY
        const int16_t y = peakY + (int16_t)(shaped * (sustY - peakY));

        const int16_t fy = min(y, baseY);
        const int16_t fh = abs(y - baseY);
        if (fh > 0) gfx_->drawFastVLine(x, fy, fh, fillCol);

        gfx_->drawLine(prevX, prevDrawY, x, y, accentColour);
        gfx_->drawLine(prevX, prevDrawY + 1, x, y + 1, accentColour);

        prevX = x;
        prevDrawY = y;
    }

    // --- SUSTAIN: flat line at sustain level ---
    // Fill the sustain region as a rectangle (much faster than per-pixel vlines)
    {
        const int16_t fy = min(sustY, baseY);
        const int16_t fh = abs(sustY - baseY);
        if (fh > 0) gfx_->fillRect(xS, fy, sustW, fh, fillCol);

        // Outline: 2px horizontal line
        gfx_->drawFastHLine(xS, sustY, sustW, accentColour);
        gfx_->drawFastHLine(xS, sustY + 1, sustW, accentColour);

        prevX = xR;
        prevDrawY = sustY;
    }

    // --- RELEASE: sustain level → baseline, shaped by release exponent ---
    for (uint8_t i = 1; i <= PTS_PER_SEG; ++i) {
        const float t = (float)i / PTS_PER_SEG;
        const float shaped = powf(t, expR);
        const int16_t x = xR + (int16_t)(t * rW);
        // Interpolate from sustY toward baseY
        const int16_t y = sustY + (int16_t)(shaped * (baseY - sustY));

        const int16_t fy = min(y, baseY);
        const int16_t fh = abs(y - baseY);
        if (fh > 0) gfx_->drawFastVLine(x, fy, fh, fillCol);

        gfx_->drawLine(prevX, prevDrawY, x, y, accentColour);
        gfx_->drawLine(prevX, prevDrawY + 1, x, y + 1, accentColour);

        prevX = x;
        prevDrawY = y;
    }

    // ── Vertex dots on the curve ────────────────────────────────────────────
    // Larger dots at the 5 ADSR vertices, accent-coloured.
    // Origin and end are slightly smaller/dimmer (they're fixed points).
    const uint16_t dimAccent = dimColour(accentColour, 1);

    gfx_->fillCircle(xA,   baseY,  3, dimAccent);   // origin (always at baseline)
    gfx_->fillCircle(xD,   peakY,  5, accentColour); // peak (end of attack)
    gfx_->fillCircle(xS,   sustY,  5, accentColour); // sustain start (end of decay)
    gfx_->fillCircle(xR,   sustY,  5, accentColour); // sustain end (start of release)
    gfx_->fillCircle(xEnd, baseY,  3, dimAccent);    // release end (returns to baseline)

    // ── Stage labels along the baseline ─────────────────────────────────────
    gfx_->setTextColor(COL_GRID);
    gfx_->setTextSize(1);

    // Centre each label under its segment
    auto printCentred = [&](uint16_t segX, uint16_t segW, const char* text) {
        const uint16_t textW = strlen(text) * 6;  // 6px per char at size 1
        gfx_->setCursor(segX + (segW - textW) / 2, botY + 4);
        gfx_->print(text);
    };

    printCentred(xA, aW,   "ATTACK");
    printCentred(xD, dW,   "DECAY");
    printCentred(xS, sustW, "SUSTAIN");
    printCentred(xR, rW,   "RELEASE");

    // ── Pitch depth label (PITCH sub-page only) ─────────────────────────────
    if (env.hasDepth) {
        gfx_->setTextColor(dimAccent);
        gfx_->setTextSize(1);
        gfx_->setCursor(drawR - 70, topY);
        gfx_->print("DEPTH ");
        if (env.depth > 0) gfx_->print("+");
        gfx_->print(env.depth);
    }
}

// =============================================================================
// SEQ page — full draw
// =============================================================================
// Draws 8 vertical bars representing step values, with a connecting line
// through the bar tops for visual flow.  Step numbers shown at baseline.

void DisplayRenderer::drawSequencerPage(const PageManager& pages) {
    const uint16_t accent = toRgb565(pages.activeMapping().ledColour);
    const uint8_t baseStep = (pages.potScene() == Scene::B) ? 8 : 0;

    // Draw all 8 bars
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t val = pages.getStepValue(baseStep + i);
        drawSequencerBar(i, val, accent, baseStep + i + 1);
        prevValues_[i] = val;
    }

    // Connecting line through bar tops
    const uint16_t padX     = 16;
    const uint16_t padTop   = 20;
    const uint16_t padBot   = 24;
    const uint16_t drawW    = SCREEN_W - 2 * padX;
    const uint16_t baselineY = PARAM_AREA_Y + PARAM_AREA_H - padBot;
    const uint16_t maxBarH  = PARAM_AREA_H - padTop - padBot;
    const uint16_t barGap   = 6;
    const uint16_t barW     = (drawW - 7 * barGap) / 8;
    const uint16_t dimAccent = dimColour(accent, 1);

    int16_t prevTopX = 0, prevTopY = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t val = pages.getStepValue(baseStep + i);
        const uint16_t barX = padX + i * (barW + barGap);
        const uint16_t barH = (uint32_t)val * maxBarH / 127;
        const int16_t topX = (int16_t)(barX + barW / 2);
        const int16_t topY = (int16_t)(baselineY - barH);

        if (i > 0) {
            gfx_->drawLine(prevTopX, prevTopY, topX, topY, dimAccent);
            gfx_->drawLine(prevTopX, prevTopY + 1, topX, topY + 1, dimAccent);
        }
        prevTopX = topX;
        prevTopY = topY;
    }

    // Baseline
    gfx_->drawFastHLine(padX, baselineY, drawW, COL_GRID);
}

// =============================================================================
// SEQ page — differential refresh
// =============================================================================

void DisplayRenderer::refreshSequencerPage(const PageManager& pages) {
    const uint8_t baseStep = (pages.potScene() == Scene::B) ? 8 : 0;
    bool anyChanged = false;

    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t val = pages.getStepValue(baseStep + i);
        if (val != prevValues_[i]) {
            anyChanged = true;
            break;
        }
    }
    if (!anyChanged) return;

    // Erase param area and redraw (connecting line means full redraw)
    const uint16_t accent = toRgb565(pages.activeMapping().ledColour);
    gfx_->fillRect(0, PARAM_AREA_Y, SCREEN_W, PARAM_AREA_H, COL_BG);
    drawSequencerPage(pages);
}

// =============================================================================
// SEQ page — single bar
// =============================================================================
// Draws one rounded vertical bar + step number label.

void DisplayRenderer::drawSequencerBar(uint8_t idx, uint8_t value,
                                       uint16_t accent, uint8_t stepNum) {
    const uint16_t padX     = 16;
    const uint16_t padTop   = 20;
    const uint16_t padBot   = 24;
    const uint16_t drawW    = SCREEN_W - 2 * padX;
    const uint16_t baselineY = PARAM_AREA_Y + PARAM_AREA_H - padBot;
    const uint16_t maxBarH  = PARAM_AREA_H - padTop - padBot;
    const uint16_t barGap   = 6;
    const uint16_t barW     = (drawW - 7 * barGap) / 8;
    const uint16_t barX     = padX + idx * (barW + barGap);
    const uint16_t barH     = (uint32_t)value * maxBarH / 127;
    const uint16_t barY     = baselineY - barH;

    // Filled bar with rounded top (radius 3px)
    if (barH > 6) {
        gfx_->fillRoundRect(barX, barY, barW, barH, 3, accent);
    } else if (barH > 0) {
        gfx_->fillRect(barX, barY, barW, barH, accent);
    }

    // Thin outline for empty bars (shows slot position even at value 0)
    if (barH == 0) {
        gfx_->drawRoundRect(barX, baselineY - 4, barW, 4, 1,
                            dimColour(accent, 2));
    }

    // Step number label below baseline
    gfx_->setTextColor(COL_LABEL);
    gfx_->setTextSize(1);
    const uint16_t labelX = barX + (barW / 2) - (stepNum >= 10 ? 6 : 3);
    gfx_->setCursor(labelX, baselineY + 6);
    gfx_->print(stepNum);
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