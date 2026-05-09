// =============================================================================
// brain/event_log.cpp
// PURPOSE : Circular SPIFFS event log implementation.
// =============================================================================

#include "brain/event_log.h"
#include <SPIFFS.h>
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Static member definitions
// ---------------------------------------------------------------------------
uint16_t EventLog::s_head  = 0u;
uint16_t EventLog::s_count = 0u;
bool     EventLog::s_ready = false;

// File header: first 4 bytes store head and count so they survive reboots.
// Layout: [uint16_t head][uint16_t count][record 0][record 1]...
static constexpr uint32_t HEADER_SIZE  = 4u;
static constexpr uint32_t RECORD_SIZE  = sizeof(BoilerEventRecord);  // 7
static constexpr uint32_t FILE_SIZE    =
    HEADER_SIZE + (EventLog::MAX_EVENTS * RECORD_SIZE);  // 4 + 3584 = 3588

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool EventLog::init() {
    // SPIFFS must already be mounted (DataManager::init() does this)
    if (!SPIFFS.begin(false)) {
        Serial.println("[BRAIN] EventLog: SPIFFS not mounted");
        return false;
    }

    // Create /brain/ directory implicitly by checking the file
    if (!SPIFFS.exists(FILE_PATH)) {
        // First boot — create empty file, zero-filled
        File f = SPIFFS.open(FILE_PATH, "w");
        if (!f) {
            Serial.println("[BRAIN] EventLog: cannot create events.bin");
            return false;
        }
        // Write header: head=0, count=0
        uint8_t hdr[HEADER_SIZE] = {0, 0, 0, 0};
        f.write(hdr, HEADER_SIZE);
        // Pad the rest to full file size so random-access reads work
        uint8_t zero[RECORD_SIZE] = {};
        for (uint16_t i = 0; i < MAX_EVENTS; i++) {
            f.write(zero, RECORD_SIZE);
        }
        f.close();
        Serial.printf("[BRAIN] EventLog: created %s (%u bytes)\n",
                      FILE_PATH, (unsigned)FILE_SIZE);
        s_head = 0u;
        s_count = 0u;
    } else {
        // Existing file — read back head and count
        File f = SPIFFS.open(FILE_PATH, "r");
        if (!f) {
            Serial.println("[BRAIN] EventLog: cannot open events.bin");
            return false;
        }
        uint8_t hdr[HEADER_SIZE];
        f.read(hdr, HEADER_SIZE);
        f.close();
        s_head  = (uint16_t)(hdr[0] | (hdr[1] << 8));
        s_count = (uint16_t)(hdr[2] | (hdr[3] << 8));
        // Sanity check
        if (s_head >= MAX_EVENTS || s_count > MAX_EVENTS) {
            Serial.println("[BRAIN] EventLog: corrupt header, resetting");
            s_head = 0u; s_count = 0u;
        }
        Serial.printf("[BRAIN] EventLog: loaded — %u events, head=%u\n",
                      s_count, s_head);
    }

    s_ready = true;
    return true;
}

// ---------------------------------------------------------------------------
//  append
// ---------------------------------------------------------------------------
bool EventLog::append(BoilerEvent type, float value) {
    if (!s_ready) return false;

    BoilerEventRecord rec;
    rec.unixTimestamp = (uint32_t)(time(nullptr));  // NTP-synced unix time
    rec.type          = type;
    rec.valueX10      = (int16_t)(value * 10.0f);

    if (!writeRecord(s_head, rec)) return false;

    // Advance ring buffer
    s_head = (uint16_t)((s_head + 1u) % MAX_EVENTS);
    if (s_count < MAX_EVENTS) s_count++;

    // Persist header
    File f = SPIFFS.open(FILE_PATH, "r+");
    if (!f) return false;
    uint8_t hdr[HEADER_SIZE] = {
        (uint8_t)(s_head  & 0xFF), (uint8_t)(s_head  >> 8),
        (uint8_t)(s_count & 0xFF), (uint8_t)(s_count >> 8)
    };
    f.write(hdr, HEADER_SIZE);
    f.close();

    const char* label = "UNKNOWN";
    switch (type) {
        case BoilerEvent::USER_ON:         label = "USER_ON";         break;
        case BoilerEvent::USER_OFF:        label = "USER_OFF";        break;
        case BoilerEvent::FLOW_START:      label = "FLOW_START";      break;
        case BoilerEvent::FLOW_STOP:       label = "FLOW_STOP";       break;
        case BoilerEvent::TEMP_SET:        label = "TEMP_SET";        break;
        case BoilerEvent::AUTO_PREHEAT:    label = "AUTO_PREHEAT";    break;
        case BoilerEvent::TANK_READY:      label = "TANK_READY";      break;
        case BoilerEvent::TARGET_TIME_SET: label = "TARGET_TIME_SET"; break;
        case BoilerEvent::WEATHER_ADJUST:  label = "WEATHER_ADJUST";  break;
    }
    Serial.printf("[BRAIN] Event logged: %s  value=%.1f  total=%u\n",
                  label, value, s_count);
    return true;
}

// ---------------------------------------------------------------------------
//  readAll
// ---------------------------------------------------------------------------
uint16_t EventLog::readAll(BoilerEventRecord* out, uint16_t maxCount) {
    if (!s_ready || s_count == 0u) return 0u;

    uint16_t toRead = (s_count < maxCount) ? s_count : maxCount;
    // Oldest record is at (s_head - s_count + MAX_EVENTS) % MAX_EVENTS
    uint16_t start = (uint16_t)((s_head + MAX_EVENTS - s_count) % MAX_EVENTS);

    for (uint16_t i = 0u; i < toRead; i++) {
        uint16_t idx = (uint16_t)((start + i) % MAX_EVENTS);
        readRecord(idx, out[i]);
    }
    return toRead;
}

// ---------------------------------------------------------------------------
//  count / clear
// ---------------------------------------------------------------------------
uint16_t EventLog::count() { return s_count; }

void EventLog::clear() {
    s_head = 0u; s_count = 0u;
    SPIFFS.remove(FILE_PATH);
    init();
    Serial.println("[BRAIN] EventLog: cleared");
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------
bool EventLog::writeRecord(uint16_t index, const BoilerEventRecord& rec) {
    File f = SPIFFS.open(FILE_PATH, "r+");
    if (!f) return false;
    uint32_t offset = HEADER_SIZE + ((uint32_t)index * RECORD_SIZE);
    f.seek(offset);
    f.write(reinterpret_cast<const uint8_t*>(&rec), RECORD_SIZE);
    f.close();
    return true;
}

bool EventLog::readRecord(uint16_t index, BoilerEventRecord& rec) {
    File f = SPIFFS.open(FILE_PATH, "r");
    if (!f) return false;
    uint32_t offset = HEADER_SIZE + ((uint32_t)index * RECORD_SIZE);
    f.seek(offset);
    f.read(reinterpret_cast<uint8_t*>(&rec), RECORD_SIZE);
    f.close();
    return true;
}
