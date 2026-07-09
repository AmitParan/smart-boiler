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
            // Mirror master-injected temperature instead of reading DS18B20
            float t = slave_demo_temp_x10 / 10.0f;
            if (xSemaphoreTake(mutex_temps, pdMS_TO_TICKS(50)) == pdTRUE) {
                temps[0] = t;
                temps[1] = t - 2.0f;
                temps[2] = t + 1.0f;
                xSemaphoreGive(mutex_temps);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        sensors.requestTemperatures();   // blocks ~188 ms for 10-bit

        if (xSemaphoreTake(mutex_temps, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < 3; i++) {
                temps[i] = sensors.getTempCByIndex(i);
            }
            xSemaphoreGive(mutex_temps);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
