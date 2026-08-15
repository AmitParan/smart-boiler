#include "smart_preheat.h"
#include "DataManager.h"
#include <Arduino.h>
#include <time.h>

// =============================================================================
//  Smart Preheat implementation — see smart_preheat.h for the design.
// =============================================================================

namespace {

// ---- Learning model: 24h histogram, 15-min slots ----
constexpr uint8_t  SLOTS         = 96u;   // 24 h * 4 slots/h
constexpr uint16_t SLOT_MINUTES  = 15u;
constexpr uint16_t RELIABLE_HITS = 3u;    // busiest slot must be seen >= 3 times
constexpr uint16_t RELIABLE_DAYS = 7u;    // and we need >= 7 days of data

uint16_t s_counts[SLOTS] = {0};
uint16_t s_total   = 0u;   // total showers recorded
uint16_t s_days    = 0u;   // distinct calendar days with >= 1 shower
int16_t  s_lastYday = -1;  // day-of-year of the last recorded shower (day counter)

// ---- Decision-tree tuning ----
// NOTE: BASE_LEAD_MIN is a fixed approximation for V1. Heating a fully-cooled
// 150 L tank to 40 C can take ~1.4 h from cold (project book), less if recently
// used. V2 should replace this with an adaptive lead measured from real heat-up
// sessions (HeatupTracker). 45 min is a pragmatic default for a daily pattern.
constexpr uint16_t BASE_LEAD_MIN    = 45u;   // start preheat this many minutes early
constexpr uint16_t PER_PERSON_MIN   = 5u;    // + minutes per person above 2
constexpr uint16_t MAX_LEAD_MIN     = 90u;
constexpr uint16_t SHOWER_GRACE_MIN = 45u;   // stay ready this long AFTER target (covers the shower)
constexpr float    PREHEAT_TARGET_C = 40.0f; // tank base target (matches SystemManager)
constexpr float    SAFETY_TEMP_C    = 80.0f; // brain stands down at/above this (defense in depth)

bool s_wantsHeat = false;

uint8_t minuteToSlot(uint16_t minuteOfDay) {
    return (uint8_t)((minuteOfDay % 1440u) / SLOT_MINUTES);
}

bool findPeak(uint8_t& peakSlot, uint16_t& peakCount) {
    peakSlot = 0u; peakCount = 0u;
    for (uint8_t i = 0u; i < SLOTS; i++) {
        if (s_counts[i] > peakCount) { peakCount = s_counts[i]; peakSlot = i; }
    }
    return peakCount > 0u;
}

// Minutes from 'nowMin' forward to 'targetMin' on a 1440-min clock (0..1439).
uint16_t minutesUntil(uint16_t nowMin, uint16_t targetMin) {
    int32_t d = (int32_t)targetMin - (int32_t)nowMin;
    if (d < 0) d += 1440;
    return (uint16_t)d;
}

uint16_t leadFor(uint8_t household) {
    uint16_t lead = BASE_LEAD_MIN;
    if (household > 2u) lead += (uint16_t)(household - 2u) * PER_PERSON_MIN;
    if (lead > MAX_LEAD_MIN) lead = MAX_LEAD_MIN;
    return lead;
}

} // namespace

void SmartPreheat::init() {
    if (DataManager::loadPreheat(s_counts, SLOTS, s_total, s_days, s_lastYday)) {
        Serial.printf("[BRAIN] Loaded history: %u showers over %u days\n", s_total, s_days);
    } else {
        Serial.println("[BRAIN] No saved history — starting fresh");
    }
}

