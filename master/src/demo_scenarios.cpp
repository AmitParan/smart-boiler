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

static void banner(const char* cat, const char* title, const char* expected) {
    Serial.println("\n[MASTER] ============================================================");
    Serial.printf( "[MASTER] %s\n", cat);
    Serial.printf( "[MASTER] %s\n", title);
    Serial.printf( "[MASTER] Expected: %s\n", expected);
    Serial.println("[MASTER] ============================================================");
}

void TaskAutomatedTestBench(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(6000));
    Serial.println("[MASTER] Automated test bench started. Waiting for first CMD cycle...");

    for (;;) {
        if (appMode != APP_MODE_DEMO) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        Serial.println("\n[MASTER] ************************************************************");
        Serial.println("[MASTER] ***          SMART BOILER - DEMO CYCLE START            ***");
        Serial.println("[MASTER] ************************************************************");

        // --- CATEGORY A: Normal Lifecycle ---

        banner("CATEGORY A | SCENARIO 1: Pre-Heating (Tank Only) [60s]",
               "Injection: UI=ON  FLOW=0.0  TEMP=25.0C  PLC=OK",
               "STATE_HEATING_TANK | SSR_INT ON | SSR_BST OFF | PWR: 2500W");
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        banner("CATEGORY A | SCENARIO 2: Cold Shower Start (Boost Activated) [60s]",
               "Injection: UI=ON  FLOW=6.5  TEMP=35.0C  PLC=OK",
               "STATE_SHOWER_BOOST | SSR_INT OFF | SSR_BST ON  | PWR: 3000W");
        demo_set(35.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        banner("CATEGORY A | SCENARIO 3: Warm Shower - Boost Cutoff [60s]",
               "Injection: UI=ON  FLOW=6.5  TEMP=46.0C (above 45C cutoff)  PLC=OK",
               "STATE_SHOWER_BOOST | SSR_INT OFF | SSR_BST OFF | PWR: 0W (Warm Enough)");
        demo_set(46.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        banner("CATEGORY A | SCENARIO 4: Redundant Request - Standby [60s]",
               "Injection: UI=ON  FLOW=0.0  TEMP=42.0C  PLC=OK",
               "STATE_STANDBY | SSR_INT OFF | SSR_BST OFF | PWR: 0W");
        demo_set(42.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(60000));

        // --- CATEGORY B: Smart Environmental ---

        banner("CATEGORY B | SCENARIO 5: Predictive Solar Bypass [60s]",
               "Injection: SOLAR_ACTIVE=true  FLOW=0.0  TEMP: sweep 28->42C",
               "STATE_STANDBY throughout | Both SSRs OFF | Solar heating simulation");
        demo_solar_active = true;
        for (int i = 0; i <= 20; i++) {
            float t = 28.0f + (14.0f * (float)i / 20.0f);
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        demo_solar_active = false;

        // --- CATEGORY C: Fault Tolerance ---

        banner("CATEGORY C | SCENARIO 6: PLC Communication Loss [60s]",
               "Injection: TX SUPPRESSED for 60s to simulate link dropout",
               "Slave TaskSafety fires HARD-CUTOFF after 5s silence");
        demo_set(30.0f, 0.0f, true, true, false);
        vTaskDelay(pdMS_TO_TICKS(60000));
        demo_stop_comms = false;
        Serial.println("[MASTER] S6: TX resumed - waiting for slave resync...");
        vTaskDelay(pdMS_TO_TICKS(2000));

        banner("CATEGORY C | SCENARIO 7: Critical Overtemp Cutoff [60s]",
               "Injection: TEMP sweep 75->87C (software cutoff @80C, HW interlock @86C)",
               "Slave FAULT @80C (software) + LM393N simulation @86C");
        for (int i = 0; i <= 20; i++) {
            float t = 75.0f + ((float)i / 20.0f) * 12.0f;
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(2000));

        banner("CATEGORY C | SCENARIO 8: Stuck SSR Triac Detection [60s]",
               "Injection: UI=OFF  FLOW=0  TEMP=25C | Slave forces 13.6A (3000W) despite OFF",
               "Slave TaskSafety UNCOMMANDED CURRENT fault within 50ms");
        demo_set(25.0f, 0.0f, false, false, true);
        vTaskDelay(pdMS_TO_TICKS(60000));
        demo_set(25.0f, 0.0f, false, false, false);

        Serial.println("\n[MASTER] ************************************************************");
        Serial.println("[MASTER] ***          DEMO CYCLE COMPLETE. Restart in 5s.         ***");
        Serial.println("[MASTER] ************************************************************\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
