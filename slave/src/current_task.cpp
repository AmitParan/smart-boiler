#include "current_task.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

static const int numSamples = 100;

void TaskCurrent(void * pvParameters) {
    // Initialize pin as analog first, then set attenuation
    analogReadResolution(12);
    analogRead(PIN_CURRENT_SENSOR);                              // primes the pin
    analogSetPinAttenuation(PIN_CURRENT_SENSOR, ADC_11db);

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

    // Derive actual VCC from measured VREF (ACS758: VREF = VCC/2)
    float vcc_actual = vref_actual * 2.0f;
    // Sensitivity scales linearly with VCC (spec is 40mV/A at 5V)
    float sensitivity_actual = ACS758_SENSITIVITY * (vcc_actual / 5.0f);

    Serial.printf("[CURRENT] Calibrated VREF = %.3fV (expected %.3fV)\n",
                  vref_actual, ACS758_VREF);
    Serial.printf("[CURRENT] VCC = %.3fV  sensitivity = %.1f mV/A (nominal 40.0)\n",
                  vcc_actual, sensitivity_actual * 1000.0f);
    Serial.println("[CURRENT] Task started");

    for(;;) {
        float sumSq = 0;

        // Sample the ADC to capture AC waveform (50 Hz)
        for (int i = 0; i < numSamples; i++) {
            float v_adc = (analogRead(PIN_CURRENT_SENSOR) / 4095.0f) * 3.3f;
            float v_sensor = v_adc / CURRENT_DIVIDER_RATIO;
            float current_instant = (v_sensor - vref_actual) / sensitivity_actual;
            sumSq += (current_instant * current_instant);
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        float rms_current = sqrtf(sumSq / numSamples);

        // Filter noise floor
        if (rms_current < 0.2f) rms_current = 0.0f;

        if (xSemaphoreTake(mutex_current, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_rms = rms_current;
            power_watts = rms_current * 220.0f;
            xSemaphoreGive(mutex_current);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
