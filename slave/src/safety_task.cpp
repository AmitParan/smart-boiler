#include "safety_task.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

void TaskSafety(void * pvParameters) {
    for(;;) {
        bool safe_state = true;
        
        // 1. Software Overheat Protection (Backup to analog NTC)
        // temps[2] is assumed to be the external temp sensor
        if (temps[0] > 80.0 || temps[1] > 80.0 || temps[2] > 80.0) {
            safe_state = false;
            Serial.println("SAFETY FAULT: Overheat detected!");
        }
        
        // 2. Flow Interlock for Boost Heater
        // If Boost is requested but flow is too low, force safe state
        if (boost_requested && current_flow < 1.0) {
            safe_state = false;
            Serial.println("SAFETY FAULT: Boost requested without flow!");
        }
        
        // 3. Dry Run / SSR Short detection (Current flows but no command)
        if (!internal_requested && !boost_requested && current_rms > 1.0) {
            // We have current but didn't ask for it! SSR might be shorted closed.
            Serial.println("SAFETY WARNING: Uncommanded current detected!");
            // Can't turn off a broken SSR from software, but we can flag it for the user
            system_fault = true; 
        }

        if (!safe_state) {
            // Cut power immediately, overriding Manager
            digitalWrite(PIN_SSR_INT, LOW);
            digitalWrite(PIN_SSR_EXT, LOW);
            system_fault = true;
        }

        // Run very frequently (e.g., every 50ms) to ensure fast response
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
