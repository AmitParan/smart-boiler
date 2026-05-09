#include "tasks/TaskBrain.h"

#include <Arduino.h>
#include "SystemManager.h"
#include "boiler_protocol.h"
#include "shared/master_state.h"
#include "task_config.h"

namespace {
SystemManager brain;

CommandSnapshot makeFailSafeCommand(TickType_t now, bool plcConnected) {
    CommandSnapshot command{};
    command.pwmInternal = PWM_OFF;
    command.pwmBoost = PWM_OFF;
    command.cmdFlags = CMD_EMERGENCY_STOP;
    command.state = BoilerState::SAFETY_OVERRIDE;
    command.decidedAtTick = now;
    command.plcConnected = plcConnected;
    command.valid = true;
    return command;
}
}

void TaskBrain(void* pvParameters) {
    (void)pvParameters;

    Serial.println("[BRAIN] Task started");

    TickType_t lastWakeTick = xTaskGetTickCount();

    for (;;) {
        const TickType_t now = xTaskGetTickCount();

        SensorSnapshot sensor{};
        UiSnapshot ui{};

        const bool hasSensor = MasterState_ReadSensorSnapshot(sensor) && sensor.valid;
        const bool hasUi = MasterState_ReadUiSnapshot(ui) && ui.valid;

        const bool plcConnected =
            hasSensor &&
            ((now - sensor.receivedAtTick) < pdMS_TO_TICKS(MASTER_PLC_LINK_TIMEOUT_MS));

        CommandSnapshot command{};

        if (!hasUi || !hasSensor) {
            command = makeFailSafeCommand(now, plcConnected);
        } else {
            SystemInputs inputs{};
            inputs.currentTemp = sensor.tempInternalC;
            inputs.flowRateLPM = sensor.flowLpm;
            inputs.targetShowerTemp = ui.targetShowerTempC;
            inputs.uiStateOn = ui.boilerOn;
            inputs.plcConnected = plcConnected;

            const SystemCommand decision = brain.process(inputs);

            command.pwmInternal = decision.pwmInternal;
            command.pwmBoost = decision.pwmBoost;
            command.cmdFlags = 0u;
            command.cmdFlags |= (decision.pwmInternal > 0u) ? CMD_HEATER_ENABLE : 0u;
            command.cmdFlags |= (decision.pwmBoost > 0u) ? CMD_BOOST_ENABLE : 0u;
            if (!plcConnected || decision.state == BoilerState::SAFETY_OVERRIDE) {
                command.cmdFlags |= CMD_EMERGENCY_STOP;
            }
            command.state = decision.state;
            command.decidedAtTick = now;
            command.plcConnected = plcConnected;
            command.valid = true;
        }

        MasterState_PublishCommandSnapshot(command);

        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(TASK_BRAIN_PERIOD_MS));
    }
}
