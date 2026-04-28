#include "plc_task.h"
#include "plc_comms.h"
#include "config.h"
#include <Arduino.h>

void TaskPLC(void* pvParameters) {
    PLC_Init();
    Serial.println("[PLC] Task started");

    uint32_t raw_bytes       = 0;
    uint32_t last_report_ms  = 0;

    for (;;) {
        // Count raw bytes so we know if the channel delivers anything at all
        while (Serial1.available()) {
            Serial1.peek();   // don't consume — let PLC_ReceivePacket handle it
            raw_bytes++;
            break;
        }

        if (millis() - last_report_ms >= 5000UL) {
            Serial.printf("[PLC] Raw bytes in last 5s: %u\n", raw_bytes);
            raw_bytes       = 0;
            last_report_ms  = millis();
        }

        bool cmd_received = PLC_ReceivePacket();
        if (cmd_received) {
            vTaskDelay(pdMS_TO_TICKS(200));
            PLC_SendStatus();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