void SmartPreheat::recordShower(uint32_t unixNow) {
    if (unixNow < 100000UL) return;  // clock not synced — cannot place the event in time
    time_t t = (time_t)unixNow;
    struct tm tmv;
    localtime_r(&t, &tmv);

    uint16_t minuteOfDay = (uint16_t)(tmv.tm_hour * 60 + tmv.tm_min);
    uint8_t  slot = minuteToSlot(minuteOfDay);

    if (s_counts[slot] < 0xFFFFu) s_counts[slot]++;
    if (s_total       < 0xFFFFu) s_total++;
    if (tmv.tm_yday != s_lastYday) { s_days++; s_lastYday = (int16_t)tmv.tm_yday; }

    Serial.printf("[BRAIN] Shower @ %02d:%02d (slot %u, count %u) | total=%u days=%u\n",
                  tmv.tm_hour, tmv.tm_min, slot, s_counts[slot], s_total, s_days);

    DataManager::savePreheat(s_counts, SLOTS, s_total, s_days, s_lastYday);
}

bool SmartPreheat::isReliable() {
    uint8_t slot; uint16_t count;
    if (!findPeak(slot, count)) return false;
    return (count >= RELIABLE_HITS) && (s_days >= RELIABLE_DAYS);
}

uint16_t SmartPreheat::predictedMinute() {
    uint8_t slot; uint16_t count;
    if (!findPeak(slot, count)) return 0xFFFFu;
    return (uint16_t)(slot * SLOT_MINUTES + SLOT_MINUTES / 2u);  // centre of the slot
}

uint16_t SmartPreheat::daysObserved() { return s_days; }
uint16_t SmartPreheat::totalShowers() { return s_total; }

// -----------------------------------------------------------------------------
//  The decision tree (runs ~every 60 s).
// -----------------------------------------------------------------------------
void SmartPreheat::update(const PreheatInputs& in) {
    s_wantsHeat = false;  // default: request nothing

    // 1. Brain is idle in DUMB mode, or when the link/clock is unusable.
    if (in.mode == OP_DUMB)        return;
    if (!in.plcConnected)          return;   // PLC link lost (>5s) — stand down
    if (in.unixNow < 100000UL)     return;   // NTP clock not synced yet

    // 2. Safety first: if the tank is already hot, stand aside and let the
    //    controller's hard protections handle it (SystemManager cuts at 85 C,
    //    slave software at 80 C). Never request preheat into an over-temp.
    if (in.tankTempC >= SAFETY_TEMP_C) return;

    // 3. Respect the user: a manual ON means they are in control — stay out.
    if (in.manualOn)               return;

    // 3. Build the list of candidate shower times for the active mode.
    //    READY_BY: every enabled slot (Morning/Evening). SMART: the learned peak.
    uint16_t targets[4];
    uint8_t  nt = 0;
    if (in.mode == OP_READY_BY) {
        for (uint8_t i = 0; i < in.readyByCount && nt < 4; i++) {
            targets[nt++] = in.readyByMinutes[i] % 1440u;
        }
        if (nt == 0) return;                 // no enabled slots — nothing to do
    } else { // OP_SMART
        if (!isReliable())         return;   // still learning — do nothing yet
        uint16_t p = predictedMinute();
        if (p == 0xFFFFu)          return;
        targets[nt++] = p;
    }

    // 4. Heat if we are inside ANY target's window [target - lead, target + grace].
    //    - lead:  heat ahead so the tank is warm by the shower time.
    //    - grace: stay on through the shower so the boost heater can fire even
    //             if the shower starts a little later than predicted.
    time_t t = (time_t)in.unixNow;
    struct tm tmv; localtime_r(&t, &tmv);
    uint16_t nowMinute = (uint16_t)(tmv.tm_hour * 60 + tmv.tm_min);
    uint16_t lead      = leadFor(in.household);

    for (uint8_t i = 0; i < nt; i++) {
        uint16_t untilTarget = minutesUntil(nowMinute, targets[i]);  // >0 before target
        uint16_t sinceTarget = minutesUntil(targets[i], nowMinute);  // >0 after target
        if (untilTarget <= lead || sinceTarget <= SHOWER_GRACE_MIN) {
            // SystemManager regulates to 40 C and drops to STANDBY once reached,
            // so the tank cannot overheat here.
            s_wantsHeat = true;
            break;
        }
    }
    (void)PREHEAT_TARGET_C;  // documented target; regulation lives in SystemManager
}

bool SmartPreheat::wantsHeat() { return s_wantsHeat; }
