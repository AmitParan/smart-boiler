#include "pwm_task_internal.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"

void TaskPWM_Internal(void* pvParameters) {
    // Initialize PWM on the SSR pin at 1000Hz (8-bit resolution) for the hardware watchdog
    ledcAttach(PIN_SSR_INT, 1000, 8);
    ledcWrite(PIN_SSR_INT, 0); // Start in OFF state

    Serial.println("[PWM_INT] Task started");

    for (;;) {
        // Hardware protection: immediate cutoff in case of system fault
        if (system_fault) {
            ledcWrite(PIN_SSR_INT, 0);
            internal_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint8_t pwm_val = cmd_pwm_internal;
        bool enabled = (cmd_flags & CMD_HEATER_ENABLE) != 0u;

        if (!enabled) {
            pwm_val = 0;
        }

        // Calculate ON and OFF times within a hardcoded 2000ms window
        int on_ms = (2000 * (int)pwm_val) / 100;
        int off_ms = 2000 - on_ms;

        // Activate heater by sending a 1000Hz pulse (Duty Cycle of 127 out of 255)
        if (on_ms > 0) {
            ledcWrite(PIN_SSR_INT, 127);
            internal_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }
        
        // Stop the pulse to turn off the heater
        if (off_ms > 0) {
            ledcWrite(PIN_SSR_INT, 0);
            internal_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
