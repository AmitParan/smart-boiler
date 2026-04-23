#include "current_task.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

static const int numSamples = 100;

void TaskCurrent(void * pvParameters) {
    // Configure ADC: full 3.3V range on the current sensor pin
    analogSetPinAttenuation(PIN_CURRENT_SENSOR, ADC_11db);
    analogReadResolution(12);

    // -----------------------------------------------------------------------
    //  Zero-current calibration
    //  SSRs are off at boot — average 200 samples to find the actual quiescent
    //  voltage of this specific sensor (ACS758 tolerance ±1% on VREF).
    // -----------------------------------------------------------------------
    Serial.println("[CURRENT] Calibrating zero reference...");
    float vref_sum = 0;
    for (int i = 0; i < 200; i++) {
        float v_adc = (analogRead(PIN_CURRENT_SENSOR) / 4095.0f) * 3.3f;
        vref_sum += v_adc / CURRENT_DIVIDER_RATIO;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    float vref_actual = vref_sum / 200.0f;
    Serial.printf("[CURRENT] Calibrated VREF = %.3fV (expected %.3fV)\n",
                  vref_actual, ACS758_VREF);
    Serial.println("[CURRENT] Task started");

    for(;;) {
        float sumSq = 0;

        // Sample the ADC to capture AC waveform (50 Hz)
        for (int i = 0; i < numSamples; i++) {
            float v_adc = (analogRead(PIN_CURRENT_SENSOR) / 4095.0f) * 3.3f;
            float v_sensor = v_adc / CURRENT_DIVIDER_RATIO;
            float current_instant = (v_sensor - vref_actual) / ACS758_SENSITIVITY;
            sumSq += (current_instant * current_instant);
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        float rms_current = sqrtf(sumSq / numSamples);

        // Filter noise floor
        if (rms_current < 0.2f) rms_current = 0.0f;

        current_rms  = rms_current;
        power_watts  = current_rms * 220.0f;

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
