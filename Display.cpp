// =============================================================================
// Display.cpp — see Display.h.
// =============================================================================
#include "Display.h"

#include "Config.h"
#include "TCA9554.h"

static Arduino_DataBus* s_bus = nullptr;
static Arduino_GFX*     s_gfx = nullptr;
static TCA9554          s_tca;

// The panel's RST line hangs off the TCA9554 expander, not off a GPIO — which
// is why Config::DISP_RST is -1 and the ST7796 driver is told it has no reset
// pin. The pulse below is the demo's exact timing; shortening the 200 ms tail
// gives an intermittently blank panel on cold boot.
static void resetPanel() {
    s_tca.begin();
    s_tca.pinMode1(Config::TCA_RST_PIN, OUTPUT);

    s_tca.write1(Config::TCA_RST_PIN, 1); delay(10);
    s_tca.write1(Config::TCA_RST_PIN, 0); delay(10);
    s_tca.write1(Config::TCA_RST_PIN, 1); delay(200);
}

Arduino_GFX* jtDisplayBegin() {
    if (s_gfx) return s_gfx;      // idempotent

    resetPanel();

    s_bus = new Arduino_ESP32SPI(
        Config::DISP_DC,      // DC
        Config::DISP_CS,      // CS   (-1: directly wired)
        Config::DISP_SCLK,
        Config::DISP_MOSI,
        Config::DISP_MISO);

    // Declared in NATIVE portrait (320x480) with rotation 3 -> 480x320
    // landscape. IPS = true. Matches the working demo exactly.
    s_gfx = new Arduino_ST7796(
        s_bus,
        Config::DISP_RST,     // -1: reset is via the TCA9554, see resetPanel()
        3,                    // rotation
        true,                 // IPS
        Config::DISP_WIDTH,   // 320 native
        Config::DISP_HEIGHT); // 480 native

    if (!s_gfx->begin()) {
        Serial.println("[TFT] ST7796 init failed");
        s_gfx = nullptr;
        return nullptr;
    }

    s_gfx->fillScreen(0x0000);

    pinMode(Config::DISP_BL, OUTPUT);
    digitalWrite(Config::DISP_BL, HIGH);

    Serial.println("[TFT] ST7796 480x320 ready");
    return s_gfx;
}
