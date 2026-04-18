#include "plc_task.h"
#include "plc_comms.h"
#include "config.h"

void TaskPLC(void* pvParameters) {
    PLC_Init();
    Serial.println("[PLC] Task started");

    unsigned long last_send_ms = 0UL;

    for (;;) {
        // Non-blocking receive — process any incoming CMD bytes
        PLC_ReceivePacket();

        // Send status once per second
        unsigned long now = millis();
        if (now - last_send_ms >= STATUS_SEND_INTERVAL_MS) {
            last_send_ms = now;
            PLC_SendStatus();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
