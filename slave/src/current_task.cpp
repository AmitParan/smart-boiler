#include "current_task.h"

#include <Arduino.h>
#include "config.h"
#include "shared/slave_state.h"
#include "task_config.h"

static const int numSamples = 100;

void TaskCurrent(void* pvParameters) {
    (void)pvParameters;

    analogReadResolution(12);
    analogRead(PIN_CURRENT_SENSOR);
    analogSetPinAttenuation(PIN_CURRENT_SENSOR, ADC_11db);

    Serial.println("[CURRENT] Calibrating zero reference...");
    float vref_sum = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float v_adc = (analogRead(PIN_CURRENT_SENSOR) / 4095.0f) * 3.3f;
        vref_sum += v_adc / CURRENT_DIVIDER_RATIO;
        vTaskDelay(pdMS_TO_TICKS(TASK_CURRENT_BOOT_CAL_MS));
    }

    const float vref_actual = vref_sum / 200.0f;
    const float vcc_actual = vref_actual * 2.0f;
    const float sensitivity_actual = ACS758_SENSITIVITY * (vcc_actual / 5.0f);

    Serial.printf("[CURRENT] Calibrated VREF = %.3fV (expected %.3fV)\n",
                  vref_actual,
                  ACS758_VREF);
    Serial.printf("[CURRENT] VCC = %.3fV  sensitivity = %.1f mV/A "
                  "(nominal 40.0)\n",
                  vcc_actual,
                  sensitivity_actual * 1000.0f);
    Serial.println("[CURRENT] Task started");

    for (;;) {
        float sumSq = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float v_adc =
                (analogRead(PIN_CURRENT_SENSOR) / 4095.0f) * 3.3f;
            const float v_sensor = v_adc / CURRENT_DIVIDER_RATIO;
            const float current_instant =
                (v_sensor - vref_actual) / sensitivity_actual;
            sumSq += current_instant * current_instant;
            vTaskDelay(pdMS_TO_TICKS(TASK_CURRENT_SAMPLE_MS));
        }

        float rms_current = sqrtf(sumSq / numSamples);
        if (rms_current < 0.2f) {
            rms_current = 0.0f;
        }

        SensorSnapshot snapshot{};
        SlaveState_ReadSensors(snapshot);
        snapshot.currentRmsA = rms_current;
        snapshot.powerW = rms_current * 220.0f;
        snapshot.updatedAtTick = xTaskGetTickCount();
        snapshot.valid = true;
        SlaveState_UpdateSensors(snapshot);

        vTaskDelay(pdMS_TO_TICKS(TASK_CURRENT_PERIOD_MS));
    }
}
