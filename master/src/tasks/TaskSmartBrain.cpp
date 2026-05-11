// =============================================================================
// tasks/TaskSmartBrain.cpp
// PURPOSE : Smart Brain decision task — Phase 3.
//
// Decision tree (runs every 60 s):
//   1. FAULT CHECK  — PLC disconnected or sensor invalid → do nothing
//   2. MANUAL CHECK — user explicitly turned ON → do nothing (respect intent)
//   3. DATA CHECK   — histogram not reliable yet (< 7 days / peak < 3) → skip
//   4. WINDOW CHECK — are we within lead_time minutes before predicted shower?
//      YES → fire AUTO_PREHEAT: set boilerOn=true in UiSnapshot
//   5. COOLDOWN     — boiler has been auto-ON for > cooldown window → turn off
//
// The task writes to the UiSnapshot queue.  TaskBrain (250 ms) reads it and
// generates the matching PWM commands to the slave via PLC.
// The user can always override by pressing ON/OFF on the touchscreen.
// =============================================================================

#include "tasks/TaskSmartBrain.h"

#include <Arduino.h>
#include <time.h>
#include "task_config.h"
#include "shared/master_state.h"
#include "brain/shower_histogram.h"
#include "brain/event_log.h"
#include "brain/heatup_tracker.h"
#include "brain/brain_settings.h"
#include "ui/ui_manager.h"

// ---------------------------------------------------------------------------
//  Internal state
// ---------------------------------------------------------------------------
namespace {

// True when the smart brain (not the user) last set boilerOn = true.
bool s_autoActive       = false;

// Tick at which we last fired AUTO_PREHEAT, used for cooldown.
TickType_t s_autoStartTick = 0u;

// Maximum minutes we keep the boiler running from a smart-brain trigger
// (safety net — prevents it heating forever if pattern shifts).
constexpr uint32_t AUTO_COOLDOWN_MIN = 120u;

// Helper: current minute-of-day (0..1439).
uint16_t minuteOfDay() {
    time_t now = time(nullptr);
    if (now < 100000L) return 0xFFFFu;  // NTP not synced yet
    struct tm t{};
    localtime_r(&now, &t);
    return (uint16_t)(t.tm_hour * 60 + t.tm_min);
}

// Helper: circular distance in minutes on a 1440-min clock.
// Returns how many minutes until 'target' from 'current', wrapping midnight.
int16_t minutesUntil(uint16_t current, uint16_t target) {
    int16_t diff = (int16_t)target - (int16_t)current;
    if (diff < 0) diff += 1440;
    return diff;
}

} // namespace

