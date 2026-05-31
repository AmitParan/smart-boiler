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
            const float t = sensors.getTempCByIndex(i);
            // DS18B20 returns 85.0 on power-on reset and -127.0 when
            // disconnected. Keep the previous value so stale-but-valid
            // data is better than a false spike triggering safety cutoff.
            if (t != DEVICE_DISCONNECTED_C && t != 85.0f) {
                snapshot.tempsC[i] = t;
            }
        }

        snapshot.updatedAtTick = xTaskGetTickCount();
        snapshot.valid = true;
        SlaveState_UpdateSensors(snapshot);

        Serial.printf("[TEMP] t0=%.1f t1=%.1f t2=%.1f\n",
                      snapshot.tempsC[0],
                      snapshot.tempsC[1],
                      snapshot.tempsC[2]);

        vTaskDelay(pdMS_TO_TICKS(TASK_TEMP_PERIOD_MS));
    }
}
