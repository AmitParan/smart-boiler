#include "plc_task.h"

#include <Arduino.h>
#include "plc_comms.h"
#include "task_config.h"

void TaskPLC(void* pvParameters) {
    (void)pvParameters;

    PLC_Init();
    Serial.println("[PLC] Task started");

    uint32_t raw_bytes = 0;
    uint32_t last_report_ms = 0;

    for (;;) {
        // Count raw bytes so we know if the channel delivers anything at all.
        while (Serial1.available()) {
            Serial1.peek();   // do not consume; PLC_ReceivePacket owns parsing
            raw_bytes++;
            break;
        }

        if (millis() - last_report_ms >= TASK_PLC_REPORT_PERIOD_MS) {
            Serial.printf("[PLC] Raw bytes in last 5s: %u\n", raw_bytes);
            raw_bytes = 0;
            last_report_ms = millis();
        }

        const bool cmd_received = PLC_ReceivePacket();
        if (cmd_received) {
            vTaskDelay(pdMS_TO_TICKS(TASK_PLC_RESPONSE_DELAY_MS));
            PLC_SendStatus();
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_PLC_POLL_MS));
    }
}
