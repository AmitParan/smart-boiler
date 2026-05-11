// =============================================================================
// brain/shower_histogram.cpp
// PURPOSE : SPIFFS-backed 96-slot 24-hour shower usage histogram.
// =============================================================================

#include "preheat/shower_histogram.h"
#include <SPIFFS.h>
#include <Arduino.h>
#include <time.h>

// ---------------------------------------------------------------------------
//  Static member definitions
// ---------------------------------------------------------------------------
uint16_t ShowerHistogram::s_counts[ShowerHistogram::SLOTS] = {};
uint16_t ShowerHistogram::s_total  = 0u;
uint16_t ShowerHistogram::s_days   = 0u;
bool     ShowerHistogram::s_ready  = false;

// File layout constants
static constexpr uint32_t HIST_HEADER_BYTES = 4u;   // total_sessions + days_observed
static constexpr uint32_t HIST_DATA_BYTES   =
    (uint32_t)ShowerHistogram::SLOTS * sizeof(uint16_t);  // 96 × 2 = 192
static constexpr uint32_t HIST_FILE_SIZE    =
    HIST_HEADER_BYTES + HIST_DATA_BYTES;                  // 196 bytes

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool ShowerHistogram::init() {
    s_ready = false;
    memset(s_counts, 0, sizeof(s_counts));
    s_total = 0u;
    s_days  = 0u;

    if (!SPIFFS.begin(false)) {
        Serial.println("[HIST] SPIFFS not mounted");
        return false;
    }

    if (!SPIFFS.exists(FILE_PATH)) {
        // First boot — create zero-filled file
        File f = SPIFFS.open(FILE_PATH, "w");
        if (!f) {
            Serial.println("[HIST] Cannot create histogram.bin");
            return false;
        }
        uint8_t zero[HIST_FILE_SIZE] = {};
        f.write(zero, HIST_FILE_SIZE);
        f.close();
        Serial.printf("[HIST] Created %s (%u bytes)\n", FILE_PATH, (unsigned)HIST_FILE_SIZE);
    } else {
        // Load existing data
        File f = SPIFFS.open(FILE_PATH, "r");
        if (!f) {
            Serial.println("[HIST] Cannot open histogram.bin");
            return false;
        }

        // Read header: total_sessions (LE uint16), days_observed (LE uint16)
        uint8_t hdr[4] = {};
        if (f.read(hdr, 4) != 4) { f.close(); return false; }
        s_total = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
        s_days  = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);

        // Read slot counts
        for (uint8_t i = 0; i < SLOTS; i++) {
            uint8_t buf[2] = {};
            if (f.read(buf, 2) != 2) { f.close(); return false; }
            s_counts[i] = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        }
        f.close();

        Serial.printf("[HIST] Loaded: %u sessions over %u days\n", s_total, s_days);
    }

    s_ready = true;
    return true;
}

// ---------------------------------------------------------------------------
//  recordShower
// ---------------------------------------------------------------------------
void ShowerHistogram::recordShower(uint32_t unixTimestamp) {
    if (!s_ready || unixTimestamp == 0u) return;

    const uint8_t slot = timestampToSlot(unixTimestamp);

    // Saturate at uint16_t max to avoid overflow
    if (s_counts[slot] < 0xFFFFu) s_counts[slot]++;
    if (s_total         < 0xFFFFu) s_total++;

    // Track distinct calendar days using the day-of-year.
    // We keep a simple running count: increment s_days whenever the
    // day-of-year differs from the most recently recorded day.
    struct tm t{};
    time_t ts = (time_t)unixTimestamp;
    localtime_r(&ts, &t);
    const uint16_t dayOfYear = (uint16_t)(t.tm_yday);

    // Store last-seen day in the top half of a static variable.
    static uint16_t s_lastDayOfYear = 0xFFFFu;
    if (dayOfYear != s_lastDayOfYear) {
        s_lastDayOfYear = dayOfYear;
        if (s_days < 0xFFFFu) s_days++;
    }

    persistToDisk();

    Serial.printf("[HIST] Recorded slot %u (%u hits) | total=%u days=%u\n",
                  slot, s_counts[slot], s_total, s_days);
}

void ShowerHistogram::recordShowerNow() {
    recordShower((uint32_t)time(nullptr));
}

// ---------------------------------------------------------------------------
//  findPeak
// ---------------------------------------------------------------------------
bool ShowerHistogram::findPeak(uint8_t& peakSlot, uint16_t& peakCount) {
    peakSlot  = 0u;
    peakCount = 0u;

    for (uint8_t i = 0; i < SLOTS; i++) {
        if (s_counts[i] > peakCount) {
            peakCount = s_counts[i];
            peakSlot  = i;
        }
    }
    return (peakCount > 0u);
}

// ---------------------------------------------------------------------------
//  isReliable
// ---------------------------------------------------------------------------
bool ShowerHistogram::isReliable() {
    if (!s_ready) return false;
    uint8_t  slot  = 0u;
    uint16_t count = 0u;
    if (!findPeak(slot, count)) return false;
    return (count >= RELIABLE_MIN) && (s_days >= DAYS_MIN);
}

// ---------------------------------------------------------------------------
//  slotCentreMinutes / slotToString
// ---------------------------------------------------------------------------
uint16_t ShowerHistogram::slotCentreMinutes(uint8_t slot) {
    return (uint16_t)slot * SLOT_MINUTES + SLOT_MINUTES / 2u;
}

void ShowerHistogram::slotToString(uint8_t slot, char* buf, uint8_t bufLen) {
    const uint16_t mins = slotCentreMinutes(slot);
    snprintf(buf, bufLen, "%02u:%02u", mins / 60u, mins % 60u);
}

// ---------------------------------------------------------------------------
//  clear
// ---------------------------------------------------------------------------
void ShowerHistogram::clear() {
    memset(s_counts, 0, sizeof(s_counts));
    s_total = 0u;
    s_days  = 0u;
    persistToDisk();
    Serial.println("[HIST] Cleared");
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------
bool ShowerHistogram::persistToDisk() {
    File f = SPIFFS.open(FILE_PATH, "r+");
    if (!f) return false;

    // Write header
    uint8_t hdr[4];
    hdr[0] = (uint8_t)(s_total & 0xFF);
    hdr[1] = (uint8_t)(s_total >> 8);
    hdr[2] = (uint8_t)(s_days  & 0xFF);
    hdr[3] = (uint8_t)(s_days  >> 8);
    f.seek(0, SeekSet);
    f.write(hdr, 4);

    // Write slot counts
    for (uint8_t i = 0; i < SLOTS; i++) {
        uint8_t buf[2];
        buf[0] = (uint8_t)(s_counts[i] & 0xFF);
        buf[1] = (uint8_t)(s_counts[i] >> 8);
        f.write(buf, 2);
    }
    f.close();
    return true;
}

uint8_t ShowerHistogram::timestampToSlot(uint32_t unixTimestamp) {
    struct tm t{};
    time_t ts = (time_t)unixTimestamp;
    localtime_r(&ts, &t);
    const uint16_t minuteOfDay = (uint16_t)(t.tm_hour * 60 + t.tm_min);
    return (uint8_t)(minuteOfDay / SLOT_MINUTES);  // 0..95
}
