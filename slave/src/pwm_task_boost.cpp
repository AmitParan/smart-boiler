#include "pwm_task_boost.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include "boiler_protocol.h"

void TaskPWM_Boost(void* pvParameters) {
    // -----------------------------------------------------------------------
    //  HARDWARE-INTERLOCK REQUIREMENT  (Project Book: "External Boost Gate")
    //  Boost gate = triple hardware guard: DC-blocking watchdog + flow interlock
    //  + PWM regulation. The SSR latches ONLY while it receives a continuous
    //  high-frequency pulse train; a constant DC level is read as a fault and
    //  the gate cuts the heater. Flow gate adds a fixed ~1.0 s drop-out delay.
    //    => ledcWrite(pin, 127) @ 1 kHz supplies that MANDATORY carrier.
    //    => DO NOT replace with digitalWrite(HIGH) (README "SW-3"): constant DC
    //       trips the hardware watchdog and the heater can never sustain ON.
    // -----------------------------------------------------------------------
    ledcAttach(PIN_SSR_EXT, 1000, 8);
    ledcWrite(PIN_SSR_EXT, 0);

    Serial.println("[PWM_BST] Task started");

    for (;;) {
        if (system_fault) {
            ledcWrite(PIN_SSR_EXT, 0);
            boost_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Snapshot command + flow values at start of burst cycle
        uint8_t pwm_val   = 0u;
        bool    enabled   = false;
        float   local_flow = 0.0f;
        if (xSemaphoreTake(guard_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            pwm_val = cmd_pwm_boost;
            enabled = (cmd_flags & CMD_BOOST_ENABLE) != 0u;
            xSemaphoreGive(guard_cmd);
        }
        if (xSemaphoreTake(guard_flow, pdMS_TO_TICKS(10)) == pdTRUE) {
            local_flow = current_flow;
            xSemaphoreGive(guard_flow);
        }

        // Flow interlock: active in both MODE_DEMO and MODE_REALTIME.
        // In DEMO, flow is mock-injected by master so the check is always correct.
        if (!enabled || local_flow < 1.0f) { pwm_val = 0; }

        int on_ms  = (2000 * (int)pwm_val) / 100;
        int off_ms = 2000 - on_ms;

        if (on_ms > 0) {
            ledcWrite(PIN_SSR_EXT, 127);
            if (!boost_ssr_on) Serial.println("[SSR] EXT -> ON");
            boost_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }
        if (off_ms > 0) {
            ledcWrite(PIN_SSR_EXT, 0);
            if (boost_ssr_on) Serial.println("[SSR] EXT -> OFF");
            boost_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

