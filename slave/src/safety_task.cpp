#include "safety_task.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include "boiler_protocol.h"
#include <Arduino.h>

#define PLC_TIMEOUT_MS  5000u

void TaskSafety(void* pvParameters) {
    Serial.println("[SAFETY] Task started");

    static uint32_t fault_latch_ms      = 0u;
    static bool overheat_logged         = false;
    static bool hw_interlock_logged     = false;
    static bool plc_timeout_logged      = false;
    static bool uncommanded_curr_logged = false;

    for (;;) {
        bool fault = false;

        // ---- Snapshot shared data ----
        float local_temps[3] = {0.0f, 0.0f, 0.0f};
        float local_flow     = 0.0f;
        float local_current  = 0.0f;
        uint8_t local_pwm_int = 0u, local_pwm_bst = 0u, local_flags = 0u;

        if (xSemaphoreTake(guard_temps,   pdMS_TO_TICKS(5)) == pdTRUE) {
            local_temps[0] = temps[0]; local_temps[1] = temps[1]; local_temps[2] = temps[2];
            xSemaphoreGive(guard_temps);
        }
        if (xSemaphoreTake(guard_flow,    pdMS_TO_TICKS(5)) == pdTRUE) {
            local_flow = current_flow; xSemaphoreGive(guard_flow);
        }
        if (xSemaphoreTake(guard_current, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_current = current_rms; xSemaphoreGive(guard_current);
        }
        if (xSemaphoreTake(guard_cmd,     pdMS_TO_TICKS(5)) == pdTRUE) {
            local_pwm_int = cmd_pwm_internal;
            local_pwm_bst = cmd_pwm_boost;
            local_flags   = cmd_flags;
            xSemaphoreGive(guard_cmd);
        }

        // 1. Overheat (always active, both modes)
        for (int i = 0; i < 3; i++) {
            if (local_temps[i] > 80.0f) {
                fault = true;
                if (currentMode == MODE_DEMO) {
                    if (!overheat_logged) {
                        Serial.printf("[SLAVE] \xe2\x9a\xa0\xef\xb8\x8f SOFTWARE CUTOFF: Temp >= 80\xc2\xb0""C (sensor[%d]=%.1f\xc2\xb0""C). Dropping PWM to 0%%.\n",
                                      i, local_temps[i]);
                        overheat_logged = true;
                    }
                    if (local_temps[i] >= 86.0f && !hw_interlock_logged) {
                        Serial.println("[SLAVE] \xe2\x9d\x8c HARDWARE INTERLOCK TRIP! (LM393N Simulation) -> CURRENT FORCED TO 0.0A!");
                        hw_interlock_logged = true;
                    }
                } else {
                    Serial.printf("[SAFETY] FAULT: Overheat sensor[%d] = %.1fC\n", i, local_temps[i]);
                }
            }
        }

        // 2. Boost flow interlock (both modes; in DEMO, flow is mock-injected correctly)
        bool boost_commanded = (local_flags & CMD_BOOST_ENABLE) && (local_pwm_bst > 0u);
        if (boost_commanded && local_flow < 1.0f) {
            fault = true;
            if (currentMode == MODE_DEMO) {
                /* silent - not a demo scenario fault */
            } else {
                Serial.println("[SAFETY] FAULT: Boost commanded with no flow!");
            }
        }

        // 3. Uncommanded current
        //    DEMO:     always check - mock current data is accurate (no ADC involved)
        //    REALTIME: only check if sensor was properly calibrated (VREF in spec)
        //              If VREF was out of range at boot (ADC non-linearity / 5V supply issue)
        //              the raw readings are unreliable - skip to avoid phantom faults.
        bool any_commanded = (local_pwm_int > 0u) || (local_pwm_bst > 0u);
        bool do_current_check = (currentMode == MODE_DEMO) || current_sensor_valid;
        if (do_current_check && !any_commanded && local_current > 0.5f) {
            fault = true;
            if (currentMode == MODE_DEMO) {
                if (!uncommanded_curr_logged) {
                    Serial.println("[SLAVE] !!! CRITICAL FAULT: UNCOMMANDED CURRENT DETECTED IN 50ms LOOP! SSR SHORT CIRCUIT !!!");
                    uncommanded_curr_logged = true;
                }
            } else {
                Serial.println("[SAFETY] FAULT: Uncommanded current - possible SSR fault!");
            }
        }

        // 4. PLC watchdog (both modes; S6 specifically tests this in DEMO)
        if (cmd_ever_received && (millis() - last_cmd_received_ms > PLC_TIMEOUT_MS)) {
            fault = true;
            if (!plc_timeout_logged) {
                if (currentMode == MODE_DEMO)
                    Serial.println("[SLAVE] \xe2\x9a\xa0\xef\xb8\x8f NO CMD RECEIVED FOR 5s! TaskSafety TRIGGERED HARD-CUTOFF!");
                else
                    Serial.println("[SAFETY] FAULT: PLC timeout - no CMD for >5s");
                plc_timeout_logged = true;
            }
        } else {
            plc_timeout_logged = false;
        }

        if (fault) {
            ledcWrite(PIN_SSR_INT, 0);
            ledcWrite(PIN_SSR_EXT, 0);
            if (!system_fault) {
                system_fault   = true;
                fault_latch_ms = millis();
            }
        }

        // DEMO: auto-clear faults after 2s so test cycle continues
        // REALTIME: permanent latch, requires hardware reboot
        if (system_fault && currentMode == MODE_DEMO) {
            if (millis() - fault_latch_ms > 2000UL) {
                system_fault          = false;
                fault_latch_ms        = 0u;
                overheat_logged         = false;
                hw_interlock_logged     = false;
                plc_timeout_logged      = false;
                uncommanded_curr_logged = false;
                Serial.println("[SAFETY] Demo fault auto-cleared");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
