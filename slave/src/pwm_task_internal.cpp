#include "pwm_task_internal.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"

void TaskPWM_Internal(void* pvParameters) {
    // Plain GPIO — time-proportional burst control is done at task level;
    // no hardware PWM carrier is needed or desired for SSR control.
    pinMode(PIN_SSR_INT, OUTPUT);
    digitalWrite(PIN_SSR_INT, LOW); // Start in OFF state

    Serial.println("[PWM_INT] Task started");

    for (;;) {
        // Hardware protection: immediate cutoff in case of system fault
        if (system_fault) {
            digitalWrite(PIN_SSR_INT, LOW);
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

        if (on_ms > 0) {
            digitalWrite(PIN_SSR_INT, HIGH);
            internal_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }

        if (off_ms > 0) {
            digitalWrite(PIN_SSR_INT, LOW);
            internal_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
