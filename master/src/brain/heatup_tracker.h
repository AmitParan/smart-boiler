#pragma once

// =============================================================================
// brain/heatup_tracker.h
// PURPOSE : Tracks real heat-up duration per session (Phase 4).
//           Stores the last HISTORY_SIZE measured durations as a ring buffer
//           in SPIFFS and returns a rolling average used as the adaptive
//           lead time in TaskSmartBrain.
//
// File layout (/brain/heatup.bin) — 12 bytes:
//   Byte 0    : uint8_t head    (next write index, 0..HISTORY_SIZE-1)
//   Byte 1    : uint8_t count   (valid entries, 0..HISTORY_SIZE)
//   Bytes 2.. : uint16_t dur[HISTORY_SIZE]  (minutes each, LE)
// =============================================================================

#include <stdint.h>

class HeatupTracker {
public:
    static constexpr uint8_t     HISTORY_SIZE  = 5u;
    static constexpr uint8_t     MARGIN_MIN    = 5u;   // safety margin added to avg
    static constexpr uint8_t     DEFAULT_MIN   = 30u;  // fallback before data exists
    static constexpr uint16_t    MAX_VALID_MIN = 180u; // ignore outliers > 3 h
    static constexpr const char* FILE_PATH     = "/brain/heatup.bin";

    // Call once at boot, after EventLog/Histogram init.
    static bool init();

    // Call when the boiler is turned ON for heating (AUTO_PREHEAT or manual).
    // Starts an in-RAM timer. Not persisted — session must complete to count.
    static void startSession();

    // Call when TANK_READY fires. Computes duration since startSession(),
    // appends to the ring buffer, and persists.
    // Silently ignored if startSession() was never called this boot.
    static void endSession();

    // Returns the rolling average of the last HISTORY_SIZE durations plus
    // MARGIN_MIN. Falls back to DEFAULT_MIN if no data yet.
    static uint8_t getLeadTimeMinutes();

    // True if at least one session has been recorded.
    static bool hasData() { return s_count > 0u; }

    static uint8_t sessionCount() { return s_count; }

private:
    static uint16_t s_durations[HISTORY_SIZE];
    static uint8_t  s_head;
    static uint8_t  s_count;
    static bool     s_ready;

    // Tick when startSession() was last called (0 = not started).
    static uint32_t s_startTick;

    static bool persistToDisk();
};
