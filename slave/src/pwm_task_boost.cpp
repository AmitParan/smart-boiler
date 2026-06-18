#include "pwm_task_boost.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include "boiler_protocol.h"

void TaskPWM_Boost(void* pvParameters) {
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
        if (xSemaphoreTake(mutex_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            pwm_val = cmd_pwm_boost;
            enabled = (cmd_flags & CMD_BOOST_ENABLE) != 0u;
            xSemaphoreGive(mutex_cmd);
        }
        if (xSemaphoreTake(mutex_flow, pdMS_TO_TICKS(10)) == pdTRUE) {
            local_flow = current_flow;
            xSemaphoreGive(mutex_flow);
        }

        // Flow interlock: only enforced in MODE_PRODUCTION
        if (currentMode == MODE_PRODUCTION) {
            if (!enabled || local_flow < 1.0f) { pwm_val = 0; }
        } else {
            if (!enabled) { pwm_val = 0; }
        }

        int on_ms  = (2000 * (int)pwm_val) / 100;
        int off_ms = 2000 - on_ms;

        if (on_ms > 0) {
            ledcWrite(PIN_SSR_EXT, 127);
            boost_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }
        if (off_ms > 0) {
            ledcWrite(PIN_SSR_EXT, 0);
            boost_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
