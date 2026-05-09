#include "shared/slave_state.h"
#include "task_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
QueueHandle_t sensorQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
}

bool SlaveState_Init() {
    if (sensorQueue == nullptr) {
        sensorQueue = xQueueCreate(SENSOR_SNAPSHOT_QUEUE_LEN,
                                   sizeof(SensorSnapshot));
    }

    if (commandQueue == nullptr) {
        commandQueue = xQueueCreate(COMMAND_SNAPSHOT_QUEUE_LEN,
                                    sizeof(CommandSnapshot));
    }

    return sensorQueue != nullptr && commandQueue != nullptr;
}

void SlaveState_UpdateSensors(const SensorSnapshot& snapshot) {
    if (sensorQueue != nullptr) {
        xQueueOverwrite(sensorQueue, &snapshot);
    }
}

bool SlaveState_ReadSensors(SensorSnapshot& snapshot) {
    return sensorQueue != nullptr &&
           xQueuePeek(sensorQueue, &snapshot, 0) == pdTRUE;
}

void SlaveState_UpdateCommand(const CommandSnapshot& snapshot) {
    if (commandQueue != nullptr) {
        xQueueOverwrite(commandQueue, &snapshot);
    }
}

bool SlaveState_ReadCommand(CommandSnapshot& snapshot) {
    return commandQueue != nullptr &&
           xQueuePeek(commandQueue, &snapshot, 0) == pdTRUE;
}
