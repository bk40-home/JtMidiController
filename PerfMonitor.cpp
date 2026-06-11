// =============================================================================
// PerfMonitor.cpp
// =============================================================================
#include "PerfMonitor.h"
#include "Config.h"

namespace PerfMonitor {

// ── Internal state ─────────────────────────────────────────────────────────
// File-scope static so it isn't part of the public header. All access happens
// from this translation unit only, so no synchronisation is needed (loop is
// single-threaded from the user's point of view).

namespace {
    uint32_t loopStartUs_     = 0;          // micros() at most recent loopBegin
    uint32_t prevLoopStartUs_ = 0;          // micros() at previous loopBegin
    uint32_t accumPeriodUs_   = 0;          // sum of loop-to-loop periods
    uint32_t maxLoopUs_       = 0;          // worst single-loop time
    uint32_t loopCount_       = 0;          // iterations since last report
    uint32_t lastReportMs_    = 0;          // millis() of last report
    uint32_t minFreeHeap_     = UINT32_MAX; // lowest DRAM watermark seen
    bool     primed_          = false;      // skip first period (no prev sample)
}

// ── Public API ─────────────────────────────────────────────────────────────

void begin() {
    loopStartUs_     = micros();
    prevLoopStartUs_ = loopStartUs_;
    lastReportMs_    = millis();
    minFreeHeap_     = ESP.getFreeHeap();
    accumPeriodUs_   = 0;
    maxLoopUs_       = 0;
    loopCount_       = 0;
    primed_          = false;

    Serial.printf("[PERF] monitor started — heap=%lu KB psram=%lu KB\n",
                  (unsigned long)(ESP.getFreeHeap() / 1024),
                  (unsigned long)(ESP.getFreePsram() / 1024));
}

void loopBegin() {
    prevLoopStartUs_ = loopStartUs_;
    loopStartUs_     = micros();

    // First call after begin() has no valid previous sample — skip the period
    // accumulator until we have two timestamps. Avoids a bogus first-window
    // average that includes setup-to-loop transition time.
    if (primed_) {
        accumPeriodUs_ += (loopStartUs_ - prevLoopStartUs_);
    }
    primed_ = true;
}

void loopEnd() {
    // Per-iteration busy time = now − loopBegin. Tracks the worst spike so a
    // single bad frame (e.g. full envelope redraw) shows up even when buried
    // in the average.
    const uint32_t now  = micros();
    const uint32_t busy = now - loopStartUs_;
    if (busy > maxLoopUs_) maxLoopUs_ = busy;
    loopCount_++;

    // DRAM watermark — cheap to read every loop, catches transient peaks that
    // a once-per-report snapshot would miss.
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < minFreeHeap_) minFreeHeap_ = freeHeap;

    // Emit the report line on cadence, then reset window-scoped accumulators.
    // minFreeHeap_ persists across windows (it's a lifetime watermark).
    if (millis() - lastReportMs_ >= Config::PERF_REPORT_MS) {
        // Average loop time over the window (in microseconds)
        const uint32_t avgLoopUs = (loopCount_ > 0)
            ? (accumPeriodUs_ / loopCount_) : 0;

        // Loop frequency — derive from total wall time and iteration count.
        // Float ok here — printed once per report, not in a hot path.
        const float loopHz = (accumPeriodUs_ > 0)
            ? 1.0e6f * (float)loopCount_ / (float)accumPeriodUs_
            : 0.0f;

        const uint32_t heapNow   = ESP.getFreeHeap();
        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t psramNow  = ESP.getFreePsram();
        const uint32_t psramTot  = ESP.getPsramSize();

        Serial.printf("[PERF] %.0fHz avg=%luus max=%.1fms "
                      "heap=%lu/%luKB(low:%luKB) psram=%.1f/%.1fMB\n",
                      loopHz,
                      (unsigned long)avgLoopUs,
                      maxLoopUs_ / 1000.0f,
                      (unsigned long)(heapNow / 1024),
                      (unsigned long)(heapTotal / 1024),
                      (unsigned long)(minFreeHeap_ / 1024),
                      psramNow / (1024.0f * 1024.0f),
                      psramTot / (1024.0f * 1024.0f));

        lastReportMs_  = millis();
        accumPeriodUs_ = 0;
        maxLoopUs_     = 0;
        loopCount_     = 0;
    }
}

void mark(const char* tag) {
    Serial.printf("[PERF/MARK] %s heap=%lu KB psram=%lu KB\n",
                  tag,
                  (unsigned long)(ESP.getFreeHeap() / 1024),
                  (unsigned long)(ESP.getFreePsram() / 1024));
}

} // namespace PerfMonitor
