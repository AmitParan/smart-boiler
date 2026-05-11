// =============================================================================
// brain/brain_settings.cpp
// PURPOSE : Persist and retrieve "Ready By" target times.
// =============================================================================

#include "preheat/preheat_settings.h"
#include <SPIFFS.h>
#include <Arduino.h>
#include <time.h>

// ---------------------------------------------------------------------------
//  Static member definitions
// ---------------------------------------------------------------------------
bool     PreheatSettings::s_weekdayEnabled = false;
uint16_t PreheatSettings::s_weekdayMin     = 0u;
bool     PreheatSettings::s_weekendEnabled = false;
uint16_t PreheatSettings::s_weekendMin     = 0u;

static constexpr uint32_t FILE_BYTES = 6u;

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool PreheatSettings::init() {
    s_weekdayEnabled = false;
    s_weekdayMin     = 0u;
    s_weekendEnabled = false;
    s_weekendMin     = 0u;

    if (!SPIFFS.begin(false)) {
        Serial.println("[SETTINGS] SPIFFS not mounted");
        return false;
    }

    if (!SPIFFS.exists(FILE_PATH)) {
        File f = SPIFFS.open(FILE_PATH, "w");
        if (!f) {
            Serial.println("[SETTINGS] Cannot create settings.bin");
            return false;
        }
        uint8_t zero[FILE_BYTES] = {};
        f.write(zero, FILE_BYTES);
        f.close();
        Serial.printf("[SETTINGS] Created %s\n", FILE_PATH);
    } else {
        File f = SPIFFS.open(FILE_PATH, "r");
        if (!f) return false;

        uint8_t buf[FILE_BYTES] = {};
        if (f.read(buf, FILE_BYTES) != FILE_BYTES) { f.close(); return false; }
        f.close();

        s_weekdayEnabled = (buf[0] != 0u);
        s_weekdayMin     = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        s_weekendEnabled = (buf[3] != 0u);
        s_weekendMin     = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);

        // Clamp to valid range
        if (s_weekdayMin > 1439u) s_weekdayMin = 0u;
        if (s_weekendMin > 1439u) s_weekendMin = 0u;

        char wdStr[6], weStr[6];
        minuteToString(s_weekdayMin, wdStr, sizeof(wdStr));
        minuteToString(s_weekendMin, weStr, sizeof(weStr));
        Serial.printf("[SETTINGS] Loaded: weekday=%s(%s) weekend=%s(%s)\n",
                      s_weekdayEnabled ? "ON" : "off", wdStr,
                      s_weekendEnabled ? "ON" : "off", weStr);
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Setters
// ---------------------------------------------------------------------------
void PreheatSettings::setWeekdayReadyBy(uint16_t minuteOfDay) {
    s_weekdayEnabled = true;
    s_weekdayMin     = (minuteOfDay < 1440u) ? minuteOfDay : 0u;
    persistToDisk();
    char buf[6];
    minuteToString(s_weekdayMin, buf, sizeof(buf));
    Serial.printf("[SETTINGS] Weekday ready-by set: %s\n", buf);
}

void PreheatSettings::clearWeekdayReadyBy() {
    s_weekdayEnabled = false;
    persistToDisk();
    Serial.println("[SETTINGS] Weekday ready-by cleared");
}

void PreheatSettings::setWeekendReadyBy(uint16_t minuteOfDay) {
    s_weekendEnabled = true;
    s_weekendMin     = (minuteOfDay < 1440u) ? minuteOfDay : 0u;
    persistToDisk();
    char buf[6];
    minuteToString(s_weekendMin, buf, sizeof(buf));
    Serial.printf("[SETTINGS] Weekend ready-by set: %s\n", buf);
}

void PreheatSettings::clearWeekendReadyBy() {
    s_weekendEnabled = false;
    persistToDisk();
    Serial.println("[SETTINGS] Weekend ready-by cleared");
}

// ---------------------------------------------------------------------------
//  getReadyByForToday
// ---------------------------------------------------------------------------
bool PreheatSettings::getReadyByForToday(uint16_t& out_min) {
    time_t now = time(nullptr);
    if (now < 100000L) return false;  // NTP not synced

    struct tm t{};
    localtime_r(&now, &t);
    const bool isWeekend = (t.tm_wday == 0 || t.tm_wday == 6);  // Sun=0, Sat=6

    if (isWeekend && s_weekendEnabled) {
        out_min = s_weekendMin;
        return true;
    }
    if (!isWeekend && s_weekdayEnabled) {
        out_min = s_weekdayMin;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
void PreheatSettings::minuteToString(uint16_t min, char* buf, uint8_t bufLen) {
    snprintf(buf, bufLen, "%02u:%02u", min / 60u, min % 60u);
}

bool PreheatSettings::persistToDisk() {
    File f = SPIFFS.open(FILE_PATH, "r+");
    if (!f) return false;

    uint8_t buf[FILE_BYTES];
    buf[0] = s_weekdayEnabled ? 1u : 0u;
    buf[1] = (uint8_t)(s_weekdayMin & 0xFF);
    buf[2] = (uint8_t)(s_weekdayMin >> 8);
    buf[3] = s_weekendEnabled ? 1u : 0u;
    buf[4] = (uint8_t)(s_weekendMin & 0xFF);
    buf[5] = (uint8_t)(s_weekendMin >> 8);

    f.seek(0, SeekSet);
    f.write(buf, FILE_BYTES);
    f.close();
    return true;
}
