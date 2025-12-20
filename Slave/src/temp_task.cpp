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
    sensors.setWaitForConversion(false); // אנחנו מנהלים את הזמן

    for(;;) {
        // שליחת בקשה למדידה
        sensors.requestTemperatures();
        
        // המתנה שהחיישן יסיים למדוד (750ms ליתר ביטחון)
        vTaskDelay(pdMS_TO_TICKS(1000));

        // קריאת הנתונים ועדכון המערך המשותף
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