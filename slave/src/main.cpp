#include <Arduino.h>
#include "flow_task.h"
#include "temp_task.h"
#include "comms_slave.h"
#include "plc_task.h"

void setup() {
    Serial.begin(115200);
    Serial.println("--- SLAVE UNIT STARTED ---");

    // 1. Flow task (Core 1)
    xTaskCreate(TaskFlow, "Flow", 4096, NULL, 1, NULL);

    // 2. Temperature task (Core 1)
    xTaskCreate(TaskTemp, "Temp", 4096, NULL, 1, NULL);

    // 3. Communication task with Master (Core 0)
    xTaskCreate(TaskComms, "Comms", 4096, NULL, 1, NULL);
    
    // 4. PLC Communication task (Core 0 - handles KQ330 PLC)
    xTaskCreate(TaskPLC, "PLC", 4096, NULL, 1, NULL);
}

void loop() {
    vTaskDelay(1000);
}
