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

        if (xSemaphoreTake(guard_temps, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < 3; i++) {
                temps[i] = sensors.getTempCByIndex(i);
            }
            xSemaphoreGive(guard_temps);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

