#include "temp_task.h"
#include "config.h"
#include "shared_data.h"
#include <OneWire.h>
#include <DallasTemperature.h>

OneWire oneWire(PIN_TEMP_BUS);
DallasTemperature sensors(&oneWire);

void TaskTemp(void * pvParameters) {
    sensors.begin();
    sensors.setResolution(10);
    sensors.setWaitForConversion(false); // We manage the timing

    for(;;) {
        // Send measurement request
        sensors.requestTemperatures();
        
        // Wait for sensor to finish measuring (1000ms for safety)
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Read data and update shared array
        int count = sensors.getDeviceCount();
        for(int i=0; i<3; i++) {
            if (i < count) {
                temps[i] = sensors.getTempCByIndex(i);
            } else {
                temps[i] = 0.0;
            }
        }
    }
}