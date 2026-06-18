#include "plc_task.h"
#include "plc_comms.h"
#include "config.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  TaskPLC — periodic STATUS push + CMD receive
//
//  Period: STATUS_SEND_INTERVAL_MS (1000 ms), enforced with vTaskDelayUntil
//  so execution time of PLC_SendStatus() does not cause timing drift.
//
//  Each cycle:
//    1. Push STATUS packet to master (unconditional, ~34 ms @ 9600 baud)
//    2. Poll Serial1 for incoming CMD packets for the remaining ~950 ms
//    3. vTaskDelayUntil blocks for the final few ms to hit the exact period
// ---------------------------------------------------------------------------
void TaskPLC(void* pvParameters) {
    PLC_Init();
    Serial.println("[PLC] Task started — periodic 1 s STATUS push");

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(STATUS_SEND_INTERVAL_MS);

    for (;;) {
        // 1. Push STATUS to master
        PLC_SendStatus();

        // 2. Poll for CMD packets for ~950 ms (leaves margin before next cycle)
        const uint32_t poll_until = millis() + 950UL;
        while ((int32_t)(poll_until - millis()) > 0) {
            PLC_ReceivePacket();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 3. Enforce strict 1-second period — blocks for remaining time
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

