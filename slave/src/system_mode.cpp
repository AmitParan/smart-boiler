#include "system_mode.h"
#include <Arduino.h>

// Default to bench-test so the board is safe to flash without sensors connected.
volatile SystemMode currentMode = MODE_BENCH_TEST;

// ---------------------------------------------------------------------------
//  TaskSerial
//  Lowest-priority helper task — watches the Serial console and switches
//  currentMode on demand.  Runs every 100ms (no blocking reads).
// ---------------------------------------------------------------------------
void TaskSerial(void* pvParameters) {
    Serial.println("[MODE] Serial control ready.  'b'=BENCH_TEST  'p'=PRODUCTION  '?'=status");
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            switch (c) {
                case 'b':
                    currentMode = MODE_BENCH_TEST;
                    Serial.println("[MODE] Switched → MODE_BENCH_TEST "
                                   "(flow + current interlocks bypassed)");
                    break;
                case 'p':
                    currentMode = MODE_PRODUCTION;
                    Serial.println("[MODE] Switched → MODE_PRODUCTION "
                                   "(all interlocks active)");
                    break;
                case '?':
                    Serial.printf("[MODE] Current mode: %s\n",
                                  currentMode == MODE_BENCH_TEST
                                      ? "BENCH_TEST"
                                      : "PRODUCTION");
                    break;
                default:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
