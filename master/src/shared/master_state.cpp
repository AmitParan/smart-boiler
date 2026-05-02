#include "shared/master_state.h"
#include "task_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
QueueHandle_t sensorQueue = nullptr;
QueueHandle_t uiQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
}

bool MasterState_Init() {
    if (sensorQueue == nullptr) {
        sensorQueue = xQueueCreate(SENSOR_SNAPSHOT_QUEUE_LEN, sizeof(SensorSnapshot));
    }
    if (uiQueue == nullptr) {
        uiQueue = xQueueCreate(UI_SNAPSHOT_QUEUE_LEN, sizeof(UiSnapshot));
    }
    if (commandQueue == nullptr) {
        commandQueue = xQueueCreate(COMMAND_SNAPSHOT_QUEUE_LEN, sizeof(CommandSnapshot));
    }

    return sensorQueue != nullptr && uiQueue != nullptr && commandQueue != nullptr;
}

void MasterState_PublishSensorSnapshot(const SensorSnapshot& snapshot) {
    if (sensorQueue != nullptr) {
        xQueueOverwrite(sensorQueue, &snapshot);
    }
}

bool MasterState_ReadSensorSnapshot(SensorSnapshot& snapshot) {
    return sensorQueue != nullptr && xQueuePeek(sensorQueue, &snapshot, 0) == pdTRUE;
}

void MasterState_PublishUiSnapshot(const UiSnapshot& snapshot) {
    if (uiQueue != nullptr) {
        xQueueOverwrite(uiQueue, &snapshot);
    }
}

bool MasterState_ReadUiSnapshot(UiSnapshot& snapshot) {
    return uiQueue != nullptr && xQueuePeek(uiQueue, &snapshot, 0) == pdTRUE;
}

void MasterState_PublishCommandSnapshot(const CommandSnapshot& snapshot) {
    if (commandQueue != nullptr) {
        xQueueOverwrite(commandQueue, &snapshot);
    }
}

bool MasterState_ReadCommandSnapshot(CommandSnapshot& snapshot) {
    return commandQueue != nullptr && xQueuePeek(commandQueue, &snapshot, 0) == pdTRUE;
}
