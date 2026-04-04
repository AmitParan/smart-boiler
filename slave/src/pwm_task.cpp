#include "pwm_task.h"
#include "config.h"
#include "shared_data.h"

// Define the time window for the slow PWM (Burst Firing) in milliseconds
// 2000ms (2 seconds) is a standard window for AC thermal regulation
const int PWM_WINDOW_MS = 2000; 

void TaskPWM(void * pvParameters) {
    for(;;) {
        // Check if the manager requested the external heater, 
        // if there are no system faults, and if there is sufficient water flow
        if (ext_heater_requested && !system_fault && current_flow >= 1.0) {
            
            // 1. Calculate Temperature Error
            // temps[2] is the temperature sensor AFTER the external heater
            float temp_error = target_temp - temps[2];
            
            // 2. Proportional Control Logic (Determine Duty Cycle)
            int duty_cycle_percent = 0;
            
            if (temp_error >= 5.0) {
                // If water is much colder than target, apply full power
                duty_cycle_percent = 100; 
            } else if (temp_error > 0.0) {
                // Proportional band: Scale duty cycle between 0% and 100%
                // For example: 2.5C error -> 50% duty cycle
                duty_cycle_percent = (int)((temp_error / 5.0) * 100.0); 
            } else {
                // Target reached or exceeded, turn off heater
                duty_cycle_percent = 0; 
            }

            // 3. Calculate ON and OFF times based on the duty cycle
            int on_time_ms = (PWM_WINDOW_MS * duty_cycle_percent) / 100;
            int off_time_ms = PWM_WINDOW_MS - on_time_ms;

            // 4. Execute the PWM Cycle
            if (on_time_ms > 0) {
                digitalWrite(PIN_SSR_EXT, HIGH);
                vTaskDelay(pdMS_TO_TICKS(on_time_ms));
            }
            
            if (off_time_ms > 0) {
                digitalWrite(PIN_SSR_EXT, LOW);
                vTaskDelay(pdMS_TO_TICKS(off_time_ms));
            }
            
        } else {
            // Safety fallback: If not requested, faulted, or no flow - force OFF
            digitalWrite(PIN_SSR_EXT, LOW);
            
            // Sleep for a short time before checking the conditions again
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}
