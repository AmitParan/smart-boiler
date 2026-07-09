// =============================================================================
// [TEST FILE] SystemManagerTest.cpp
// PURPOSE : Standalone test bench for SystemManager state machine.
// ENABLE  : Set #define TEST_MODE 1 in master/src/main.cpp
// DISABLE : TEST_MODE 0 (default) — this file is compiled but never runs.
// DO NOT  : Enable in production. For development validation only.
// =============================================================================
#include "SystemManagerTest.h"
#include "SystemManager.h"
#include "ui_manager.h"
#include <Arduino.h>

// ===========================================================================
//  SystemManager Test Bench
//
//  Runs a fixed sequence of scenarios that cover every state and every edge.
//  Each scenario is held for SCENARIO_HOLD_MS so you can observe the screen
//  and the serial log before the next one starts.
//
//  Enable with: #define TEST_MODE 1  in main.cpp
//  Serial output format (115200 baud):
//
//  ============================================================
//  [TEST  3/8] STATE_HEATING  (full power)
//  ============================================================
//    currentTemp  :  40.0 °C
//    flowRateLPM  :   0.0 L/min
//    targetTemp   :  60.0 °C
//    uiStateOn    : ON
//    plcConnected : YES
//  ------------------------------------------------------------
//    pwmInternal  : 100 %
//    pwmBoost     :   0 %
//    state        : STATE_HEATING   ✓ PASS
//  ============================================================
//
// ===========================================================================

static constexpr uint32_t SCENARIO_HOLD_MS = 5000u;  // 5 s per scenario

// ---------------------------------------------------------------------------
//  Test scenario descriptor
// ---------------------------------------------------------------------------
struct Scenario {
    const char*    name;
    SystemInputs   in;
    BoilerState    expectedState;
    uint8_t        expectedPwmInternal;  // exact value, or 0xFF = "any non-zero"
    uint8_t        expectedPwmBoost;     // exact value, or 0xFF = "any non-zero"
};

