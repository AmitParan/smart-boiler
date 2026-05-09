// =============================================================================
// brain/heatup_tracker.cpp
// PURPOSE : Adaptive lead time — rolling average of real heat-up durations.
// =============================================================================

#include "brain/heatup_tracker.h"
#include <SPIFFS.h>
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Static member definitions
// ---------------------------------------------------------------------------
uint16_t HeatupTracker::s_durations[HeatupTracker::HISTORY_SIZE] = {};
uint8_t  HeatupTracker::s_head      = 0u;
uint8_t  HeatupTracker::s_count     = 0u;
bool     HeatupTracker::s_ready     = false;
uint32_t HeatupTracker::s_startTick = 0u;

static constexpr uint32_t FILE_BYTES =
    2u + (uint32_t)HeatupTracker::HISTORY_SIZE * sizeof(uint16_t);  // 12

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool HeatupTracker::init() {
    s_ready     = false;
    s_head      = 0u;
    s_count     = 0u;
    s_startTick = 0u;
    memset(s_durations, 0, sizeof(s_durations));

    if (!SPIFFS.begin(false)) {
        Serial.println("[HEATUP] SPIFFS not mounted");
        return false;
    }

    if (!SPIFFS.exists(FILE_PATH)) {
        File f = SPIFFS.open(FILE_PATH, "w");
        if (!f) {
            Serial.println("[HEATUP] Cannot create heatup.bin");
            return false;
        }
        uint8_t zero[FILE_BYTES] = {};
        f.write(zero, FILE_BYTES);
        f.close();
        Serial.printf("[HEATUP] Created %s (%u bytes)\n", FILE_PATH, (unsigned)FILE_BYTES);
    } else {
        File f = SPIFFS.open(FILE_PATH, "r");
        if (!f) return false;

        uint8_t hdr[2] = {};
        if (f.read(hdr, 2) != 2) { f.close(); return false; }
        s_head  = hdr[0];
        s_count = hdr[1];

        // Clamp
        if (s_head  >= HISTORY_SIZE) s_head  = 0u;
        if (s_count >  HISTORY_SIZE) s_count = HISTORY_SIZE;

        for (uint8_t i = 0; i < HISTORY_SIZE; i++) {
            uint8_t buf[2] = {};
            f.read(buf, 2);
            s_durations[i] = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        }
        f.close();

        Serial.printf("[HEATUP] Loaded %u session(s), lead=%u min\n",
                      s_count, getLeadTimeMinutes());
    }

    s_ready = true;
    return true;
}

// ---------------------------------------------------------------------------
//  startSession / endSession
// ---------------------------------------------------------------------------
void HeatupTracker::startSession() {
    s_startTick = (uint32_t)xTaskGetTickCount();
    Serial.println("[HEATUP] Session started");
}

void HeatupTracker::endSession() {
    if (s_startTick == 0u) return;  // startSession() never called

    const uint32_t nowTick  = (uint32_t)xTaskGetTickCount();
    const uint32_t elapsedMs = (nowTick - s_startTick) * portTICK_PERIOD_MS;
    const uint16_t elapsedMin = (uint16_t)(elapsedMs / 60000UL);

    s_startTick = 0u;

    // Ignore implausible values
    if (elapsedMin == 0u || elapsedMin > MAX_VALID_MIN) {
        Serial.printf("[HEATUP] Session ignored (duration=%u min)\n", elapsedMin);
        return;
    }

    s_durations[s_head] = elapsedMin;
    s_head = (s_head + 1u) % HISTORY_SIZE;
    if (s_count < HISTORY_SIZE) s_count++;

    persistToDisk();

    Serial.printf("[HEATUP] Session complete: %u min → lead=%u min\n",
                  elapsedMin, getLeadTimeMinutes());
}

// ---------------------------------------------------------------------------
//  getLeadTimeMinutes
// ---------------------------------------------------------------------------
uint8_t HeatupTracker::getLeadTimeMinutes() {
    if (s_count == 0u) return DEFAULT_MIN;

    uint32_t sum = 0u;
    for (uint8_t i = 0; i < s_count; i++) sum += s_durations[i];
    const uint32_t avg = sum / s_count;

    const uint32_t lead = avg + MARGIN_MIN;
    return (uint8_t)(lead > 255u ? 255u : lead);
}

// ---------------------------------------------------------------------------
//  persistToDisk
// ---------------------------------------------------------------------------
bool HeatupTracker::persistToDisk() {
    File f = SPIFFS.open(FILE_PATH, "r+");
    if (!f) return false;

    uint8_t hdr[2] = { s_head, s_count };
    f.seek(0, SeekSet);
    f.write(hdr, 2);

    for (uint8_t i = 0; i < HISTORY_SIZE; i++) {
        uint8_t buf[2];
        buf[0] = (uint8_t)(s_durations[i] & 0xFF);
        buf[1] = (uint8_t)(s_durations[i] >> 8);
        f.write(buf, 2);
    }
    f.close();
    return true;
}
