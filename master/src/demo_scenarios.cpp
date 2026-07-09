#include "demo_scenarios.h"
#include "app_mode.h"
#include <Arduino.h>

static void demo_set(float temp, float flow, bool ui_on,
                     bool stop_comms, bool fault_sim) {
    demo_temp        = temp;
    demo_flow        = flow;
    demo_ui_on       = ui_on;
    demo_stop_comms  = stop_comms;
    demo_fault_sim   = fault_sim;
}

void TaskAutomatedTestBench(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(6000));
    Serial.println("[BENCH] Automated test bench started.");

    for (;;) {
        if (appMode != APP_MODE_DEMO) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        Serial.println("\n[BENCH] ====== DEMO CYCLE START ======");

        // S1: Pre-Heating - 60s
        Serial.println("[BENCH] S1: Pre-Heating | STATE_HEATING_TANK | SSR_INT ON");
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        // S2: Cold Shower Start - 60s
        Serial.println("[BENCH] S2: Cold Shower | STATE_SHOWER_BOOST | SSR_EXT ON");
        demo_set(35.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        // S3: Warm Shower Cutoff - 60s
        Serial.println("[BENCH] S3: Warm Shower | BOOST_CUTOFF (46C>45C) | Both OFF");
        demo_set(46.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        // S4: Standby - 60s
        Serial.println("[BENCH] S4: Standby | STATE_STANDBY | Both OFF");
        demo_set(42.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        // S5: Solar Bypass - 60s with temp sweep 28->42
        Serial.println("[BENCH] S5: Solar Bypass | STATE_STANDBY | temp sweep 28->42C");
        for (int i = 0; i <= 20; i++) {
            float t = 28.0f + (14.0f * (float)i / 20.0f);
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // S6: PLC Loss - 60s
        Serial.println("[BENCH] S6: PLC Loss | TX suppressed | Slave fault after 5s");
        demo_set(30.0f, 0.0f, true, true, false);
        vTaskDelay(pdMS_TO_TICKS(60000));
        demo_stop_comms = false;
        vTaskDelay(pdMS_TO_TICKS(2000));  // let slave resync

        // S7: Overtemp - 60s with sweep 75->87
        Serial.println("[BENCH] S7: Overtemp | sweep 75->87C | Slave FAULT@80C, Master OVERRIDE@85C");
        for (int i = 0; i <= 20; i++) {
            float t = 75.0f + ((float)i / 20.0f) * 12.0f;
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // S8: Stuck SSR - 60s
        Serial.println("[BENCH] S8: Stuck SSR | cmds=0 but slave reports 13.6A | FAULT sts=0x08");
        demo_set(25.0f, 0.0f, false, false, true);
        vTaskDelay(pdMS_TO_TICKS(60000));
        demo_set(25.0f, 0.0f, false, false, false);

        Serial.println("[BENCH] ====== DEMO CYCLE COMPLETE. Restarting in 5s. ======\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
