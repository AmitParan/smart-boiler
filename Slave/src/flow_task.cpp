#include "flow_task.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

// משתנה פנימי לספירת פולסים (רק הקובץ הזה מכיר אותו)
volatile int flow_pulse_count = 0;

// פסיקה (Interrupt)
void IRAM_ATTR pulseCounter() {
    portENTER_CRITICAL_ISR(&timerMux);
    flow_pulse_count++;
    portEXIT_CRITICAL_ISR(&timerMux);
}

void TaskFlow(void * pvParameters) {
    pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), pulseCounter, RISING);

    for(;;) {
        // מדידה למשך שנייה
        vTaskDelay(pdMS_TO_TICKS(1000));

        // קריאה בטוחה ואיפוס
        int local_count;
        portENTER_CRITICAL(&timerMux);
        local_count = flow_pulse_count;
        flow_pulse_count = 0;
        portEXIT_CRITICAL(&timerMux);

        // חישוב (מותאם ל-YF-B6)
        float flow = ((float)local_count) / 6.6;
        if(flow < 0.5) flow = 0.0; // סינון רעשים

        // עדכון המשתנה המשותף
        current_flow = flow;
    }
}