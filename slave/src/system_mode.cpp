#include "system_mode.h"
#include <Arduino.h>

volatile SystemMode currentMode = MODE_DEMO;
volatile bool serialModeOverride = false;

void TaskSerial(void* pvParameters) {
    Serial.println("[MODE] Ready: 'd'=DEMO  'r'=REALTIME  '?'=status");
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            switch (c) {
                case 'd':
                    currentMode = MODE_DEMO;
                    serialModeOverride = true;
                    Serial.println("[MODE] -> DEMO (serial override — master CMD will overwrite within ~1s)");
                    break;
                case 'r':
                    currentMode = MODE_REALTIME;
                    serialModeOverride = true;
                    Serial.println("[MODE] -> REALTIME (serial override — master CMD will overwrite within ~1s)");
                    break;
                case '?': {
                    Serial.printf("[MODE] Current: %s%s\n",
                                  currentMode == MODE_DEMO ? "DEMO" : "REALTIME",
                                  serialModeOverride ? " (serial override pending)" : "");
                    break;
                }
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

