#include "flow_task.h"

#include <Arduino.h>
#include "config.h"
#include "shared/slave_state.h"
#include "shared_data.h"   // timerMux only; ISR pulse count remains isolated
#include "task_config.h"

// Internal variable for pulse counting. Only this file and the ISR touch it.
volatile int flow_pulse_count = 0;

void IRAM_ATTR pulseCounter() {
    portENTER_CRITICAL_ISR(&timerMux);
    flow_pulse_count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}

void TaskFlow(void* pvParameters) {
    (void)pvParameters;

    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR),
                    pulseCounter,
                    RISING);

    Serial.println("[FLOW] Task started");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TASK_FLOW_PERIOD_MS));

        int local_count = 0;
        portENTER_CRITICAL(&timerMux);
        local_count = flow_pulse_count;
        flow_pulse_count = 0;
        portEXIT_CRITICAL(&timerMux);

        float flow = ((float)local_count) / 6.6f;
        if (flow < 0.5f) {
            flow = 0.0f;
        }

        SensorSnapshot snapshot{};
        SlaveState_ReadSensors(snapshot);
        snapshot.flowLpm = flow;
        snapshot.updatedAtTick = xTaskGetTickCount();
        snapshot.valid = true;
        SlaveState_UpdateSensors(snapshot);
    }
}
