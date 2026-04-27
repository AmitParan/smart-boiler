#include <Arduino.h>
#include "config.h"
#include "flow_task.h"
#include "temp_task.h"
#include "current_task.h"
#include "safety_task.h"
#include "pwm_task_internal.h"
#include "pwm_task_boost.h"
#include "plc_task.h"
#include "plc_test_sender.h"
// Note: comms_slave (old JSON) removed — all comms now via binary PLC protocol

// ---------------------------------------------------------------------------
//  SLAVE TEST MODE
//  Set to 1 to replace real sensor data with scripted scenarios.
//  The slave will cycle through all SystemManager states and send fake STATUS
//  packets so the master test bench (TEST_MODE 1) can verify its logic.
//  Set to 0 for normal operation with real sensors.
// ---------------------------------------------------------------------------
#define SLAVE_TEST_MODE 0

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== SLAVE UNIT STARTED ===");

    // -----------------------------------------------------------------------
    //  FreeRTOS task layout
    //
    //  Core 0  (WiFi/BT radio core — unused on slave, good for time-critical)
    //    PLC    — must be responsive to KQ-330 UART traffic
    //
    //  Core 1  (application core)
    //    Safety — highest priority, runs every 50 ms
    //    PWM    — controls SSRs, must not be starved
    //    Flow   — reads pulse counter from YF-B6
    //    Temp   — reads DS18B20 (slow, 750 ms conversion)
    //    Current— samples ACS758 ADC at 1 kHz for RMS
    // -----------------------------------------------------------------------

    // ESP32-C6 is single-core — all tasks pinned to Core 0
    xTaskCreatePinnedToCore(TaskFlow,    "Flow",    4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskTemp,    "Temp",    4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskCurrent, "Current", 4096, NULL, 2, NULL, 0);

    // Control tasks  (higher priority than sensors)
    xTaskCreatePinnedToCore(TaskSafety,       "Safety",   4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(TaskPWM_Internal, "PWM_Int",  4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskPWM_Boost,    "PWM_Bst",  4096, NULL, 3, NULL, 0);

    // PLC communication — or scripted test sender
#if SLAVE_TEST_MODE
    Serial.println("[SLAVE TEST MODE] Starting PLC test sender");
    xTaskCreatePinnedToCore(TaskPLCTestSender, "PLCTest", 4096, NULL, 2, NULL, 0);
#else
    xTaskCreatePinnedToCore(TaskPLC, "PLC", 4096, NULL, 2, NULL, 0);
#endif
}

void loop() {
    // All work is done in FreeRTOS tasks — loop does nothing
    vTaskDelay(pdMS_TO_TICKS(1000));
}
