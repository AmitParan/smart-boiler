#include "current_task.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

// Sampling variables for AC RMS calculation
const int numSamples = 100;

void TaskCurrent(void * pvParameters) {
    for(;;) {
        float sumSq = 0;
        
        // Sample the ADC quickly to catch the AC wave (50Hz)
        for (int i = 0; i < numSamples; i++) {
            // Read ADC (12-bit, 0-4095, 3.3V reference)
            int adc_raw = analogRead(PIN_CURRENT_SENSOR);

            // Step 1: ADC count → voltage at the ADC pin
            float v_adc = (adc_raw / 4095.0f) * 3.3f;

            // Step 2: Reverse the hardware voltage divider (1.8kΩ / 3.3kΩ)
            //         V_sensor = V_adc × (R1+R2)/R2 = V_adc / CURRENT_DIVIDER_RATIO
            float v_sensor = v_adc / CURRENT_DIVIDER_RATIO;

            // Step 3: ACS758LCB-050B on 5V — 40mV/A, quiescent at 2.5V
            float current_instant = (v_sensor - ACS758_VREF) / ACS758_SENSITIVITY;
            
            sumSq += (current_instant * current_instant);
            vTaskDelay(pdMS_TO_TICKS(1)); // 1ms delay between samples
        }
        
        // Calculate RMS Current
        float rms_current = sqrt(sumSq / numSamples);
        
        // Filter out noise close to 0
        if (rms_current < 0.2) rms_current = 0.0;
        
        // Safely update shared variable (assuming you have a mutex or using FreeRTOS atomic)
        current_rms = rms_current;
        
        // Power calculation (assuming 220V AC)
        power_watts = current_rms * 220.0;
        
        vTaskDelay(pdMS_TO_TICKS(500)); // Run twice a second
    }
}
