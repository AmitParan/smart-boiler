#include "plc_task.h"
#include "plc_comms.h"
#include "config.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  TaskPLC — triggered STATUS response + 5s heartbeat
//
//  Protocol (half-duplex KQ-330 — only one device transmits at a time):
//    1. Poll Serial1 every 10ms for incoming CMD from master
//    2. When CMD received: wait 200ms guard time, then push STATUS
//    3. Heartbeat: if no CMD received for 5s, push STATUS anyway so the
//       master knows the slave is alive
//
//  Why triggered instead of periodic unconditional push?
//  The KQ-330 is half-duplex. Master sends CMD every ~1s and listens for
//  STATUS for 3s. If slave also pushes every 1s independently, the two
//  transmissions collide on the power line and both packets are lost.
//  Triggered response guarantees the channel is quiet before slave TX.
// ---------------------------------------------------------------------------
void TaskPLC(void* pvParameters) {
    PLC_Init();
    Serial.println("[PLC] Task started — triggered STATUS response");

    uint32_t last_status_ms = millis();

    for (;;) {
        bool cmd_received = PLC_ReceivePacket();

        if (cmd_received) {
            // Guard time: let the CMD echo clear the KQ-330 channel before TX
            vTaskDelay(pdMS_TO_TICKS(200));
            PLC_SendStatus();
            last_status_ms = millis();
        }

        // Heartbeat: push STATUS if master has been silent for 5 seconds
        if (millis() - last_status_ms >= 5000UL) {
            PLC_SendStatus();
            last_status_ms = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

