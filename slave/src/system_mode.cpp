#include "system_mode.h"
#include <Arduino.h>

volatile SystemMode currentMode = MODE_DEMO;

void TaskSerial(void* pvParameters) {
    Serial.println("[MODE] Ready: 'd'=DEMO  'r'=REALTIME  '?'=status");
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            switch (c) {
                case 'd':
                    currentMode = MODE_DEMO;
                    Serial.println("[MODE] -> DEMO (mock sensors, auto-clear faults)");
                    break;
                case 'r':
                    currentMode = MODE_REALTIME;
                    Serial.println("[MODE] -> REALTIME (real sensors, permanent faults)");
                    break;
                case '?': {
                    Serial.printf("[MODE] Current: %s\n",
                                  currentMode == MODE_DEMO ? "DEMO" : "REALTIME");
                    break;
                }
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

