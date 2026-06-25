// =============================================================================
// TouchInput.cpp
// =============================================================================
#include "TouchInput.h"

// SensorLib — only the FT6X36 touch driver is pulled in here, not the whole
// library. Matches the include the Waveshare demo uses.
#include "TouchDrvFT6X36.hpp"

TouchInput::TouchInput() = default;

TouchInput::~TouchInput() {
    delete drv_;   // safe on nullptr if begin() was never called
}

bool TouchInput::begin(TwoWire* wire) {
    wire_ = wire;

    // Optional touch INT pin. -1 = disabled → pure gated polling (no wiring
    // needed). If set, poll() consults it to skip needless bus reads while
    // idle. We register it with the driver too (some SensorLib revisions use
    // it internally) AND read it ourselves in poll() for the bus-skip gate.
    drv_ = new TouchDrvFT6X36();
    if (Config::TOUCH_INT_PIN >= 0) {
        pinMode((uint8_t)Config::TOUCH_INT_PIN, INPUT_PULLUP);
        // setPins(rst, int) — this board ties RST to the TCA9554 expander, so
        // pass -1 for reset and only hand over the INT line.
        drv_->setPins(-1, (int)Config::TOUCH_INT_PIN);
    }

    // begin(wire, addr) — bus already initialised by the caller (the .ino runs
    // Wire.begin() before any unit begin()). FT6X36_SLAVE_ADDRESS is 0x38,
    // matching Config::TOUCH_ADDR; we pass the Config constant for one source
    // of truth.
    present_ = drv_->begin(*wire_, Config::TOUCH_ADDR);
    if (!present_) {
        delete drv_;
        drv_ = nullptr;
    }

    // Seed the poll gate to "now" so the first poll() waits a full interval
    // rather than firing immediately at boot (lets the controller settle after
    // the display reset sequence before the first read).
    lastPollMs_ = millis();

    return present_;
}

void TouchInput::poll() {
    if (!present_ || drv_ == nullptr) return;

    // ── Gate 1: optional INT pin ────────────────────────────────────────────
    // If a touch INT pin is wired, skip the bus read entirely while it is HIGH
    // (idle). FT6336 INT is active-low: a contact pulls it LOW. This makes an
    // idle screen cost nothing on the bus. Falls through to the time gate when
    // the pin signals activity, so bursts are still rate-limited.
    if (Config::TOUCH_INT_PIN >= 0) {
        if (digitalRead((uint8_t)Config::TOUCH_INT_PIN) == HIGH) return;
    }

    // ── Gate 2: time ────────────────────────────────────────────────────────
    // Do not read more often than Config::TOUCH_POLL_MS. Returns with zero bus
    // traffic when the interval hasn't elapsed — nothing calculated or read.
    const uint32_t now = millis();
    if (now - lastPollMs_ < Config::TOUCH_POLL_MS) return;
    lastPollMs_ = now;

    // ── Read up to two points from the controller ──────────────────────────
    // getPoint() returns the live contact count and fills the arrays with raw
    // coordinates in the panel's native portrait frame.
    int16_t rx[kMaxPoints] = {0, 0};
    int16_t ry[kMaxPoints] = {0, 0};
    uint8_t count = drv_->getPoint(rx, ry, kMaxPoints);
    if (count > kMaxPoints) count = kMaxPoints;   // defensive clamp

    // ── Transform every active point to the landscape frame ────────────────
    pointCount_ = count;
    for (uint8_t i = 0; i < kMaxPoints; ++i) {
        if (i < count) {
            // getPoint() yields non-negative coords for valid contacts; cast
            // through uint16_t after the count check so an inactive slot's
            // stale value never reaches the transform.
            transformPoint((uint16_t)rx[i], (uint16_t)ry[i], px_[i], py_[i]);
        } else {
            px_[i] = 0;
            py_[i] = 0;
        }
    }

    // ── Latch a tap on the primary contact's press (touch-down) edge ────────
    // Edge = was-not-touched → now-touched. Capture the landing position so a
    // tab/cell hit-test uses where the finger LANDED, not where it drifts to.
    const bool nowTouched = (count > 0);
    if (nowTouched && !prevTouched_) {
        tapLatched_ = true;
        tapX_ = px_[0];
        tapY_ = py_[0];
    }
    prevTouched_ = nowTouched;
}

uint16_t TouchInput::x(uint8_t idx) const {
    return (idx < kMaxPoints) ? px_[idx] : 0;
}

uint16_t TouchInput::y(uint8_t idx) const {
    return (idx < kMaxPoints) ? py_[idx] : 0;
}

// ── Coordinate transform: native portrait → landscape (rotation = 3) ────────
// The display is initialised at rotation=3, mapping the native 320(W)×480(H)
// portrait panel to a 480(W)×320(H) landscape canvas. The FT6336 reports raw
// coordinates in native portrait. For rotation=3 the standard mapping is:
//     landscapeX = NATIVE_H - 1 - nativeY
//     landscapeY = nativeX
// If a tap registers mirrored or 90° off on hardware, this ONE function is the
// only place to adjust — nothing else in the firmware knows the native frame.
// (The Waveshare demo runs rotation=0 and needs no transform, so this is the
// expected first thing to tune when moving to the landscape layout.)
void TouchInput::transformPoint(uint16_t nx, uint16_t ny,
                                uint16_t& outX, uint16_t& outY) {
    // Clamp raw inputs to the native range before mapping, so a noisy
    // out-of-range sample can't produce a wild landscape coordinate.
    if (nx >= Config::TOUCH_NATIVE_W) nx = Config::TOUCH_NATIVE_W - 1;
    if (ny >= Config::TOUCH_NATIVE_H) ny = Config::TOUCH_NATIVE_H - 1;

    outX = (Config::TOUCH_NATIVE_H - 1) - ny;  // 0..479
    outY = nx;                                  // 0..319
}
