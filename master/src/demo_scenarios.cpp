#include "demo_scenarios.h"
#include "app_mode.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Helper: set all demo injection variables atomically
// ---------------------------------------------------------------------------
static void demo_set(float temp, float flow, bool ui_on,
                     bool stop_comms, bool fault_sim) {
    demo_temp        = temp;
    demo_flow        = flow;
    demo_ui_on       = ui_on;
    demo_stop_comms  = stop_comms;
    demo_fault_sim   = fault_sim;
}

// ---------------------------------------------------------------------------
//  TaskAutomatedTestBench
//
//  Runs 8 scenarios in sequence.  Only active when appMode == APP_MODE_DEMO.
//  Logs each scenario boundary to Serial so the tester can observe transitions.
//
//  CATEGORY A — Normal Lifecycle
//    S1  Pre-Heating (Tank Only)            8 s
//    S2  Cold Shower Start (Boost Active)  10 s
//    S3  Warm Shower (Boost Cutoff)         8 s
//    S4  Standby Mode                       6 s
//  CATEGORY B — Smart Environmental Integration
//    S5  Predictive Solar Bypass           12 s  (linear temp sweep 28→42 °C)
//  CATEGORY C — Fault Tolerance & Safety
//    S6  PLC Communication Loss             6 s  (TX suppressed → slave timeout)
//    S7  Critical Overtemp Cutoff           8 s  (temp sweep 75→87 °C)
//    S8  Stuck SSR Triac Detection          6 s  (slave fakes 13.6A with cmds=0)
//
//  Between scenarios: 2-second pause + serial announcement.
//  After all 8: 5-second rest then cycle restarts.
// ---------------------------------------------------------------------------
void TaskAutomatedTestBench(void* pvParameters) {
    // Allow system to fully boot and first PLC CMD-STATUS handshake to complete
    vTaskDelay(pdMS_TO_TICKS(6000));
    Serial.println("[BENCH] Automated test bench task started.");

    for (;;) {
        // Only execute when in demo mode
        if (appMode != APP_MODE_DEMO) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        Serial.println("\n[BENCH] ============================================");
        Serial.println("[BENCH]  AUTOMATED TEST BENCH — starting 8-scenario run");
        Serial.println("[BENCH] ============================================");

        // ---------------------------------------------------------------
        // CATEGORY A  —  Normal Lifecycle & System Logic
        // ---------------------------------------------------------------

        // Scenario 1: Pre-Heating (Tank Only)  — 8 s
        Serial.println("\n[BENCH] --- S1: Pre-Heating (Tank Only) [8s] ---");
        Serial.println("[BENCH]  Expected: STATE_HEATING_TANK | pwmInt=100 pwmBst=0");
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(8000));

        // Scenario 2: Cold Shower Start (Boost Active)  — 10 s
        Serial.println("\n[BENCH] --- S2: Cold Shower Start (Boost Active) [10s] ---");
        Serial.println("[BENCH]  Expected: STATE_SHOWER_BOOST | pwmInt=0 pwmBst=100");
        demo_set(35.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(10000));

        // Scenario 3: Warm Shower (Boost Cutoff)  — 8 s
        //  temp=46°C > BOOST_CUTOFF_C(45°C) → boost drops to 0 even with flow
        Serial.println("\n[BENCH] --- S3: Warm Shower (Boost Cutoff) [8s] ---");
        Serial.println("[BENCH]  Expected: STATE_SHOWER_BOOST | pwmInt=0 pwmBst=0 (warm enough)");
        demo_set(46.0f, 6.5f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(8000));

        // Scenario 4: Standby Mode  — 6 s
        Serial.println("\n[BENCH] --- S4: Standby Mode [6s] ---");
        Serial.println("[BENCH]  Expected: STATE_STANDBY | pwmInt=0 pwmBst=0");
        demo_set(42.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(6000));

        // ---------------------------------------------------------------
        // CATEGORY B  —  Smart Environmental Integration
        // ---------------------------------------------------------------

        // Scenario 5: Predictive Solar Bypass  — 12 s (temp sweep 28→42 °C)
        Serial.println("\n[BENCH] --- S5: Predictive Solar Bypass [12s, temp 28->42 C] ---");
        Serial.println("[BENCH]  Expected: STATE_STANDBY throughout (no heating needed)");
        for (int i = 0; i <= 12; i++) {
            float t = 28.0f + (14.0f * (float)i / 12.0f);
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // ---------------------------------------------------------------
        // CATEGORY C  —  Fault Tolerance & Safety Interlocks
        // ---------------------------------------------------------------

        // Scenario 6: PLC Communication Loss  — 6 s
        Serial.println("\n[BENCH] --- S6: PLC Communication Loss [6s] ---");
        Serial.println("[BENCH]  Expected: master SAFETY_OVERRIDE; slave fault after 5s silence");
        demo_set(30.0f, 0.0f, true, true, false);   // stop_comms=true
        vTaskDelay(pdMS_TO_TICKS(6000));
        demo_stop_comms = false;                      // resume TX
        Serial.println("[BENCH]  S6: TX resumed");
        vTaskDelay(pdMS_TO_TICKS(2000));              // let slave re-sync

        // Scenario 7: Critical Overtemp Cutoff  — 8 s (sweep 75→87 °C)
        Serial.println("\n[BENCH] --- S7: Critical Overtemp Cutoff [8s, temp 75->87 C] ---");
        Serial.println("[BENCH]  Expected: slave safety trip @80C; master SAFETY_OVERRIDE @85C");
        for (int i = 0; i <= 8; i++) {
            float t = 75.0f + ((float)i / 8.0f) * 12.0f;  // 75 → 87 °C
            demo_set(t, 0.0f, true, false, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        // Reset to safe temp; give slave fault 2s to auto-clear
        demo_set(25.0f, 0.0f, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Scenario 8: Stuck SSR Triac Detection  — 6 s
        Serial.println("\n[BENCH] --- S8: Stuck SSR Triac Detection [6s] ---");
        Serial.println("[BENCH]  Expected: slave reports 13.6A with cmds=0 -> FAULT sts=0x08");
        demo_set(25.0f, 0.0f, false, false, true);   // ui_on=false (cmds 0%); fault_sim=true
        vTaskDelay(pdMS_TO_TICKS(6000));
        demo_set(25.0f, 0.0f, false, false, false);  // clear fault sim

        Serial.println("\n[BENCH] ============================================");
        Serial.println("[BENCH]  All 8 scenarios complete. Restarting in 5s.");
        Serial.println("[BENCH] ============================================\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
