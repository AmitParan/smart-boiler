#include <Arduino.h>
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include "flow_task.h"
#include "temp_task.h"
#include "current_task.h"
#include "safety_task.h"
#include "pwm_task_internal.h"
#include "pwm_task_boost.h"
#include "plc_task.h"
// Note: comms_slave (old JSON) removed - all comms now via binary PLC protocol

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== SLAVE UNIT STARTED ===");
    Serial.println("[MODE] Boot mode: DEMO | Auto-switches to REALTIME when master sends without CMD_DEMO_ACTIVE");

    // -----------------------------------------------------------------------
    //  Create FreeRTOS mutexes before any task starts.
    //  All tasks that access shared_data must take the appropriate mutex.
    // -----------------------------------------------------------------------
    guard_temps   = xSemaphoreCreateMutex();
    guard_flow    = xSemaphoreCreateMutex();
    guard_current = xSemaphoreCreateMutex();
    guard_cmd     = xSemaphoreCreateMutex();

    if (!guard_temps || !guard_flow || !guard_current || !guard_cmd) {
        Serial.println("[FATAL] Failed to create mutexes - halting.");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // -----------------------------------------------------------------------
    //  FreeRTOS task layout � all pinned to Core 0 (ESP32-C6 is single-core)
    //
    //  Priority 4 (highest) : Safety � hard-cuts SSRs, runs every 50 ms
    //  Priority 3           : PWM tasks � time-proportional SSR burst control
    //  Priority 2           : Sensor + PLC tasks
    //  Priority 1 (lowest)  : Serial console (mode switching)
    // -----------------------------------------------------------------------

    // Sensor tasks
    xTaskCreatePinnedToCore(TaskFlow,    "Flow",    4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskTemp,    "Temp",    4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskCurrent, "Current", 4096, NULL, 2, NULL, 0);

    // Control tasks
    xTaskCreatePinnedToCore(TaskSafety,       "Safety",  4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(TaskPWM_Internal, "PWM_Int", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskPWM_Boost,    "PWM_Bst", 4096, NULL, 3, NULL, 0);

    // PLC communication (periodic push + CMD receive)
    xTaskCreatePinnedToCore(TaskPLC,    "PLC",    4096, NULL, 2, NULL, 0);

    // Serial console � mode switching ('b'/'p'/'?')
    xTaskCreatePinnedToCore(TaskSerial, "Serial", 2048, NULL, 1, NULL, 0);
}

void loop() {
    // All work is done in FreeRTOS tasks - loop does nothing
    vTaskDelay(pdMS_TO_TICKS(1000));
}

