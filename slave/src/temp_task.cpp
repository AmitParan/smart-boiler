#include "temp_task.h"
#include "config.h"
#include "shared_data.h"
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
        sensors.requestTemperatures();   // blocks ~188 ms for 10-bit

        for (int i = 0; i < 3; i++) {
            temps[i] = sensors.getTempCByIndex(i);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
