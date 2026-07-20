// =============================================================================
// HomePanel.cpp — see HomePanel.h for the design rationale.
// =============================================================================
#include "HomePanel.h"
#include <Arduino_GFX_Library.h>
#include <stdio.h>

namespace JtView {

namespace {
    constexpr uint16_t C_BG     = 0x0000;
    constexpr uint16_t C_TEXT   = 0xFFFF;
    constexpr uint16_t C_ACCENT = 0xFD20;   // JT orange
    constexpr uint16_t C_DIM    = 0x8410;   // labels, inactive outlines
    constexpr uint16_t C_LINK   = 0x07E0;   // green — the link-alive dot

    constexpr int16_t kNameY   = 62;
    constexpr int16_t kSlotY   = 100;
    constexpr int16_t kLabelVolY = 138;
    constexpr int16_t kVoiceY  = 214;   // dot centre line
    constexpr int16_t kLabelVoiceY = 192;
    constexpr int16_t kSeqY    = 262;
    constexpr int16_t kSeqH    = 16;
    constexpr int16_t kLabelSeqY = 244;
}

void HomePanel::draw(const State& s) {
    if (!gfx_) return;

    if (dirty_) {
        // One background clear for the whole content area — HOME owns it all
        // (no rows). After this, everything is per-element.
        gfx_->fillRect(0, 46, 480, 320 - 46, C_BG);

        gfx_->setTextSize(1);
        gfx_->setTextColor(C_DIM);
        gfx_->setCursor(kVolX, kLabelVolY);    gfx_->print("VOLUME");
        gfx_->setCursor(kVolX, kLabelVoiceY);  gfx_->print("VOICES");
        gfx_->setCursor(kVolX, kLabelSeqY);    gfx_->print("SEQ");
    }

    const bool all = dirty_;
    dirty_ = false;

    if (all || strncmp(s.name, lastName_, sizeof(lastName_) - 1) != 0
            || s.slot != lastSlot_) {
        drawName(s);
        strncpy(lastName_, s.name, sizeof(lastName_) - 1);
        lastName_[sizeof(lastName_) - 1] = '\0';
        lastSlot_ = s.slot;
    }
    if (all || s.link != lastLink_) {
        drawLink(s.link);
        lastLink_ = s.link;
    }
    if (all || s.volume != lastVol_) {
        drawVolume(s.volume);
        lastVol_ = s.volume;
    }
    if (all || s.mask != lastMask_) {
        drawVoices(s.mask);
        lastMask_ = s.mask;
    }
    if (all || s.step != lastStep_ || s.running != lastRun_) {
        drawSeq(s.step, s.running);
        lastStep_ = s.step;
        lastRun_  = s.running;
    }
}

void HomePanel::drawName(const State& s) {
    // Name band cleared as one rect: the name is variable-width text and a
    // shorter name must not leave the old one's tail behind.
    gfx_->fillRect(0, kNameY - 2, 440, 70, C_BG);

    gfx_->setTextSize(4);
    gfx_->setTextColor(C_ACCENT);
    gfx_->setCursor(kVolX, kNameY);
    gfx_->print(s.name[0] ? s.name : "INIT");

    gfx_->setTextSize(2);
    gfx_->setTextColor(C_DIM);
    gfx_->setCursor(kVolX, kSlotY);
    if (s.slot == 0xFF) {
        gfx_->print("UNSAVED");
    } else {
        char buf[12];
        snprintf(buf, sizeof(buf), "SLOT %u", s.slot + 1);
        gfx_->print(buf);
    }
}

void HomePanel::drawLink(bool lit) {
    // Alive = the engine spoke recently (any rx, including the status feed
    // itself). Dark link on a powered rig is the first thing to check.
    gfx_->fillCircle(456, 66, 6, lit ? C_LINK : C_BG);
    gfx_->drawCircle(456, 66, 6, lit ? C_LINK : C_DIM);
}

void HomePanel::drawVolume(float v) {
    const int16_t fill = static_cast<int16_t>(v * static_cast<float>(kVolW - 4));
    gfx_->drawRect(kVolX, kVolY, kVolW, kVolH, C_DIM);
    gfx_->fillRect(static_cast<int16_t>(kVolX + 2), static_cast<int16_t>(kVolY + 2),
                   fill, static_cast<int16_t>(kVolH - 4), C_ACCENT);
    gfx_->fillRect(static_cast<int16_t>(kVolX + 2 + fill),
                   static_cast<int16_t>(kVolY + 2),
                   static_cast<int16_t>(kVolW - 4 - fill),
                   static_cast<int16_t>(kVolH - 4), C_BG);
}

void HomePanel::drawVoices(uint8_t mask) {
    // 8 dots, MAX_VOICES on the engine. Lit = that voice is sounding NOW,
    // straight from the status feed — this is the engine's truth, not an echo
    // of what the controller sent.
    for (uint8_t i = 0; i < 8; ++i) {
        const int16_t cx = static_cast<int16_t>(kVolX + 10 + i * 30);
        const bool on = (mask >> i) & 1u;
        gfx_->fillCircle(cx, kVoiceY, 8, on ? C_ACCENT : C_BG);
        gfx_->drawCircle(cx, kVoiceY, 8, on ? C_ACCENT : C_DIM);
    }
}

void HomePanel::drawSeq(uint8_t step, bool running) {
    // 16 ticks; the playhead tick fills while the sequencer runs. All-dim
    // when stopped — a stopped sequencer showing a lit step would read as
    // stuck, not stopped.
    const int16_t tw = kVolW / 16;
    for (uint8_t i = 0; i < 16; ++i) {
        const int16_t tx = static_cast<int16_t>(kVolX + i * tw);
        const bool lit = running && (i == (step & 0x0F));
        gfx_->fillRect(tx, kSeqY, static_cast<int16_t>(tw - 3), kSeqH,
                       lit ? C_ACCENT : C_BG);
        gfx_->drawRect(tx, kSeqY, static_cast<int16_t>(tw - 3), kSeqH,
                       lit ? C_ACCENT : C_DIM);
    }
}

} // namespace JtView
