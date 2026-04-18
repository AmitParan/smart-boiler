#include <Arduino.h>
#include "config.h"
#include "flow_task.h"
#include "temp_task.h"
#include "current_task.h"
#include "safety_task.h"
#include "pwm_task.h"
#include "plc_task.h"

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

    // Sensor tasks
    xTaskCreatePinnedToCore(TaskFlow,    "Flow",    4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(TaskTemp,    "Temp",    4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(TaskCurrent, "Current", 4096, NULL, 2, NULL, 1);

    // Control tasks  (higher priority than sensors)
    xTaskCreatePinnedToCore(TaskSafety,  "Safety",  4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(TaskPWM,     "PWM",     4096, NULL, 3, NULL, 1);

    // PLC communication on Core 0
    xTaskCreatePinnedToCore(TaskPLC,     "PLC",     4096, NULL, 2, NULL, 0);
}

void loop() {
    // All work is done in FreeRTOS tasks — loop does nothing
    vTaskDelay(pdMS_TO_TICKS(1000));
}
