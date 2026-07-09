#include "safety_task.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include "boiler_protocol.h"
#include <Arduino.h>

// PLC loss is only a fault after first CMD is received (avoids boot-time false trip)
#define PLC_TIMEOUT_MS  5000u

void TaskSafety(void* pvParameters) {
    Serial.println("[SAFETY] Task started");

    // For demo-mode auto-clear: track when system_fault was latched
    static uint32_t fault_latch_ms = 0u;

    for (;;) {
        bool fault = false;

        // ---- Snapshot shared data under mutexes --------------------------------
        float local_temps[3] = {0.0f, 0.0f, 0.0f};
        float local_flow     = 0.0f;
        float local_current  = 0.0f;
        uint8_t local_pwm_int = 0u, local_pwm_bst = 0u, local_flags = 0u;

        if (xSemaphoreTake(mutex_temps, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_temps[0] = temps[0];
            local_temps[1] = temps[1];
            local_temps[2] = temps[2];
            xSemaphoreGive(mutex_temps);
        }
        if (xSemaphoreTake(mutex_flow, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_flow = current_flow;
            xSemaphoreGive(mutex_flow);
        }
        if (xSemaphoreTake(mutex_current, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_current = current_rms;
            xSemaphoreGive(mutex_current);
        }
        if (xSemaphoreTake(mutex_cmd, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_pwm_int = cmd_pwm_internal;
            local_pwm_bst = cmd_pwm_boost;
            local_flags   = cmd_flags;
            xSemaphoreGive(mutex_cmd);
        }

        // -------------------------------------------------------------------
        //  1. Temperature overheat protection
        //     Always active regardless of SystemMode.
        // -------------------------------------------------------------------
        for (int i = 0; i < 3; i++) {
            if (local_temps[i] > 80.0f) {
                Serial.printf("[SAFETY] FAULT: Overheat sensor[%d] = %.1fC\n",
                              i, local_temps[i]);
                fault = true;
            }
        }

        // -------------------------------------------------------------------
        //  2 & 3. Flow interlock + uncommanded current
        //     Active in MODE_DEMO and MODE_PRODUCTION.
        //     Bypassed only in legacy MODE_BENCH_TEST.
        // -------------------------------------------------------------------
        if (currentMode != MODE_BENCH_TEST) {

            // 2. Boost heater flow interlock
            bool boost_commanded = (local_flags & CMD_BOOST_ENABLE) && (local_pwm_bst > 0u);
            if (boost_commanded && local_flow < 1.0f) {
                Serial.println("[SAFETY] FAULT: Boost commanded with no flow!");
                fault = true;
            }

            // 3. Uncommanded current detection (SSR short / stuck triac)
            bool any_commanded = (local_pwm_int > 0u) || (local_pwm_bst > 0u);
            if (!any_commanded && local_current > 0.5f) {
                Serial.println("[SAFETY] FAULT: Uncommanded current detected - possible SSR fault!");
                fault = true;
            }

            // 4. PLC watchdog: fault if no CMD received for > PLC_TIMEOUT_MS
            //    (scenario 6 test: master stops transmitting)
            if (cmd_ever_received && (millis() - last_cmd_received_ms > PLC_TIMEOUT_MS)) {
                Serial.println("[SAFETY] FAULT: PLC timeout - no CMD for >5s");
                fault = true;
            }
        }

        if (fault) {
            ledcWrite(PIN_SSR_INT, 0);
            ledcWrite(PIN_SSR_EXT, 0);
            if (!system_fault) {
                system_fault   = true;
                fault_latch_ms = millis();
            }
        }

        // -------------------------------------------------------------------
        //  Demo-mode auto-clear
        //  In MODE_DEMO, safety faults are temporary (2 s) so the automated
        //  test bench can continue through multiple scenarios without a reboot.
        //  In production the latch is permanent (requires hardware reboot).
        // -------------------------------------------------------------------
        if (system_fault && currentMode == MODE_DEMO) {
            if (millis() - fault_latch_ms > 2000UL) {
                system_fault   = false;
                fault_latch_ms = 0u;
                Serial.println("[SAFETY] Demo fault auto-cleared");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
