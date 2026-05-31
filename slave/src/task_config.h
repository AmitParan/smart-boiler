#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ===========================================================================
//  Slave FreeRTOS task configuration
//
//  Central place for task stack sizes, priorities, core affinity, periods,
//  protocol delays, and queue lengths.
// ===========================================================================

// ---------------------------------------------------------------------------
//  Stack sizes in bytes (ESP32-Arduino uses bytes, not 4-byte words).
//
//  Rationale per task:
//    Flow    1536 — ISR counter + one float division + queue write. No printf.
//    Temp    2048 — DallasTemperature/OneWire nests ~5 calls deep (~1.2 KB).
//    Current 4096 — 100-sample RMS loop, sqrt(), Serial.printf at boot cal.
//    Safety  4096 — Priority-4 critical path; multiple Serial.printf (~1 KB each).
//    PWM_Int 2048 — ledcWrite + queue reads; no printf in hot path.
//    PWM_Bst 2048 — same as PWM_Int.
//    PLC     4096 — UART state machine + Serial.printf every 5 s.
// ---------------------------------------------------------------------------
#define TASK_FLOW_STACK_WORDS          1536
#define TASK_TEMP_STACK_WORDS          2048
#define TASK_CURRENT_STACK_WORDS       4096
#define TASK_SAFETY_STACK_WORDS        4096
#define TASK_PWM_INTERNAL_STACK_WORDS  2048
#define TASK_PWM_BOOST_STACK_WORDS     2048
#define TASK_PLC_STACK_WORDS           4096
#define TASK_PLC_TEST_STACK_WORDS      4096

// ---------------------------------------------------------------------------
//  Priorities.
//  Higher number = higher priority.
// ---------------------------------------------------------------------------
#define TASK_FLOW_PRIORITY             2 //  Flow task is low priority since it's not time-sensitive and can be blocked by other tasks.
#define TASK_TEMP_PRIORITY             2
#define TASK_CURRENT_PRIORITY          2
#define TASK_SAFETY_PRIORITY           4 // Safety task is high priority since it needs to respond quickly to fault conditions and should preempt other tasks.
#define TASK_PWM_INTERNAL_PRIORITY     3
#define TASK_PWM_BOOST_PRIORITY        3
#define TASK_PLC_PRIORITY              2
#define TASK_PLC_TEST_PRIORITY         2

// ---------------------------------------------------------------------------
//  Core affinity.
//  ESP32-C6 is single-core, so all tasks are pinned to Core 0.
// ---------------------------------------------------------------------------
#define TASK_CORE_SLAVE                0
#define TASK_CORE_FLOW                 TASK_CORE_SLAVE
#define TASK_CORE_TEMP                 TASK_CORE_SLAVE
#define TASK_CORE_CURRENT              TASK_CORE_SLAVE
#define TASK_CORE_SAFETY               TASK_CORE_SLAVE
#define TASK_CORE_PWM_INTERNAL         TASK_CORE_SLAVE
#define TASK_CORE_PWM_BOOST            TASK_CORE_SLAVE
#define TASK_CORE_PLC                  TASK_CORE_SLAVE
#define TASK_CORE_PLC_TEST             TASK_CORE_SLAVE

// ---------------------------------------------------------------------------
//  Sensor and control task timing.
// ---------------------------------------------------------------------------
#define TASK_FLOW_PERIOD_MS            1000u
#define TASK_TEMP_PERIOD_MS            1000u // 1000ms is a reasonable period for temperature samples since the thermal time constant of the system is on the order of minutes. Faster sampling would not provide much additional insight and would consume more CPU time and power.
#define TASK_CURRENT_BOOT_CAL_MS       5u              //  Time to wait between samples during current sensor boot calibration.    
#define TASK_CURRENT_SAMPLE_MS         1u
#define TASK_CURRENT_SETTLE_MS         500u           //  Time to wait after current sensor boot calibration before starting regular sampling. This allows the sensor output to stabilize after the initial calibration samples are taken.
#define TASK_CURRENT_PERIOD_MS         TASK_CURRENT_SETTLE_MS  //  TASK_CURRENT_SAMPLE_MS //  Effective period of current samples after boot calibration.
#define TASK_SAFETY_PERIOD_MS          50u
#define TASK_COMMAND_WATCHDOG_MS       6000u   // slave gets ~1 of 3 CMDs (half-duplex), so ~3s between CMDs
#define TASK_PWM_FAULT_PERIOD_MS       100u
#define TASK_PWM_WINDOW_MS             2000u
#define TASK_PWM_SLICE_MS              50u

// ---------------------------------------------------------------------------
//  PLC task and protocol timing.
// ---------------------------------------------------------------------------
#define TASK_PLC_POLL_MS               10u
#define TASK_PLC_RESPONSE_DELAY_MS     200u
#define TASK_PLC_REPORT_PERIOD_MS      5000UL
#define PLC_RX_TIMEOUT_MS              2000u
#define PLC_TX_QUIET_TIME_MS           500u
#define PLC_INTER_BYTE_GAP_MS          2u

// ---------------------------------------------------------------------------
//  Test-mode timing.
// ---------------------------------------------------------------------------
#define TASK_PLC_TEST_SCENARIO_HOLD_S  10u

// ---------------------------------------------------------------------------
//  Shared snapshot queues.
//  Length 1 means "latest value wins".
// ---------------------------------------------------------------------------
#define SENSOR_SNAPSHOT_QUEUE_LEN      1
#define COMMAND_SNAPSHOT_QUEUE_LEN     1

// ---------------------------------------------------------------------------
//  Task handles.
//  These will be defined in main.cpp when we wire the slave tasks to config.
// ---------------------------------------------------------------------------
extern TaskHandle_t g_taskFlowHandle;
extern TaskHandle_t g_taskTempHandle;
extern TaskHandle_t g_taskCurrentHandle;
extern TaskHandle_t g_taskSafetyHandle;
extern TaskHandle_t g_taskPwmInternalHandle;
extern TaskHandle_t g_taskPwmBoostHandle;
extern TaskHandle_t g_taskPlcHandle;
extern TaskHandle_t g_taskPlcTestHandle;

#endif // TASK_CONFIG_H
