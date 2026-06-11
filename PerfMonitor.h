// =============================================================================
// PerfMonitor.h — runtime loop-rate and memory monitoring
// =============================================================================
// Tracks loop frequency, per-iteration timing, and DRAM/PSRAM usage. Prints a
// one-line summary to Serial every Config::PERF_REPORT_MS milliseconds.
//
// USAGE:
//   setup() {
//       Serial.begin(...);
//       PerfMonitor::begin();           // after Serial is up
//       ...
//   }
//   loop() {
//       PerfMonitor::loopBegin();       // top of loop
//       ... loop body ...
//       PerfMonitor::loopEnd();         // bottom of loop — prints on cadence
//   }
//
// OUTPUT FORMAT (one line, ~95 chars):
//   [PERF] 1247Hz avg=802us max=18.2ms heap=187/229KB(low:172) psram=8.0/8.0MB
//
//   Hz       — average loop iterations per second over the report window
//   avg      — mean wall-clock time per loop iteration
//   max      — worst-case single iteration time in the window
//   heap     — current free / total DRAM in KB, plus lowest watermark since boot
//   psram    — current free / total PSRAM in MB
//
// WHY NOT A "CPU %"?
//   The Arduino main loop runs continuously with no idle gap, so a naive
//   busy/period ratio always reads ~100%. Loop frequency + worst-case latency
//   are the actually useful numbers on this architecture — a sudden Hz drop
//   or max spike points straight at a subsystem that just got expensive.
// =============================================================================
#pragma once

#include <Arduino.h>

namespace PerfMonitor {

// Initialise tracking. Call once from setup() after Serial.begin().
void begin();

// Call at the top of loop(). Records timestamps for period/busy tracking.
void loopBegin();

// Call at the bottom of loop(). Accumulates timing stats and prints the
// report line if Config::PERF_REPORT_MS has elapsed since the last report.
void loopEnd();

// Log a free-heap snapshot with a tag (e.g. before/after expensive init).
// Useful for spotting one-off allocations during startup.
void mark(const char* tag);

} // namespace PerfMonitor
