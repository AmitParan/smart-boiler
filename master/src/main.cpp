#include <Arduino.h>
#include <WiFi.h>

#include "DataManager.h"
#include "brain/event_log.h"
#include "config.h"
#include "shared/master_state.h"
#include "task_config.h"
#include "tasks/TaskBrain.h"
#include "tasks/TaskMasterComms.h"
#include "ui_manager.h"

// ===========================================================================
//  Master task handles
// ===========================================================================
TaskHandle_t g_taskBrainHandle = nullptr;
TaskHandle_t g_taskMasterCommsHandle = nullptr;
TaskHandle_t g_taskUiHandle = nullptr;
TaskHandle_t g_taskNetworkHandle = nullptr;

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
    createTaskPinned(TaskBrain,
                     "Brain",
                     TASK_BRAIN_STACK_WORDS,
                     TASK_BRAIN_PRIORITY,
                     TASK_CORE_BRAIN,
                     &g_taskBrainHandle);

    createTaskPinned(TaskMasterComms,
                     "MasterComms",
                     TASK_MASTER_COMMS_STACK_WORDS,
                     TASK_MASTER_COMMS_PRIORITY,
                     TASK_CORE_MASTER_COMMS,
                     &g_taskMasterCommsHandle);

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

    Serial.println("[BOOT] Master FreeRTOS shell ready");
}

void loop() {
    // Arduino still owns loopTask, but all application work now lives in
    // explicit FreeRTOS tasks. Keep this task idle.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
