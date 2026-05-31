#include <Arduino.h>
#include <WiFi.h>

#include "storage/DataManager.h"
#include "preheat/event_log.h"
#include "preheat/shower_histogram.h"
#include "preheat/heatup_tracker.h"
#include "preheat/preheat_settings.h"
#include "config.h"
#include "shared/master_state.h"
#include "task_config.h"
#include "tasks/TaskController.h"
#include "tasks/TaskComms.h"
#include "tasks/TaskPreheatScheduler.h"
#include "ui/ui_manager.h"
#include "system/SystemManagerTest.h"
#include "system/InteractiveTestBench.h"

// ===========================================================================
//  TEST MODE
//  0 = production (default)
//  1 = automated SystemManager scenario runner  (SystemManagerTest)
//  2 = interactive serial control panel         (InteractiveTestBench)
// ===========================================================================
#define TEST_MODE 0

// ===========================================================================
//  Master task handles
// ===========================================================================
TaskHandle_t g_taskControllerHandle       = nullptr;
TaskHandle_t g_taskCommsHandle            = nullptr;
TaskHandle_t g_taskUiHandle               = nullptr;
TaskHandle_t g_taskNetworkHandle          = nullptr;
TaskHandle_t g_taskPreheatSchedulerHandle = nullptr;

namespace {

// ---------------------------------------------------------------------------
//  TaskNetwork placeholder
//
//  Temporary shell for WiFi, NTP, and API work. Network calls can block, so
//  they belong outside the control brain and outside the PLC communication loop.
// ---------------------------------------------------------------------------
static void TaskNetwork(void* pvParameters) {
    (void)pvParameters;

    Serial.println("[NET] Placeholder task started");

    TickType_t lastWakeTick = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&lastWakeTick,
                        pdMS_TO_TICKS(TASK_WIFI_STATUS_PERIOD_MS));
    }
}

static void publishInitialUiState() {
    UiSnapshot ui{};
    ui.boilerOn = false;
    ui.targetShowerTempC = 60.0f;
    ui.updatedAtTick = xTaskGetTickCount();
    ui.valid = true;

    MasterState_PublishUiSnapshot(ui);
}

static void createTaskPinned(TaskFunction_t taskFunction,
                             const char* taskName,
                             uint32_t stackWords,
                             UBaseType_t priority,
                             BaseType_t coreId,
                             TaskHandle_t* taskHandle) {
    const BaseType_t created = xTaskCreatePinnedToCore(
        taskFunction,
        taskName,
        stackWords,
        nullptr,
        priority,
        taskHandle,
        coreId);

    if (created == pdPASS) {
        Serial.printf("[BOOT] Created task: %s\n", taskName);
    } else {
        Serial.printf("[BOOT] FAILED to create task: %s\n", taskName);
    }
}

} // namespace

void setup() {
    // -----------------------------------------------------------------------
    //  Basic hardware initialization
    // -----------------------------------------------------------------------
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("--- MASTER UNIT STARTED: FreeRTOS architecture shell ---");

    WiFi.persistent(false);
    WiFi.setSleep(false);

    // Existing persistent storage layer. Network/UI tasks will use it later.
    DataManager::init();

    // Smart brain event log (requires SPIFFS — must be after DataManager::init)
    EventLog::init();
    Serial.printf("[BOOT] EventLog ready: %u events in log\n", EventLog::count());

    // Shower histogram — 96-slot usage pattern (Phase 2)
    ShowerHistogram::init();
    Serial.printf("[BOOT] Histogram ready: %u sessions over %u days\n",
                  ShowerHistogram::totalSessions(), ShowerHistogram::daysObserved());

    // Adaptive lead time tracker (Phase 4)
    HeatupTracker::init();
    Serial.printf("[BOOT] HeatupTracker ready: %u session(s), lead=%u min\n",
                  HeatupTracker::sessionCount(), HeatupTracker::getLeadTimeMinutes());

    // Brain user settings — ready-by times (Phase 6)
    PreheatSettings::init();
    {
        uint16_t rbMin = 0u;
        if (PreheatSettings::getReadyByForToday(rbMin)) {
            char buf[6]; PreheatSettings::minuteToString(rbMin, buf, sizeof(buf));
            Serial.printf("[BOOT] Ready-by today: %s\n", buf);
        } else {
            Serial.println("[BOOT] Ready-by: not set");
        }
    }

    // -----------------------------------------------------------------------
    //  Shared state initialization
    // -----------------------------------------------------------------------
    if (!MasterState_Init()) {
        Serial.println("[BOOT] ERROR: failed to create master state queues");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    publishInitialUiState();

    // -----------------------------------------------------------------------
    //  FreeRTOS task creation
    // -----------------------------------------------------------------------
    createTaskPinned(TaskController,
                     "Controller",
                     TASK_CONTROLLER_STACK_WORDS,
                     TASK_CONTROLLER_PRIORITY,
                     TASK_CORE_CONTROLLER,
                     &g_taskControllerHandle);

    createTaskPinned(TaskComms,
                     "Comms",
                     TASK_COMMS_STACK_WORDS,
                     TASK_COMMS_PRIORITY,
                     TASK_CORE_COMMS,
                     &g_taskCommsHandle);

    createTaskPinned(TaskUi,
                     "UI",
                     TASK_UI_STACK_WORDS,
                     TASK_UI_PRIORITY,
                     TASK_CORE_UI,
                     &g_taskUiHandle);

    createTaskPinned(TaskNetwork,
                     "Network",
                     TASK_NETWORK_STACK_WORDS,
                     TASK_NETWORK_PRIORITY,
                     TASK_CORE_NETWORK,
                     &g_taskNetworkHandle);

    createTaskPinned(TaskPreheatScheduler,
                     "PreheatScheduler",
                     TASK_PREHEAT_SCHEDULER_STACK_WORDS,
                     TASK_PREHEAT_SCHEDULER_PRIORITY,
                     TASK_CORE_PREHEAT_SCHEDULER,
                     &g_taskPreheatSchedulerHandle);

#if TEST_MODE == 1
    createTaskPinned(TaskSystemManagerTest,
                     "SysMgrTest",
                     4096,
                     1,
                     1,
                     nullptr);
    Serial.println("[BOOT] TEST_MODE 1 — automated SystemManager test running");
#elif TEST_MODE == 2
    createTaskPinned(TaskInteractiveTestBench,
                     "InteractiveTB",
                     4096,
                     1,
                     1,
                     nullptr);
    Serial.println("[BOOT] TEST_MODE 2 — interactive serial test bench running");
#endif

    Serial.println("[BOOT] Master FreeRTOS shell ready");
}

void loop() {
    // Arduino still owns loopTask, but all application work now lives in
    // explicit FreeRTOS tasks. Keep this task idle.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
