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
        //     Hardware comparator cuts power at 85°C. Software preemptively
        //     triggers a fault at 80°C to prevent reaching hardware limits.
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
        //     Boost element MUST NOT run without water flow (dry-fire risk).
        // -------------------------------------------------------------------
        bool boost_commanded = (cmd_flags & CMD_BOOST_ENABLE) &&
                               (cmd_pwm_boost > 0u);
                               
        if (boost_commanded && current_flow < 1.0f) {
            Serial.println("[SAFETY] FAULT: Boost commanded with no flow!");
            fault = true;
        }

        // -------------------------------------------------------------------
        //  3. Uncommanded current detection (SSR short-circuit)
        //     Threshold set to 0.5A. The ACS758-050B has an inherent noise 
        //     floor of ~0.25A (10mV noise / 40mV/A). 0.5A safely avoids 
        //     false positives while quickly detecting SSR leakage.
        // -------------------------------------------------------------------
        bool any_commanded = (cmd_pwm_internal > 0u) || (cmd_pwm_boost > 0u);
        if (!any_commanded && current_rms > 0.5f) {
            Serial.println("[SAFETY] FAULT: Current detected without command"
                           " — possible SSR short!");
            fault = true;
        }

        if (fault) {
            // Hard-cut both SSRs immediately
            digitalWrite(PIN_SSR_INT, LOW);
            digitalWrite(PIN_SSR_EXT, LOW);
            system_fault = true;
            // Note: system_fault is only cleared by a hardware reboot
        }

        // Run every 50 ms for fast fault response
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
