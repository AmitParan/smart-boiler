#ifndef BRAIN_EVENT_LOG_H
#define BRAIN_EVENT_LOG_H

// =============================================================================
// brain/event_log.h
// PURPOSE : Circular SPIFFS event log — records timestamped boiler events.
//           Used by the Smart Brain to learn user habits over time.
// BRANCH  : feature/smart-brain
// =============================================================================

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Event types
// ---------------------------------------------------------------------------
enum class BoilerEvent : uint8_t {
    USER_ON          = 0x01,  // user pressed ON on touchscreen
    USER_OFF         = 0x02,  // user pressed OFF
    FLOW_START       = 0x03,  // flow crossed threshold (tap opened)
    FLOW_STOP        = 0x04,  // flow dropped to zero (tap closed)
    TEMP_SET         = 0x05,  // user changed target shower temperature
    AUTO_PREHEAT     = 0x06,  // brain auto-started pre-heat
    TANK_READY       = 0x07,  // tank reached target temperature
    TARGET_TIME_SET  = 0x08,  // user set a fixed ready-by time
    WEATHER_ADJUST   = 0x09,  // brain adjusted lead time due to weather
};

// ---------------------------------------------------------------------------
//  Event record — 7 bytes, packed to match SPIFFS layout exactly.
//  unixTimestamp: seconds since epoch (from NTP-synced RTC)
//  valueX10:      context value × 10 (e.g. temp 55.2°C → 552, flow 7.5 → 75)
//                 0 for events with no numeric context
// ---------------------------------------------------------------------------
#pragma pack(1)
struct BoilerEventRecord {
    uint32_t    unixTimestamp;  // 4 bytes
    BoilerEvent type;           // 1 byte
    int16_t     valueX10;       // 2 bytes  (signed: temp can be negative)
};
#pragma pack()

static_assert(sizeof(BoilerEventRecord) == 7u,
              "BoilerEventRecord must be exactly 7 bytes");

// ---------------------------------------------------------------------------
//  EventLog — circular log stored in SPIFFS at /brain/events.bin
//
//  Capacity: MAX_EVENTS records.
//  When full, oldest record is overwritten (ring buffer).
//  Survives reboots. DataManager::init() must be called before EventLog::init().
// ---------------------------------------------------------------------------
class EventLog {
public:
    static constexpr uint16_t MAX_EVENTS = 512u;   // 512 × 7 = 3584 bytes
    static constexpr const char* FILE_PATH = "/brain/events.bin";

    // Call once at boot, after DataManager::init()
    static bool init();

    // Append a new event.  Returns false if SPIFFS write fails.
    static bool append(BoilerEvent type, float value = 0.0f);

    // Read all stored events into caller-supplied buffer.
    // Returns the number of valid records written into 'out'.
    static uint16_t readAll(BoilerEventRecord* out, uint16_t maxCount);

    // Total events stored (may be less than MAX_EVENTS early in life).
    static uint16_t count();

    // Remove all events. Used for factory reset.
    static void clear();

private:
    static uint16_t s_head;   // index of next write position (0-based)
    static uint16_t s_count;  // number of valid records currently stored
    static bool     s_ready;  // true after successful init()

    static bool writeRecord(uint16_t index, const BoilerEventRecord& rec);
    static bool readRecord(uint16_t index, BoilerEventRecord& rec);
};

#endif // BRAIN_EVENT_LOG_H
