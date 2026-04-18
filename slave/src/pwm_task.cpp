#include "pwm_task.h"
#include "config.h"
#include "shared_data.h"
#include "boiler_protocol.h"

// Burst-firing PWM window for AC mains thermal control (50 Hz)
static const int PWM_WINDOW_MS = 2000;

void TaskPWM(void* pvParameters) {
    pinMode(PIN_SSR_INT, OUTPUT);
    pinMode(PIN_SSR_EXT, OUTPUT);
    digitalWrite(PIN_SSR_INT, LOW);
    digitalWrite(PIN_SSR_EXT, LOW);
    Serial.println("[PWM] Task started");

    for (;;) {
        // ---------------------------------------------------------------
        //  Safety gate: if ANY fault is active, cut both SSRs immediately
        // ---------------------------------------------------------------
        if (system_fault) {
            digitalWrite(PIN_SSR_INT, LOW);
            digitalWrite(PIN_SSR_EXT, LOW);
            internal_ssr_on = false;
            boost_ssr_on    = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Read master commands (written atomically by plc_task on ESP32)
        uint8_t pwm_int = cmd_pwm_internal;
        uint8_t pwm_bst = cmd_pwm_boost;
        bool    en_int  = (cmd_flags & CMD_HEATER_ENABLE) != 0u;
        bool    en_bst  = (cmd_flags & CMD_BOOST_ENABLE)  != 0u;

        // ---------------------------------------------------------------
        //  Internal heater: simple on/off (2 kW tank element)
        //  Only allowed when master enables it AND sends PWM > 0
        // ---------------------------------------------------------------
        bool int_on = en_int && (pwm_int > 0u);
        digitalWrite(PIN_SSR_INT, int_on ? HIGH : LOW);
        internal_ssr_on = int_on;

        // ---------------------------------------------------------------
        //  Boost heater: PWM burst-firing (inline instantaneous heater)
        //  SAFETY INTERLOCK: forced off if water flow < 1.0 L/min
        // ---------------------------------------------------------------
        if (!en_bst || current_flow < 1.0f) {
            pwm_bst = 0u;
        }
        boost_ssr_on = (pwm_bst > 0u);

        if (pwm_bst > 0u) {
            int on_ms  = (PWM_WINDOW_MS * (int)pwm_bst) / 100;
            int off_ms = PWM_WINDOW_MS - on_ms;
            if (on_ms  > 0) { digitalWrite(PIN_SSR_EXT, HIGH); vTaskDelay(pdMS_TO_TICKS(on_ms));  }
            if (off_ms > 0) { digitalWrite(PIN_SSR_EXT, LOW);  vTaskDelay(pdMS_TO_TICKS(off_ms)); }
        } else {
            digitalWrite(PIN_SSR_EXT, LOW);
            vTaskDelay(pdMS_TO_TICKS(PWM_WINDOW_MS));
        }
    }
}
