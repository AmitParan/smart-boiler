#include "plc_task.h"
#include "plc_comms.h"
#include "config.h"

void TaskPLC(void* pvParameters) {
    PLC_Init();
    Serial.println("[PLC] Task started");

    for (;;) {
        // Request-response protocol:
        // Slave ONLY transmits STATUS after receiving a CMD from master.
        // This eliminates half-duplex collisions entirely.
        bool cmd_received = PLC_ReceivePacket();
        if (cmd_received) {
            // Small guard delay — let KQ-330 finish receiving before we TX
            vTaskDelay(pdMS_TO_TICKS(200));
            PLC_SendStatus();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
