#include <Arduino.h>
#include "config.h"
#include "task_config.h"
#include "tasks/flow_task.h"
#include "tasks/temp_task.h"
#include "tasks/current_task.h"
#include "tasks/safety_task.h"
#include "tasks/pwm_task_internal.h"
#include "tasks/pwm_task_boost.h"
#include "tasks/plc_task.h"
#include "tasks/plc_test_sender.h"
#include "tasks/TaskHardwareValidator.h"
#include "state/slave_state.h"
// Note: comms_slave (old JSON) removed — all comms now via binary PLC protocol

// ---------------------------------------------------------------------------
//  Global task handles — defined here, declared extern in task_config.h.
//  TaskSafety uses these to vTaskSuspend the PWM tasks on a hard fault.
// ---------------------------------------------------------------------------
TaskHandle_t g_taskFlowHandle        = NULL;
TaskHandle_t g_taskTempHandle        = NULL;
TaskHandle_t g_taskCurrentHandle     = NULL;
TaskHandle_t g_taskSafetyHandle      = NULL;
TaskHandle_t g_taskPwmInternalHandle = NULL;
TaskHandle_t g_taskPwmBoostHandle    = NULL;
TaskHandle_t g_taskPlcHandle         = NULL;
TaskHandle_t g_taskPlcTestHandle     = NULL;

// ---------------------------------------------------------------------------
//  SLAVE TEST MODES
//  SLAVE_TEST_MODE 0 = production (real sensors, real PLC)
//  SLAVE_TEST_MODE 1 = PLC test sender (scripted STATUS packets to master)
//  HW_TEST_MODE    1 = hardware LED validator (Part 2 bench test)
// ---------------------------------------------------------------------------
#define SLAVE_TEST_MODE 0
#define HW_TEST_MODE    0

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== SLAVE UNIT STARTED ===");

    if (!SlaveState_Init()) {
        Serial.println("[BOOT] ERROR: failed to create slave state queues");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

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

    // ESP32-C6 is single-core — all tasks pinned to Core 0.
    // Handles are stored globally so TaskSafety can vTaskSuspend the PWM
    // tasks on a hard fault, guaranteeing the SSRs cannot re-enable.
    xTaskCreatePinnedToCore(TaskFlow,    "Flow",    TASK_FLOW_STACK_WORDS,    NULL, TASK_FLOW_PRIORITY,    &g_taskFlowHandle,    TASK_CORE_FLOW);
    xTaskCreatePinnedToCore(TaskTemp,    "Temp",    TASK_TEMP_STACK_WORDS,    NULL, TASK_TEMP_PRIORITY,    &g_taskTempHandle,    TASK_CORE_TEMP);
    xTaskCreatePinnedToCore(TaskCurrent, "Current", TASK_CURRENT_STACK_WORDS, NULL, TASK_CURRENT_PRIORITY, &g_taskCurrentHandle, TASK_CORE_CURRENT);

    // Control tasks (higher priority than sensors)
    xTaskCreatePinnedToCore(TaskSafety,       "Safety",  TASK_SAFETY_STACK_WORDS,       NULL, TASK_SAFETY_PRIORITY,       &g_taskSafetyHandle,      TASK_CORE_SAFETY);
    xTaskCreatePinnedToCore(TaskPWM_Internal, "PWM_Int", TASK_PWM_INTERNAL_STACK_WORDS, NULL, TASK_PWM_INTERNAL_PRIORITY, &g_taskPwmInternalHandle, TASK_CORE_PWM_INTERNAL);
    xTaskCreatePinnedToCore(TaskPWM_Boost,    "PWM_Bst", TASK_PWM_BOOST_STACK_WORDS,    NULL, TASK_PWM_BOOST_PRIORITY,    &g_taskPwmBoostHandle,    TASK_CORE_PWM_BOOST);

    // PLC communication — or scripted test sender — or hardware validator
#if HW_TEST_MODE
    xTaskCreatePinnedToCore(TaskHardwareValidator, "HWValid", 4096, NULL, 2, NULL, TASK_CORE_SLAVE);
    Serial.println("[BOOT] HW_TEST_MODE 1 — hardware LED validator running");
#elif SLAVE_TEST_MODE
    Serial.println("[SLAVE TEST MODE] Starting PLC test sender");
    xTaskCreatePinnedToCore(TaskPLCTestSender, "PLCTest", TASK_PLC_TEST_STACK_WORDS, NULL, TASK_PLC_TEST_PRIORITY, &g_taskPlcTestHandle, TASK_CORE_PLC_TEST);
#else
    xTaskCreatePinnedToCore(TaskPLC, "PLC", TASK_PLC_STACK_WORDS, NULL, TASK_PLC_PRIORITY, &g_taskPlcHandle, TASK_CORE_PLC);
#endif
}

void loop() {
    // All work is done in FreeRTOS tasks — loop does nothing
    vTaskDelay(pdMS_TO_TICKS(1000));
}
