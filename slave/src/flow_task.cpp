#include "flow_task.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include <Arduino.h>

// Internal variable for pulse counting (only this file knows it)
volatile int flow_pulse_count = 0;

// Interrupt
void IRAM_ATTR pulseCounter() {
    portENTER_CRITICAL_ISR(&timerMux);
    flow_pulse_count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}

void TaskFlow(void * pvParameters) {
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), pulseCounter, RISING);

    for(;;) {
        if (currentMode == MODE_DEMO) {
            // Mirror master-injected flow rate
            float mock_flow = slave_demo_flow_x10 / 10.0f;
            if (xSemaphoreTake(mutex_flow, pdMS_TO_TICKS(10)) == pdTRUE) {
                current_flow = mock_flow;
                xSemaphoreGive(mutex_flow);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Measure for one second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Safe read and reset
        int local_count;
        portENTER_CRITICAL(&timerMux);
        local_count = flow_pulse_count;
        flow_pulse_count = 0;
        portEXIT_CRITICAL(&timerMux);

        // Calculation (calibrated for YF-B6)
        float flow = ((float)local_count) / 6.6;
        if(flow < 0.5) flow = 0.0; // Filter noise

        // Update shared variable under mutex
        if (xSemaphoreTake(mutex_flow, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_flow = flow;
            xSemaphoreGive(mutex_flow);
        }
    }
}