// ---------------------------------------------------------------------------
//  TaskSmartBrain
// ---------------------------------------------------------------------------
void TaskSmartBrain(void* pvParameters) {
    (void)pvParameters;
    Serial.println("[SMART] Task started");

    TickType_t lastWakeTick = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(TASK_SMART_BRAIN_PERIOD_MS));

        const TickType_t now = xTaskGetTickCount();

        // -----------------------------------------------------------------
        // 1. FAULT CHECK — need a valid sensor snapshot and PLC link
        // -----------------------------------------------------------------
        SensorSnapshot sensor{};
        const bool hasSensor = MasterState_ReadSensorSnapshot(sensor) && sensor.valid;
        if (!hasSensor) {
            Serial.println("[SMART] No sensor data — skip");
            continue;
        }
        const bool plcOk = (now - sensor.receivedAtTick) <
                            pdMS_TO_TICKS(MASTER_PLC_LINK_TIMEOUT_MS);
        if (!plcOk) {
            Serial.println("[SMART] PLC link lost — skip");
            continue;
        }

        // -----------------------------------------------------------------
        // 2. MANUAL CHECK — if user explicitly turned ON, stay out of the way
        // -----------------------------------------------------------------
        UiSnapshot ui{};
        const bool hasUi = MasterState_ReadUiSnapshot(ui) && ui.valid;

        if (hasUi && ui.boilerOn && !s_autoActive) {
            // User is in manual control — do not interfere
            continue;
        }

        // -----------------------------------------------------------------
        // 3. DATA CHECK — skip if histogram not reliable YET, but bypass
        //    entirely when a fixed ready-by time is set (Phase 6).
        // -----------------------------------------------------------------
        uint16_t readyByMin = 0u;
        const bool hasReadyBy = BrainSettings::getReadyByForToday(readyByMin);

        if (!hasReadyBy && !ShowerHistogram::isReliable()) {
            uint8_t  slot  = 0u;
            uint16_t count = 0u;
            ShowerHistogram::findPeak(slot, count);
            Serial.printf("[SMART] Histogram not reliable yet "
                          "(peak=%u sessions, days=%u) — skip\n",
                          count, ShowerHistogram::daysObserved());
            continue;
        }

        // -----------------------------------------------------------------
        // 4. WINDOW CHECK — determine target minute and lead time
        // -----------------------------------------------------------------
        uint8_t  peakSlot  = 0u;
        uint16_t peakCount = 0u;
        ShowerHistogram::findPeak(peakSlot, peakCount);

        // Ready-by override (Phase 6) takes priority over histogram peak.
        const uint16_t peakMinute = hasReadyBy
                                    ? readyByMin
                                    : ShowerHistogram::slotCentreMinutes(peakSlot);

        // Base lead time from HeatupTracker (adaptive, Phase 4)
        uint16_t leadMin = (uint16_t)HeatupTracker::getLeadTimeMinutes();

        // Weather adjustment (Phase 5):
        //   inlet_temp = 15°C at 20°C outdoor, shifts 0.3°C per outdoor degree
        //   colder inlet → longer heat-up → add ~2 min per °C below baseline
        const float outdoorC   = UI_GetOutdoorTempC();
        const float inletC     = 15.0f + (outdoorC - 20.0f) * 0.3f;
        const float deltaInlet = 15.0f - inletC;  // positive = colder than baseline
        const int8_t weatherAdj = (int8_t)(deltaInlet * 2.0f);  // 2 min/°C
        if (weatherAdj != 0) {
            const int16_t adjusted = (int16_t)leadMin + weatherAdj;
            leadMin = (adjusted < 10) ? 10u : (uint16_t)adjusted;  // floor 10 min
            EventLog::append(BoilerEvent::WEATHER_ADJUST, outdoorC);
            Serial.printf("[SMART] Weather adj: outdoor=%.1f°C inlet=%.1f°C adj=%+d min → lead=%u min\n",
                          outdoorC, inletC, (int)weatherAdj, leadMin);
        }
        const uint16_t startMinute  = (peakMinute >= leadMin)
                                      ? peakMinute - leadMin
                                      : peakMinute + 1440u - leadMin;

        const uint16_t nowMinute = minuteOfDay();
        if (nowMinute == 0xFFFFu) {
            Serial.println("[SMART] NTP not synced — skip");
            continue;
        }

        const int16_t minsToStart = minutesUntil(nowMinute, startMinute);
        const int16_t minsToPeak  = minutesUntil(nowMinute, peakMinute);

        // We are inside the preheat window when:
        //   minsToStart <= 0  (start time passed) AND minsToPeak > 0 (peak not yet reached)
        // minsToStart wraps, so check: we passed startMinute but haven't passed peakMinute.
        const bool inWindow = (minsToStart <= 0 || minsToStart >= 1440 - (int16_t)TASK_SMART_BRAIN_LEAD_MIN)
                              && (minsToPeak > 0 && minsToPeak < (int16_t)TASK_SMART_BRAIN_LEAD_MIN + 5);

        char peakStr[6];
        ShowerHistogram::slotToString(peakSlot, peakStr, sizeof(peakStr));

        // -----------------------------------------------------------------
        // 5. COOLDOWN — auto boiler has run long enough, shut it down
        // -----------------------------------------------------------------
        if (s_autoActive) {
            const uint32_t onMinutes =
                (uint32_t)((now - s_autoStartTick) / pdMS_TO_TICKS(60000UL));

            if (!inWindow || onMinutes >= AUTO_COOLDOWN_MIN) {
                // Turn boiler off (smart brain control)
                UiSnapshot off{};
                off.boilerOn         = false;
                off.targetShowerTempC = hasUi ? ui.targetShowerTempC : 60.0f;
                off.updatedAtTick    = now;
                off.valid            = true;
                MasterState_PublishUiSnapshot(off);

                s_autoActive = false;
                Serial.printf("[SMART] Auto OFF — peak=%s on=%u min\n",
                              peakStr, onMinutes);
            }
            continue;
        }

        // -----------------------------------------------------------------
        // 6. FIRE — enter preheat window, turn boiler ON
        // -----------------------------------------------------------------
        if (inWindow) {
            UiSnapshot on{};
            on.boilerOn          = true;
            on.targetShowerTempC = hasUi ? ui.targetShowerTempC : 60.0f;
            on.updatedAtTick     = now;
            on.valid             = true;
            MasterState_PublishUiSnapshot(on);

            s_autoActive    = true;
            s_autoStartTick = now;
            HeatupTracker::startSession();

            EventLog::append(BoilerEvent::AUTO_PREHEAT, (float)peakMinute);

            Serial.printf("[SMART] AUTO_PREHEAT fired — peak=%s lead=%u min\n",
                          peakStr, (unsigned)leadMin);
        } else {
            Serial.printf("[SMART] Standby — peak=%s in %d min\n",
                          peakStr, minsToPeak);
        }
    }
}