// ---------------------------------------------------------------------------
//  Scenario table  (10 scenarios, every state + every boundary)
//
//  SystemInputs layout: { currentTemp, flowRateLPM, targetShowerTemp, uiStateOn, plcConnected }
//  TARGET_TANK_TEMP = 40.0 °C  (hardcoded in SystemManager)
//
//  ID  Description
//  --  -----------
//   1  SAFETY: PLC lost                 → SAFETY_OVERRIDE, both=0
//   2  SAFETY: Overtemp 86 °C           → SAFETY_OVERRIDE, both=0
//   3  SAFETY: Overtemp boundary 85 °C  → SAFETY_OVERRIDE, both=0
//   4  OFF: user pressed OFF             → STATE_OFF, both=0
//   5  HEATING: cold tank (20 °C < 40)  → STATE_HEATING_TANK, int=100, bst=0
//   6  HEATING: just below base (39 °C) → STATE_HEATING_TANK, int=100, bst=0
//   7  STANDBY: tank exactly at base (40°C) → STATE_STANDBY, both=0
//   8  STANDBY: tank above base (55 °C) → STATE_STANDBY, both=0
//   9  SHOWER BOOST: flow=7.5, cold tank → STATE_SHOWER_BOOST, int=0, bst=100
//  10  SHOWER BOOST: flow=7.5, warm tank → STATE_SHOWER_BOOST, int=0, bst=100
//      (internal must be 0 even if tank is cold — breaker protection)
//  11  SAFETY beats SHOWER: overtemp + flow → SAFETY_OVERRIDE
// ---------------------------------------------------------------------------
static const Scenario kScenarios[] = {
    // 1 — SAFETY: PLC disconnected  [MASTER-ONLY — cannot be driven by slave test sender]
    {
        "[MASTER-ONLY] SAFETY: PLC lost",
        { 35.0f, 0.0f, 60.0f, true, false },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 2 — SAFETY: overtemp above cutoff
    {
        "SAFETY: Overtemp (86 degC)",
        { 86.0f, 0.0f, 60.0f, true, true },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 3 — SAFETY: overtemp exactly at cutoff (>=85 trips)
    {
        "SAFETY: Overtemp boundary (85.0 degC)",
        { 85.0f, 0.0f, 60.0f, true, true },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 4 — STATE_OFF  [MASTER-ONLY — UI state, cannot be driven by slave test sender]
    {
        "[MASTER-ONLY] STATE_OFF: user pressed OFF",
        { 35.0f, 0.0f, 60.0f, false, true },
        BoilerState::STATE_OFF, 0, 0
    },
    // 5 — STATE_HEATING_TANK: cold tank (20 °C), no flow
    {
        "STATE_HEATING_TANK: cold start (20 degC < 40 base)",
        { 20.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_HEATING_TANK, 100, 0
    },
    // 6 — STATE_HEATING_TANK: just below base temp (39 °C)
    {
        "STATE_HEATING_TANK: near base temp (39 degC)",
        { 39.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_HEATING_TANK, 100, 0
    },
    // 7 — STATE_STANDBY: tank exactly at base temp
    {
        "STATE_STANDBY: tank at base temp (40 degC)",
        { 40.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_STANDBY, 0, 0
    },
    // 8 — STATE_STANDBY: tank warmer than base (e.g. residual heat)
    {
        "STATE_STANDBY: tank above base temp (55 degC)",
        { 55.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_STANDBY, 0, 0
    },
    // 9 — STATE_SHOWER_BOOST: tap open, cold tank
    //     internal MUST be 0 even though tank is cold (breaker protection rule)
    {
        "STATE_SHOWER_BOOST: flow=7.5 L/min, cold tank (20 degC)",
        { 20.0f, 7.5f, 60.0f, true, true },
        BoilerState::STATE_SHOWER_BOOST, 0, 100
    },
    // 10 — STATE_SHOWER_BOOST: tap open, warm tank
    {
        "STATE_SHOWER_BOOST: flow=7.5 L/min, warm tank (40 degC)",
        { 40.0f, 7.5f, 60.0f, true, true },
        BoilerState::STATE_SHOWER_BOOST, 0, 100
    },
    // 11 — SAFETY beats SHOWER_BOOST: overtemp + flow
    {
        "SAFETY beats SHOWER_BOOST: overtemp + flow",
        { 86.0f, 7.5f, 60.0f, true, true },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
};

static constexpr uint8_t kNumScenarios =
    (uint8_t)(sizeof(kScenarios) / sizeof(kScenarios[0]));

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static void printSeparator() {
    Serial.println("============================================================");
}

static void printResult(uint8_t idx, const Scenario& s,
                        const SystemCommand& cmd) {
    bool stateOk = (cmd.state == s.expectedState);
    bool intOk   = (s.expectedPwmInternal == 0xFF)
                   ? (cmd.pwmInternal > 0)
                   : (cmd.pwmInternal == s.expectedPwmInternal);
    bool bstOk   = (s.expectedPwmBoost == 0xFF)
                   ? (cmd.pwmBoost > 0)
                   : (cmd.pwmBoost == s.expectedPwmBoost);
    bool pass    = stateOk && intOk && bstOk;

    printSeparator();
    Serial.printf("[TEST %2u/%u] %s\n", idx + 1, kNumScenarios, s.name);
    printSeparator();
    Serial.printf("  INPUT  currentTemp      : %5.1f degC\n",  s.in.currentTemp);
    Serial.printf("  INPUT  flowRateLPM      : %5.1f L/min\n", s.in.flowRateLPM);
    Serial.printf("  INPUT  targetShowerTemp : %5.1f degC\n",  s.in.targetShowerTemp);
    Serial.printf("  INPUT  uiStateOn        : %s\n",          s.in.uiStateOn    ? "ON"  : "OFF");
    Serial.printf("  INPUT  plcConnected     : %s\n",          s.in.plcConnected ? "YES" : "NO");
    Serial.printf("  (base tank target hardcoded: %.1f degC)\n", TARGET_TANK_TEMP);
    Serial.println("  ------------------------------------------------------------");
    Serial.printf("  OUTPUT pwmInternal  : %3u %%\n",  cmd.pwmInternal);
    Serial.printf("  OUTPUT pwmBoost     : %3u %%\n",  cmd.pwmBoost);
    Serial.printf("  OUTPUT state        : %-22s  %s\n",
                  cmd.stateLabel, pass ? "PASS" : "*** FAIL ***");

    if (!stateOk)
        Serial.printf("         EXPECTED state       : %s\n",
                      SystemManager::labelFor(s.expectedState));
    if (!intOk)
        Serial.printf("         EXPECTED pwmInternal : %s\n",
                      s.expectedPwmInternal == 0xFF ? ">0" :
                      String(s.expectedPwmInternal).c_str());
    if (!bstOk)
        Serial.printf("         EXPECTED pwmBoost    : %s\n",
                      s.expectedPwmBoost == 0xFF ? ">0" :
                      String(s.expectedPwmBoost).c_str());
}

// ---------------------------------------------------------------------------
//  TaskSystemManagerTest — FreeRTOS entry point
// ---------------------------------------------------------------------------
void TaskSystemManagerTest(void* pvParameters) {
    SystemManager mgr;
    uint32_t      pass_count  = 0;
    uint32_t      fail_count  = 0;
    uint32_t      cycle       = 0;

    Serial.println("\n");
    printSeparator();
    Serial.println("  SystemManager TEST BENCH");
    Serial.printf( "  %u scenarios  |  %u s per scenario\n",
                   kNumScenarios, SCENARIO_HOLD_MS / 1000u);
    printSeparator();
    Serial.println("\n");

    for (;;) {
        cycle++;
        pass_count = 0;
        fail_count = 0;

        Serial.printf("\n>>> TEST CYCLE %u  START\n\n", cycle);

        for (uint8_t i = 0; i < kNumScenarios; i++) {
            const Scenario& s   = kScenarios[i];
            SystemCommand   cmd = mgr.process(s.in);

            printResult(i, s, cmd);

            // Mirror current scenario to the UI
            UI_UpdateSensorData(s.in.currentTemp, 0.0f,
                                s.in.flowRateLPM, 0.0f);
            UI_UpdatePLCStatus(s.in.plcConnected);

            // Accumulate pass/fail
            bool stateOk = (cmd.state == s.expectedState);
            bool intOk   = (s.expectedPwmInternal == 0xFF)
                           ? (cmd.pwmInternal > 0)
                           : (cmd.pwmInternal == s.expectedPwmInternal);
            bool bstOk   = (s.expectedPwmBoost == 0xFF)
                           ? (cmd.pwmBoost > 0)
                           : (cmd.pwmBoost == s.expectedPwmBoost);
            if (stateOk && intOk && bstOk) pass_count++;
            else                           fail_count++;

            Serial.printf("\n  Holding %u s...\n\n", SCENARIO_HOLD_MS / 1000u);
            vTaskDelay(pdMS_TO_TICKS(SCENARIO_HOLD_MS));
        }

        // Cycle summary
        printSeparator();
        Serial.printf("  CYCLE %u COMPLETE  —  %u PASS  /  %u FAIL  /  %u TOTAL\n",
                      cycle, pass_count, fail_count, kNumScenarios);
        if (fail_count == 0)
            Serial.println("  ALL PASS — SystemManager logic is CORRECT");
        else
            Serial.printf("  *** %u FAILURES — review output above ***\n", fail_count);
        printSeparator();
        Serial.printf("\n  Next cycle in 10 s...\n\n");
        vTaskDelay(pdMS_TO_TICKS(10000u));
    }
}

