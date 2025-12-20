#include <Arduino.h>
#include "flow_task.h"
#include "temp_task.h"
#include "comms_slave.h"

void setup() {
    Serial.begin(115200);
    Serial.println("--- SLAVE UNIT STARTED ---");

    // 1. משימת זרימה (Core 1)
    xTaskCreatePinnedToCore(TaskFlow, "Flow", 4096, NULL, 1, NULL, 1);

    // 2. משימת טמפרטורה (Core 1)
    xTaskCreatePinnedToCore(TaskTemp, "Temp", 4096, NULL, 1, NULL, 1);

    // 3. משימת תקשורת (Core 0 - שלא תפריע לחיישנים)
    xTaskCreatePinnedToCore(TaskComms, "Comms", 4096, NULL, 1, NULL, 0);
}

void loop() {
    vTaskDelay(1000);
}