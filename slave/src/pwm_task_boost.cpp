#include "pwm_task_boost.h"

#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "shared/slave_state.h"
#include "shared_data.h"   // legacy SSR status/fault flags until PLC is refactored
#include "task_config.h"

namespace {
static constexpr uint32_t PWM_CARRIER_HZ = 1000u;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8u;
static constexpr uint32_t PWM_ACTIVE_DUTY = 127u;
static constexpr float BOOST_MIN_FLOW_LPM = 1.0f;

CommandSnapshot lastCommand{};
SensorSnapshot lastSensors{};

bool readLatestCommand(CommandSnapshot& command) {
    CommandSnapshot latest{};
    if (SlaveState_ReadCommand(latest) && latest.valid) {
        lastCommand = latest;
    }

    command = lastCommand;
    return command.valid;
}

bool readLatestSensors(SensorSnapshot& sensors) {
    SensorSnapshot latest{};
    if (SlaveState_ReadSensors(latest) && latest.valid) {
        lastSensors = latest;
    }

    sensors = lastSensors;
    return sensors.valid;
}

uint8_t boostDutyFromSnapshots(const CommandSnapshot& command,
                               const SensorSnapshot& sensors) {
    if (!command.valid ||
        !sensors.valid ||
        (command.flags & CMD_BOOST_ENABLE) == 0u ||
        sensors.flowLpm < BOOST_MIN_FLOW_LPM) {
        return 0u;
    }

    return command.pwmBoost > 100u ? 100u : command.pwmBoost;
}

bool commandChanged(const CommandSnapshot& previous,
                    const CommandSnapshot& current) {
    return previous.valid != current.valid ||
           previous.pwmInternal != current.pwmInternal ||
           previous.pwmBoost != current.pwmBoost ||
           previous.flags != current.flags;
}

void setBoostOutput(bool on) {
    ledcWrite(PIN_SSR_EXT, on ? PWM_ACTIVE_DUTY : 0u);
    boost_ssr_on = on;
}

bool waitInResponsiveSlices(uint32_t durationMs,
                            const CommandSnapshot& cycleCommand,
                            bool outputIsOn) {
    uint32_t elapsedMs = 0u;

    while (elapsedMs < durationMs) {
        CommandSnapshot currentCommand{};
        readLatestCommand(currentCommand);

        SensorSnapshot currentSensors{};
        readLatestSensors(currentSensors);

        if (commandChanged(cycleCommand, currentCommand)) {
            return false;
        }

        if (outputIsOn &&
            (!currentSensors.valid ||
             currentSensors.flowLpm < BOOST_MIN_FLOW_LPM)) {
            Serial.println("[PWM_BST] Flow dropped; boost output cut");
            setBoostOutput(false);
            return false;
        }

        const uint32_t remainingMs = durationMs - elapsedMs;
        const uint32_t delayMs =
            remainingMs < TASK_PWM_SLICE_MS ? remainingMs : TASK_PWM_SLICE_MS;

        vTaskDelay(pdMS_TO_TICKS(delayMs));
        elapsedMs += delayMs;
    }

    return true;
}
}

void TaskPWM_Boost(void* pvParameters) {
    (void)pvParameters;

    ledcAttach(PIN_SSR_EXT, PWM_CARRIER_HZ, PWM_RESOLUTION_BITS);
    setBoostOutput(false);

    Serial.println("[PWM_BST] Task started");

    for (;;) {
        if (system_fault) {
            setBoostOutput(false);
            vTaskDelay(pdMS_TO_TICKS(TASK_PWM_FAULT_PERIOD_MS));
            continue;
        }

        CommandSnapshot command{};
        SensorSnapshot sensors{};
        const bool hasCommand = readLatestCommand(command);
        const bool hasSensors = readLatestSensors(sensors);

        if (!hasCommand || !hasSensors) {
            setBoostOutput(false);
            vTaskDelay(pdMS_TO_TICKS(TASK_PWM_SLICE_MS));
            continue;
        }

        const uint8_t pwmVal = boostDutyFromSnapshots(command, sensors);
        const uint32_t onMs = (TASK_PWM_WINDOW_MS * (uint32_t)pwmVal) / 100u;
        const uint32_t offMs = TASK_PWM_WINDOW_MS - onMs;

        if (onMs > 0u) {
            setBoostOutput(true);
            if (!waitInResponsiveSlices(onMs, command, true)) {
                setBoostOutput(false);
                continue;
            }
        }

        if (offMs > 0u) {
            setBoostOutput(false);
            waitInResponsiveSlices(offMs, command, false);
        }
    }
}
