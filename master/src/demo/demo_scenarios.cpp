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

// Check mode and abort cycle if switched to REALTIME mid-cycle
#define DEMO_CHECK() if (appMode != MODE_DEMO) { demo_stop_comms = false; demo_fault_sim = false; demo_solar_active = false; continue; }

void TaskAutomatedTestBench(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(6000));
    Serial.println("[MASTER] Automated test bench started. Waiting for first CMD cycle...");

    for (;;) {
        if (appMode != MODE_DEMO) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        Serial.println("\n[MASTER] ************************************************************");
        Serial.println("[MASTER] ***          SMART BOILER - DEMO CYCLE START            ***");
        Serial.println("[MASTER] ************************************************************");

        // --- CATEGORY A ---
        // Values/timings aligned to Project Book section 9.1.8 (MATLAB scenarios).
        // Physical durations from the book (e.g. 1.4h heat-up, 5min standby) are
        // compressed to a ~60s live-demo window; injected values and intra-scenario
        // event timings (flow @30s, PLC cut @30s, overtemp rate) match the book.

        banner("CATEGORY A | SCENARIO 1: Pre-Heating (Tank Only) [60s]",
               "Injection: UI=ON  FLOW=0.0  TEMP=20.0C  PLC=OK",
               "STATE_HEATING_TANK | SSR_INT ON | SSR_BST OFF | PWR: 2500W");
        demo_set(20.0f, 0.0f, true, false, false);   // book S1: cold tank 20C
        vTaskDelay(pdMS_TO_TICKS(60000));
        DEMO_CHECK();

        banner("CATEGORY A | SCENARIO 2: Preheated Shower + Inline Boost [60s]",
               "Injection: UI=ON  TEMP=40.0C  FLOW 0.0->8.0 LPM at t=30s  PLC=OK",
               "t<30s STANDBY | t>=30s STATE_SHOWER_BOOST: SSR_INT OFF SSR_BST ON 3000W");
        demo_set(40.0f, 0.0f, true, false, false);   // book S2: tank preheated to 40C, no flow yet
        vTaskDelay(pdMS_TO_TICKS(30000));
        DEMO_CHECK();
        demo_set(40.0f, 8.0f, true, false, false);   // book S2: flow opens 8 LPM at t=30s
        vTaskDelay(pdMS_TO_TICKS(30000));
        DEMO_CHECK();

        banner("CATEGORY A | SCENARIO 3: Warm Shower - Dynamic Cutoff [60s]",
               "Injection: UI=ON  TEMP=45.0C  FLOW 0.0->8.0 LPM at t=30s  PLC=OK",
               "SHOWER_BOOST | SSR_INT OFF | SSR_BST OFF (tank warm) | PWR: 0W");
        // book S3: tank starts 45C; boost stays off while tank above the dynamic
        // target. NOTE: book's dynamic target is 42C; firmware BOOST_CUTOFF_C=45C
        // (SystemManager) -- threshold reconciliation is a separate task.
        demo_set(45.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(30000));
        DEMO_CHECK();
        demo_set(45.0f, 8.0f, true, false, false);   // book S3: flow opens 8 LPM at t=30s
        vTaskDelay(pdMS_TO_TICKS(30000));
        DEMO_CHECK();

        banner("CATEGORY A | SCENARIO 4: Redundant Request - Standby [60s]",
               "Injection: UI=ON  FLOW=0.0  TEMP=42.0C  PLC=OK",
               "STATE_STANDBY | SSR_INT OFF | SSR_BST OFF | PWR: 0W");
        demo_set(42.0f, 0.0f, true, false, false);   // book S4: tank 42C > target 40C -> blocked
        vTaskDelay(pdMS_TO_TICKS(60000));
        DEMO_CHECK();

        // --- CATEGORY B ---

        banner("CATEGORY B | SCENARIO 5: Predictive Solar Bypass [~21s]",
               "Injection: SOLAR_ACTIVE=true  FLOW=0.0  TEMP: sweep 28->42C (target 40C)",
               "Both SSRs FORCED to 0% throughout - boiler yields to solar prediction");
        // book S5 is a 24h energy comparison (shower 19:00, 8 LPM); not reproducible
        // in a live demo, so this is a compressed representation of the core behaviour:
        // solar sufficient -> electric heaters stay off while water warms to target.
        demo_solar_active = true;
        for (int i = 0; i <= 20; i++) {
            if (appMode != MODE_DEMO) break;
            float t = 28.0f + (14.0f * (float)i / 20.0f);  // 28 -> 42 C
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        demo_solar_active = false;
        DEMO_CHECK();

        // --- CATEGORY C ---

        banner("CATEGORY C | SCENARIO 6: PLC Communication Loss [60s]",
               "Injection: TEMP=20.0C | idle 0-15s, heating 15-30s, TX SUPPRESSED at t=30s",
               "After cut: Slave TaskSafety HARD-CUTOFF (5s watchdog) -> SAFETY_OVERRIDE, 0W");
        // Models S6: t_active=15s (idle->demand), t_loss=30s (PLC link drops).
        // NOTE: Models 9.1.8 (MATLAB) idealises the watchdog at <50ms; the real firmware
        // (and book section 10) uses a 5s no-STATUS timeout -- 5s is correct here.
        demo_set(20.0f, 0.0f, false, false, false);  // 0-15s: idle (no demand)
        vTaskDelay(pdMS_TO_TICKS(15000));
        DEMO_CHECK();
        demo_set(20.0f, 0.0f, true, false, false);   // 15-30s: demand + link OK -> heating
        vTaskDelay(pdMS_TO_TICKS(15000));
        DEMO_CHECK();
        demo_set(20.0f, 0.0f, true, true, false);    // t=30s: TX suppressed (link cut)
        vTaskDelay(pdMS_TO_TICKS(30000));
        demo_stop_comms = false;
        DEMO_CHECK();
        Serial.println("[MASTER] S6: TX resumed - waiting for slave resync...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        DEMO_CHECK();

        banner("CATEGORY C | SCENARIO 7: Critical Overtemp Cutoff [~46s]",
               "Injection: TEMP sweep 70->88C at 0.4C/s (SW cutoff @80C ~25s, HW interlock @85C ~37.5s)",
               "Slave FAULT @80C (software) + LM393N simulation @85C");
        // book S7: base 70C, thermal runaway at 0.4C/s. SW cut @80C (~25s, Case 7A),
        // HW interlock @85C (~37.5s, Case 7B).
        for (int i = 0; i <= 45; i++) {
            if (appMode != MODE_DEMO) break;
            float t = 70.0f + 0.4f * (float)i;   // 70 -> 88 C at 0.4 C/s
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(2000));
        DEMO_CHECK();

        banner("CATEGORY C | SCENARIO 8: Stuck SSR Triac Detection [60s]",
               "Injection: UI=OFF  FLOW=0  TEMP=70.0C | Slave forces 13.6A despite OFF cmd",
               "Slave TaskSafety UNCOMMANDED CURRENT fault within 50ms");
        demo_set(70.0f, 0.0f, false, false, true);   // book S8: boiler hot (70C), stuck triac 13.6A
        vTaskDelay(pdMS_TO_TICKS(60000));
        demo_set(25.0f, 0.0f, false, false, false);
        DEMO_CHECK();

        Serial.println("\n[MASTER] ************************************************************");
        Serial.println("[MASTER] ***          DEMO CYCLE COMPLETE. Restart in 5s.         ***");
        Serial.println("[MASTER] ************************************************************\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

