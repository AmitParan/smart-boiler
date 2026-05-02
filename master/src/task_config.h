#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ===========================================================================
//  Master FreeRTOS task configuration
//
//  This file is the single place to tune task stack sizes, priorities,
//  execution periods, communication timeouts, and queue lengths.
// ===========================================================================

// ---------------------------------------------------------------------------
//  Task stack sizes, in words as expected by xTaskCreatePinnedToCore().
// ---------------------------------------------------------------------------
#define TASK_BRAIN_STACK_WORDS        4096
#define TASK_MASTER_COMMS_STACK_WORDS 4096
#define TASK_UI_STACK_WORDS           8192
#define TASK_NETWORK_STACK_WORDS      6144

// ---------------------------------------------------------------------------
//  Task priorities.
//  Higher number = higher priority.
// ---------------------------------------------------------------------------
#define TASK_BRAIN_PRIORITY           3
#define TASK_MASTER_COMMS_PRIORITY    2
#define TASK_UI_PRIORITY              2
#define TASK_NETWORK_PRIORITY         1

// ---------------------------------------------------------------------------
//  ESP32-S3 core affinity.
//  Core 0 is generally busier with WiFi/radio work.
//  Core 1 is preferred for UI/control work on the master display unit.
// ---------------------------------------------------------------------------
#define TASK_CORE_NETWORK             0
#define TASK_CORE_MASTER_COMMS        1
#define TASK_CORE_BRAIN               1
#define TASK_CORE_UI                  1

// ---------------------------------------------------------------------------
//  Periodic task timing.
// ---------------------------------------------------------------------------
#define TASK_BRAIN_PERIOD_MS          250u
#define MASTER_COMMS_PERIOD_MS        10u
#define TASK_MASTER_COMMS_RX_POLL_MS  10u
#define TASK_MASTER_COMMS_TX_PERIOD_MS 1000u
#define TASK_UI_PERIOD_MS             10u
#define TASK_UI_RENDER_PERIOD_MS      250u
#define TASK_WIFI_STATUS_PERIOD_MS    5000u
#define TASK_CLOCK_UPDATE_PERIOD_MS   1000u
#define TASK_NTP_RESYNC_PERIOD_MS     3600000UL
#define TASK_WEATHER_PERIOD_MS        600000UL

// ---------------------------------------------------------------------------
//  PLC protocol timing and link supervision.
// ---------------------------------------------------------------------------
#define MASTER_PLC_BAUD               9600
#define MASTER_PLC_INTER_BYTE_GAP_MS  2u
#define MASTER_PLC_RX_TIMEOUT_MS      200u
#define MASTER_PLC_LINK_TIMEOUT_MS    3000u

// ---------------------------------------------------------------------------
//  Shared snapshot queues.
//  Length 1 means "latest value wins", which is ideal for live control state.
// ---------------------------------------------------------------------------
#define SENSOR_SNAPSHOT_QUEUE_LEN     1
#define UI_SNAPSHOT_QUEUE_LEN         1
#define COMMAND_SNAPSHOT_QUEUE_LEN    1

// ---------------------------------------------------------------------------
//  Task handles.
//  Defined in main.cpp so future modules can inspect/signal tasks if needed.
// ---------------------------------------------------------------------------
extern TaskHandle_t g_taskBrainHandle;
extern TaskHandle_t g_taskMasterCommsHandle;
extern TaskHandle_t g_taskUiHandle;
extern TaskHandle_t g_taskNetworkHandle;

#endif // TASK_CONFIG_H
