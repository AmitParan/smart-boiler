#include "pwm_task_boost.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"

void TaskPWM_Boost(void* pvParameters) {
    // Initialize PWM channel 1 at 1000Hz (8-bit resolution) for the hardware watchdog
    ledcSetup(1, 1000, 8);
    ledcAttachPin(PIN_SSR_EXT, 1);
    ledcWrite(1, 0); // Start in OFF state

    Serial.println("[PWM_BST] Task started");

    for (;;) {
        // Hardware protection: immediate cutoff in case of system fault
        if (system_fault) {
            ledcWrite(1, 0);
            boost_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint8_t pwm_val = cmd_pwm_boost;
        bool enabled = (cmd_flags & CMD_BOOST_ENABLE) != 0u;

        // Safety Interlock: prevent activation if water flow is below 1.0 L/min
        if (!enabled || current_flow < 1.0f) {
            pwm_val = 0;
        }

        // Calculate ON and OFF times within a hardcoded 2000ms window
        int on_ms = (2000 * (int)pwm_val) / 100;
        int off_ms = 2000 - on_ms;

        // Activate heater by sending a 1000Hz pulse (Duty Cycle of 127 out of 255)
        if (on_ms > 0) {
            ledcWrite(1, 127);
            boost_ssr_on = true;
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        }
        
        // Stop the pulse to turn off the heater
        if (off_ms > 0) {
            ledcWrite(1, 0);
            boost_ssr_on = false;
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
