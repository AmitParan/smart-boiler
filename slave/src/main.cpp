#include <Arduino.h>
#include "flow_task.h"
#include "temp_task.h"
#include "comms_slave.h"
#include "plc_task.h"

void setup() {
    Serial.begin(115200);
    Serial.println("--- SLAVE UNIT STARTED ---");

    // 1. Flow task (Core 1)
    xTaskCreatePinnedToCore(TaskFlow, "Flow", 4096, NULL, 1, NULL, 1);

    // 2. Temperature task (Core 1)
    xTaskCreatePinnedToCore(TaskTemp, "Temp", 4096, NULL, 1, NULL, 1);

    // 3. Communication task with Master (Core 0)
    xTaskCreatePinnedToCore(TaskComms, "Comms", 4096, NULL, 1, NULL, 0);
    
    // 4. PLC Communication task (Core 0 - handles KQ330 PLC)
    xTaskCreatePinnedToCore(TaskPLC, "PLC", 4096, NULL, 1, NULL, 0);
}

void loop() {
    vTaskDelay(1000);
}