#include "pwm_task_internal.h"

#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "state/slave_state.h"
#include "state/shared_data.h"   // legacy SSR status/fault flags until PLC is refactored
#include "task_config.h"

namespace {
static constexpr uint32_t PWM_CARRIER_HZ = 1000u;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8u;
static constexpr uint32_t PWM_ACTIVE_DUTY = 127u;

CommandSnapshot lastCommand{};

bool readLatestCommand(CommandSnapshot& command) {
    CommandSnapshot latest{};
    if (SlaveState_ReadCommand(latest) && latest.valid) {
        lastCommand = latest;
    }

    command = lastCommand;
    return command.valid;
}

uint8_t internalDutyFromCommand(const CommandSnapshot& command) {
    if (!command.valid || (command.flags & CMD_HEATER_ENABLE) == 0u) {
        return 0u;
    }

    return command.pwmInternal > 100u ? 100u : command.pwmInternal;
}

bool commandChanged(const CommandSnapshot& previous,
                    const CommandSnapshot& current) {
    return previous.valid != current.valid ||
           previous.pwmInternal != current.pwmInternal ||
           previous.pwmBoost != current.pwmBoost ||
           previous.flags != current.flags;
}

bool waitInResponsiveSlices(uint32_t durationMs,
                            const CommandSnapshot& cycleCommand) {
    uint32_t elapsedMs = 0u;

    while (elapsedMs < durationMs) {
        CommandSnapshot currentCommand{};
        readLatestCommand(currentCommand);

        SensorSnapshot sensors{};
        SlaveState_ReadSensors(sensors);

        if (commandChanged(cycleCommand, currentCommand)) {
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

void setInternalOutput(bool on) {
    ledcWrite(PIN_SSR_INT, on ? PWM_ACTIVE_DUTY : 0u);
    internal_ssr_on = on;
}
}

void TaskPWM_Internal(void* pvParameters) {
    (void)pvParameters;

    ledcAttach(PIN_SSR_INT, PWM_CARRIER_HZ, PWM_RESOLUTION_BITS);
    setInternalOutput(false);

    Serial.println("[PWM_INT] Task started");

    for (;;) {
        if (system_fault) {
            setInternalOutput(false);
            vTaskDelay(pdMS_TO_TICKS(TASK_PWM_FAULT_PERIOD_MS));
            continue;
        }

        CommandSnapshot command{};
        if (!readLatestCommand(command)) {
            setInternalOutput(false);
            vTaskDelay(pdMS_TO_TICKS(TASK_PWM_SLICE_MS));
            continue;
        }

        const uint8_t pwmVal = internalDutyFromCommand(command);
        const uint32_t onMs = (TASK_PWM_WINDOW_MS * (uint32_t)pwmVal) / 100u;
        const uint32_t offMs = TASK_PWM_WINDOW_MS - onMs;

        if (onMs > 0u) {
            setInternalOutput(true);
            if (!waitInResponsiveSlices(onMs, command)) {
                setInternalOutput(false);
                continue;
            }
        }

        if (offMs > 0u) {
            setInternalOutput(false);
            waitInResponsiveSlices(offMs, command);
        }
    }
}
