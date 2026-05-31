#include "tasks/TaskController.h"

#include <Arduino.h>
#include "system/SystemManager.h"
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

void TaskController(void* pvParameters) {
    (void)pvParameters;

    Serial.println("[CTRL] Task started");

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

        // -----------------------------------------------------------------------
        //  Live serial output — print on state change + heartbeat every 5 s
        // -----------------------------------------------------------------------
        static BoilerState s_lastState  = BoilerState::STATE_OFF;
        static TickType_t  s_lastPrintTick = 0u;

        const bool stateChanged = (command.state != s_lastState);
        const bool heartbeat    = ((now - s_lastPrintTick) >= pdMS_TO_TICKS(5000u));

        if (stateChanged || heartbeat) {
            if (stateChanged) {
                Serial.printf("[CTRL] *** STATE CHANGE: %s -> %s ***\n",
                              SystemManager::labelFor(s_lastState),
                              SystemManager::labelFor(command.state));
                s_lastState = command.state;
            }

            // ── Sensor data from slave ──────────────────────────────────────
            if (hasSensor) {
                Serial.printf("[SENS] tempTank=%.1f°C  tempOut=%.1f°C  tempBoost=%.1f°C"
                              "  flow=%.2fL/min  power=%.0fW\n",
                              sensor.tempInternalC,
                              sensor.tempBoilerOutC,
                              sensor.tempBoostOutC,
                              sensor.flowLpm,
                              sensor.powerW);
            } else {
                Serial.println("[SENS] NO DATA FROM SLAVE — PLC link not established yet");
            }

            // ── Master decision ─────────────────────────────────────────────
            Serial.printf("[CTRL] state=%-20s  pwmInt=%3u%%  pwmBst=%3u%%"
                          "  plc=%-4s  target=%.0f°C  uiOn=%s\n",
                          SystemManager::labelFor(command.state),
                          command.pwmInternal,
                          command.pwmBoost,
                          command.plcConnected ? "OK" : "LOST",
                          hasUi ? ui.targetShowerTempC : 0.f,
                          (hasUi && ui.boilerOn) ? "YES" : "NO");

            s_lastPrintTick = now;
        }

        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(TASK_CONTROLLER_PERIOD_MS));
    }
}
