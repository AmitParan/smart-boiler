#include "system_mode.h"
#include <Arduino.h>

volatile SystemMode currentMode = MODE_BENCH_TEST;

void TaskSerial(void* pvParameters) {
    Serial.println("[MODE] Serial control ready: b=BENCH_TEST  d=DEMO  p=PRODUCTION  ?=status");
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            switch (c) {
                case 'b':
                    currentMode = MODE_BENCH_TEST;
                    Serial.println("[MODE] -> BENCH_TEST (interlocks+sensors bypassed)");
                    break;
                case 'd':
                    currentMode = MODE_DEMO;
                    Serial.println("[MODE] -> DEMO (mock sensors, interlocks active)");
                    break;
                case 'p':
                    currentMode = MODE_PRODUCTION;
                    Serial.println("[MODE] -> PRODUCTION (real sensors, all interlocks)");
                    break;
                case '?': {
                    const char* names[] = {"BENCH_TEST", "DEMO", "PRODUCTION"};
                    Serial.printf("[MODE] Current: %s\n", names[(int)currentMode]);
                    break;
                }
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
