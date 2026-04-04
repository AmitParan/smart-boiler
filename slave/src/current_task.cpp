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
            // Read ADC (assuming 12-bit on ESP32, 0-4095)
            int adc_raw = analogRead(PIN_CURRENT_SENSOR);
            
            // Convert ADC to Voltage
            float voltage = (adc_raw / 4095.0) * 3.3; 
            
            // ACS758LCB-050B outputs 40mV/A, centered at VCC/2 (approx 1.65V)
            float current_instant = (voltage - 1.65) / 0.040;
            
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
