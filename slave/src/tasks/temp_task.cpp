#include "temp_task.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include <OneWire.h>
#include <DallasTemperature.h>

OneWire oneWire(PIN_TEMP_BUS);
DallasTemperature sensors(&oneWire);

void TaskTemp(void * pvParameters) {
    sensors.begin();

    int deviceCount = sensors.getDeviceCount();
    Serial.printf("[TEMP] DS18B20 devices found: %d (expected 3)\n", deviceCount);

    // 10-bit resolution: max conversion time = 188 ms
    // Let the library block for conversion — simpler and correct
    sensors.setResolution(10);
    sensors.setWaitForConversion(true);

    Serial.println("[TEMP] Task started");

    bool temp_fault_logged = false;   // log a sensor fault once, not every second

    for (;;) {
        if (currentMode == MODE_DEMO) {
            // Infer mock temperature from demo flags + commanded SSR state
            float t;
            if (slave_demo_overtemp) {
                t = 87.0f;    // Scenario 7: triggers overheat safety at 80C
            } else {
                uint8_t local_pwm_int = 0u, local_pwm_bst = 0u;
                if (xSemaphoreTake(guard_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
                    local_pwm_int = cmd_pwm_internal;
                    local_pwm_bst = cmd_pwm_boost;
                    xSemaphoreGive(guard_cmd);
                }
                if      (local_pwm_int > 0u) t = 25.0f;  // cold tank heating
                else if (local_pwm_bst > 0u) t = 35.0f;  // shower, below 40C target
                else                         t = 42.0f;  // warm standby
            }
            if (xSemaphoreTake(guard_temps, pdMS_TO_TICKS(50)) == pdTRUE) {
                temps[0] = t;
                temps[1] = t - 2.0f;
                temps[2] = t + 1.0f;
                xSemaphoreGive(guard_temps);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        sensors.requestTemperatures();   // blocks ~188 ms for 10-bit

        // ---------------------------------------------------------------------
        //  Validate before trusting. A DS18B20 is specified over -55..+125 °C,
        //  so a reading outside that window is not a temperature - it is a dead
        //  or disconnected sensor. DallasTemperature signals this by returning
        //  DEVICE_DISCONNECTED_C (-127.0).
        //
        //  Storing -127 as if it were real would be actively dangerous: the
        //  controller would conclude the water is freezing and command maximum
        //  heat forever, while the 80 °C overheat check stays silent because
        //  -127 is not greater than 80. A failed sensor must stop the heater,
        //  not command it to full power.
        // ---------------------------------------------------------------------
        float raw[3];
        bool  all_valid = true;
        for (int i = 0; i < 3; i++) {
            raw[i] = sensors.getTempCByIndex(i);
            if (raw[i] < -55.0f || raw[i] > 125.0f) {
                all_valid = false;
            }
        }

        if (xSemaphoreTake(guard_temps, pdMS_TO_TICKS(50)) == pdTRUE) {
            // Only publish readings we believe. On failure the previous good
            // values are left in place rather than poisoning the control loop
            // with -127; TaskSafety cuts both SSRs regardless via the flag below.
            if (all_valid) {
                for (int i = 0; i < 3; i++) {
                    temps[i] = raw[i];
                }
            }
            xSemaphoreGive(guard_temps);
        }

        temp_sensors_valid = all_valid;
        temp_ever_read     = true;

        if (!all_valid) {
            if (!temp_fault_logged) {
                Serial.printf("[TEMP] FAULT: sensor reading out of range "
                              "(%.1f / %.1f / %.1f) - disconnected or faulty bus\n",
                              raw[0], raw[1], raw[2]);
                temp_fault_logged = true;
            }
        } else {
            temp_fault_logged = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

