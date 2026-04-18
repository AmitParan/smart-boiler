#include "safety_task.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"
#include <Arduino.h>

void TaskSafety(void* pvParameters) {
    Serial.println("[SAFETY] Task started");

    for (;;) {
        bool fault = false;

        // -------------------------------------------------------------------
        //  1. Temperature overheat protection
        //     DS18B20 sensors: cut power if any reads > 80°C
        // -------------------------------------------------------------------
        for (int i = 0; i < 3; i++) {
            if (temps[i] > 80.0f) {
                Serial.printf("[SAFETY] FAULT: Overheat sensor[%d] = %.1f°C\n",
                              i, temps[i]);
                fault = true;
            }
        }

        // -------------------------------------------------------------------
        //  2. Boost heater flow interlock
        //     Boost element MUST NOT run without water flow (dry-fire risk)
        // -------------------------------------------------------------------
        bool boost_commanded = (cmd_flags & CMD_BOOST_ENABLE) &&
                               (cmd_pwm_boost > 0u);
        if (boost_commanded && current_flow < 1.0f) {
            Serial.println("[SAFETY] FAULT: Boost commanded with no flow!");
            fault = true;
        }

        // -------------------------------------------------------------------
        //  3. Uncommanded current detection (possible SSR short-circuit)
        // -------------------------------------------------------------------
        bool any_commanded = (cmd_pwm_internal > 0u) || (cmd_pwm_boost > 0u);
        if (!any_commanded && current_rms > 1.0f) {
            Serial.println("[SAFETY] WARNING: Current detected without command"
                           " — possible SSR short!");
            fault = true;
        }

        if (fault) {
            // Hard-cut both SSRs — PWM task will also see system_fault
            digitalWrite(PIN_SSR_INT, LOW);
            digitalWrite(PIN_SSR_EXT, LOW);
            system_fault = true;
            // system_fault is only cleared by a hardware reboot
        }

        // Run every 50 ms for fast fault response
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
