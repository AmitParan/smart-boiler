#ifndef BRAIN_SHOWER_HISTOGRAM_H
#define BRAIN_SHOWER_HISTOGRAM_H

// =============================================================================
// brain/shower_histogram.h
// PURPOSE : 24-hour usage histogram — 96 slots of 15 minutes each.
//           Each slot counts how many times a shower started in that window.
//           Used by BrainTask (Phase 3) to predict the next shower time.
//
// File layout on SPIFFS (/brain/histogram.bin):
//   Bytes 0-1  : uint16_t total_sessions  (total number of FLOW_START events seen)
//   Bytes 2-3  : uint16_t days_observed   (calendar days with at least 1 shower)
//   Bytes 4..  : SLOTS × uint16_t         (count per 15-min slot, LE)
//   Total      : 4 + 96×2 = 196 bytes
//
// Slot index = (hour × 60 + minute) / 15  →  0..95
// =============================================================================

#include <stdint.h>

class ShowerHistogram {
public:
    static constexpr uint8_t  SLOTS         = 96u;   // 24 h × 4 slots/h
    static constexpr uint16_t SLOT_MINUTES  = 15u;
    static constexpr uint16_t RELIABLE_MIN  = 3u;    // peak needs ≥ 3 hits
    static constexpr uint16_t DAYS_MIN      = 7u;    // need ≥ 7 days of data
    static constexpr const char* FILE_PATH  = "/brain/histogram.bin";

    // Call once at boot, after EventLog::init().
    static bool init();

    // Increment the slot corresponding to the given unix timestamp.
    // Call this on every FLOW_START event.
    static void recordShower(uint32_t unixTimestamp);

    // Convenience: increment using current time (time(nullptr)).
    static void recordShowerNow();

    // Find the slot with the highest count.
    // Returns false if histogram is empty.
    static bool findPeak(uint8_t& peakSlot, uint16_t& peakCount);

    // True when data is sufficient to trust the pattern.
    // Requires: peak count ≥ RELIABLE_MIN AND days_observed ≥ DAYS_MIN.
    static bool isReliable();

    // Convert a slot index to the minute-of-day at its centre.
    // e.g. slot 28 → 28×15 + 7 = 427 min = 07:07
    static uint16_t slotCentreMinutes(uint8_t slot);

    // Convert slot to human-readable "HH:MM" string (buf must be ≥ 6 bytes).
    static void slotToString(uint8_t slot, char* buf, uint8_t bufLen);

    // Erase all counts. Used for factory reset.
    static void clear();

    // Read-only accessors for diagnostics / Phase 3
    static uint16_t totalSessions()  { return s_total;   }
    static uint16_t daysObserved()   { return s_days;    }
    static uint16_t slotCount(uint8_t slot) {
        return (slot < SLOTS) ? s_counts[slot] : 0u;
    }

private:
    static uint16_t s_counts[SLOTS];
    static uint16_t s_total;   // cumulative shower starts
    static uint16_t s_days;    // distinct calendar days seen

    static bool     s_ready;

    static bool persistToDisk();
    static uint8_t  timestampToSlot(uint32_t unixTimestamp);
};

#endif // BRAIN_SHOWER_HISTOGRAM_H
