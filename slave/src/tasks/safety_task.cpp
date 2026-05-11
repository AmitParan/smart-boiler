#include "safety_task.h"

#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "state/slave_state.h"
#include "state/shared_data.h"   // legacy fault/status flags until PLC is refactored
#include "task_config.h"

namespace {
static constexpr float TEMP_SOFTWARE_CUTOFF_C = 80.0f;
static constexpr float BOOST_MIN_FLOW_LPM = 1.0f;

// Zero the LEDC outputs immediately, then suspend the PWM tasks so they
// cannot re-enable the SSRs even if the fault flag is read in a race window.
void forceOutputsOff() {
    ledcWrite(PIN_SSR_INT, 0);
    ledcWrite(PIN_SSR_EXT, 0);
    internal_ssr_on = false;
    boost_ssr_on = false;

    // Suspend PWM tasks. Guards against NULL during early boot (before the
    // handles are populated by xTaskCreatePinnedToCore in main.cpp).
    if (g_taskPwmInternalHandle != NULL) {
        vTaskSuspend(g_taskPwmInternalHandle);
    }
    if (g_taskPwmBoostHandle != NULL) {
        vTaskSuspend(g_taskPwmBoostHandle);
    }
}

bool commandTimedOut(const CommandSnapshot& command, TickType_t now) {
    if (!command.valid) {
        return true;
    }

    return (now - command.receivedAtTick) >
           pdMS_TO_TICKS(TASK_COMMAND_WATCHDOG_MS);
}

bool bothHeatersCommanded(const CommandSnapshot& command) {
    if (!command.valid) {
        return false;
    }

    const bool internalEnabled = (command.flags & CMD_HEATER_ENABLE) != 0u;
    const bool boostEnabled = (command.flags & CMD_BOOST_ENABLE) != 0u;
    return internalEnabled && boostEnabled;
}

bool overTemperature(const SensorSnapshot& sensors) {
    if (!sensors.valid) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        if (sensors.tempsC[i] > TEMP_SOFTWARE_CUTOFF_C) {
            Serial.printf("[SAFETY] FAULT: Overheat sensor[%d] = %.1f C\n",
                          i,
                          sensors.tempsC[i]);
            return true;
        }
    }

    return false;
}

bool boostWithoutFlow(const CommandSnapshot& command,
                      const SensorSnapshot& sensors) {
    if (!command.valid) {
        return false;
    }

    const bool boostCommanded =
        (command.flags & CMD_BOOST_ENABLE) != 0u &&
        command.pwmBoost > 0u;

    if (!boostCommanded) {
        return false;
    }

    if (!sensors.valid || sensors.flowLpm < BOOST_MIN_FLOW_LPM) {
        Serial.println("[SAFETY] FAULT: Boost commanded with no flow!");
        return true;
    }

    return false;
}
}

void TaskSafety(void* pvParameters) {
    (void)pvParameters;

    Serial.println("[SAFETY] Task started");

    for (;;) {
        const TickType_t now = xTaskGetTickCount();

        CommandSnapshot command{};
        SensorSnapshot sensors{};
        SlaveState_ReadCommand(command);
        SlaveState_ReadSensors(sensors);

        bool fault = false;

        if (commandTimedOut(command, now)) {
            Serial.println("[SAFETY] FAULT: Master command watchdog timeout");
            fault = true;
        }

        if (bothHeatersCommanded(command)) {
            Serial.println("[SAFETY] FAULT: Both heaters commanded at once");
            fault = true;
        }

        if (overTemperature(sensors)) {
            fault = true;
        }

        if (boostWithoutFlow(command, sensors)) {
            fault = true;
        }

        if (fault) {
            forceOutputsOff();
            system_fault = true;
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_SAFETY_PERIOD_MS));
    }
}
