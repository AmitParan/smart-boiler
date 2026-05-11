#include "temp_task.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "state/slave_state.h"
#include "task_config.h"

OneWire oneWire(PIN_TEMP_BUS);
DallasTemperature sensors(&oneWire);

void TaskTemp(void* pvParameters) {
    (void)pvParameters;

    sensors.begin();

    const int deviceCount = sensors.getDeviceCount();
    Serial.printf("[TEMP] DS18B20 devices found: %d (expected 3)\n",
                  deviceCount);

    // 10-bit resolution: max conversion time is about 188 ms.
    sensors.setResolution(10);
    sensors.setWaitForConversion(true);

    Serial.println("[TEMP] Task started");

    for (;;) {
        sensors.requestTemperatures();

        SensorSnapshot snapshot{};
        SlaveState_ReadSensors(snapshot);

        for (int i = 0; i < 3; ++i) {
            snapshot.tempsC[i] = sensors.getTempCByIndex(i);
        }

        snapshot.updatedAtTick = xTaskGetTickCount();
        snapshot.valid = true;
        SlaveState_UpdateSensors(snapshot);

        vTaskDelay(pdMS_TO_TICKS(TASK_TEMP_PERIOD_MS));
    }
}
