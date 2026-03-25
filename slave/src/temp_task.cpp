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
        

        // Read data and update shared array
        for(int i=0; i<3; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            temps[i] = sensors.getTempCByIndex(i);
            }
        }
    }
