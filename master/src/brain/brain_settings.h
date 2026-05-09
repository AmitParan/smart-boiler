#pragma once

// =============================================================================
// brain/brain_settings.h
// PURPOSE : Persists user-configurable "Ready By" target times (Phase 6).
//           Two independent schedules: weekday (Mon-Fri) and weekend (Sat-Sun).
//           When set, TaskSmartBrain uses ready_by - lead_time instead of
//           the histogram peak to determine when to start pre-heating.
//
// File layout (/brain/settings.bin) — 6 bytes:
//   Byte 0   : uint8_t  weekday_enabled   (0 = off, 1 = on)
//   Bytes 1-2: uint16_t weekday_ready_min (minute-of-day, 0..1439, LE)
//   Byte 3   : uint8_t  weekend_enabled   (0 = off, 1 = on)
//   Bytes 4-5: uint16_t weekend_ready_min (minute-of-day, 0..1439, LE)
// =============================================================================

#include <stdint.h>

class BrainSettings {
public:
    static constexpr const char* FILE_PATH = "/brain/settings.bin";

    // Call once at boot after HeatupTracker::init()
    static bool init();

    // --- Weekday (Mon-Fri) ---
    static void    setWeekdayReadyBy(uint16_t minuteOfDay);
    static void    clearWeekdayReadyBy();
    static bool    weekdayEnabled()    { return s_weekdayEnabled; }
    static uint16_t weekdayReadyMin()  { return s_weekdayMin; }

    // --- Weekend (Sat-Sun) ---
    static void    setWeekendReadyBy(uint16_t minuteOfDay);
    static void    clearWeekendReadyBy();
    static bool    weekendEnabled()    { return s_weekendEnabled; }
    static uint16_t weekendReadyMin()  { return s_weekendMin; }

    // Returns true and sets out_min to the ready-by time for today.
    // Returns false if no schedule applies to today.
    static bool getReadyByForToday(uint16_t& out_min);

    // Helper: convert minute-of-day to "HH:MM" string (buf must be ≥ 6 bytes)
    static void minuteToString(uint16_t min, char* buf, uint8_t bufLen);

private:
    static bool     s_weekdayEnabled;
    static uint16_t s_weekdayMin;
    static bool     s_weekendEnabled;
    static uint16_t s_weekendMin;

    static bool persistToDisk();
};
