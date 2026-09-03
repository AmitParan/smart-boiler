#include "pwm_task_internal.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"

void TaskPWM_Internal(void* pvParameters) {
    // -----------------------------------------------------------------------
    //  HARDWARE-INTERLOCK REQUIREMENT  (Project Book: "Internal Watchdog Gate")
    //  The SSR gate is DC-blocking (series input capacitors). It latches ONLY
    //  while it receives a continuous high-frequency pulse train ("AC" proof of
    //  a live controller). A constant DC level is read as a controller-freeze
    //  fault and the gate physically cuts the heater within ~1 s (RC, tau ~= 1 s).
    //    => ledcWrite(pin, 127) @ 1 kHz supplies that MANDATORY carrier.
    //    => DO NOT replace with digitalWrite(HIGH) (README "SW-3"): constant DC
    //       trips the hardware watchdog and the heater can never sustain ON.
    // -----------------------------------------------------------------------
    ledcAttach(PIN_SSR_INT, 1000, 8);
    ledcWrite(PIN_SSR_INT, 0);

    Serial.println("[PWM_INT] Task started");

    for (;;) {
        if (system_fault) {
            ledcWrite(PIN_SSR_INT, 0);
            internal_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Snapshot command values at start of burst cycle
        uint8_t pwm_val    = 0u;
        bool    enabled    = false;
        if (xSemaphoreTake(guard_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            pwm_val = cmd_pwm_internal;
            enabled = (cmd_flags & CMD_HEATER_ENABLE) != 0u;
            xSemaphoreGive(guard_cmd);
        }

        if (!enabled) { pwm_val = 0; }

        int on_ms  = (2000 * (int)pwm_val) / 100;
        int off_ms = 2000 - on_ms;

        if (on_ms > 0) {
            ledcWrite(PIN_SSR_INT, 127);
            if (!internal_ssr_on) Serial.println("[SSR] INT -> ON");
            internal_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }
        if (off_ms > 0) {
            ledcWrite(PIN_SSR_INT, 0);
            if (internal_ssr_on) Serial.println("[SSR] INT -> OFF");
            internal_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

