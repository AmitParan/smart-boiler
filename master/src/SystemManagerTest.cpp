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
    uint8_t        expectedPwmInternal; // 0–100, or 0xFF = "any non-zero"
    uint8_t        expectedPwmBoost;    // 0–100, or 0xFF = "any non-zero"
};

// ---------------------------------------------------------------------------
//  Scenario table — every state + every boundary case
//
//  ID  Name                           Notes
//  --  ----                           -----
//   1  SAFETY: PLC lost               plcConnected=false — must cut all power
//   2  SAFETY: Overtemp               temp=86 >= 85 cutoff — must cut all power
//   3  SAFETY: Overtemp at boundary   temp=85.0 exactly — still trips
//   4  OFF: user pressed OFF          uiStateOn=false — must cut all power
//   5  HEATING: full power            delta=20°C >= 5°C window → 100%
//   6  HEATING: proportional 60%      delta=3°C inside window → ~60%
//   7  HEATING: proportional 1%       delta=0.05°C — nearly at target
//   8  STANDBY: tank at target        temp==target, no flow → all off
//   9  STANDBY: tank above target     temp > target, no flow → all off
//  10  BOOST: flow active, cold tank  pwmBoost=100, pwmInternal=100 (delta large)
//  11  BOOST: flow active, warm tank  pwmBoost=100, pwmInternal proportional
//  12  BOOST: flow active, hot tank   pwmBoost=100, pwmInternal=0 (already hot)
//  13  SAFETY beats ON+flow           overtemp + flow — safety must win
// ---------------------------------------------------------------------------
static const Scenario kScenarios[] = {
    // 1 — SAFETY: PLC disconnected
    {
        "SAFETY: PLC lost",
        { 40.0f, 0.0f, 60.0f, true, false },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 2 — SAFETY: overtemp (above cutoff)
    {
        "SAFETY: Overtemp (86 degC)",
        { 86.0f, 0.0f, 60.0f, true, true },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 3 — SAFETY: overtemp exactly at cutoff (85.0)
    {
        "SAFETY: Overtemp (85.0 degC boundary)",
        { 85.0f, 0.0f, 60.0f, true, true },
        BoilerState::SAFETY_OVERRIDE, 0, 0
    },
    // 4 — STATE_OFF: user turned boiler off
    {
        "STATE_OFF: uiStateOn=false",
        { 40.0f, 0.0f, 60.0f, false, true },
        BoilerState::STATE_OFF, 0, 0
    },
    // 5 — STATE_HEATING: full power (delta 20°C, well outside proportional window)
    {
        "STATE_HEATING: full power (delta=20 degC)",
        { 40.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_HEATING, 100, 0
    },
    // 6 — STATE_HEATING: proportional, delta=3°C (inside 5°C window → ~60%)
    {
        "STATE_HEATING: proportional ~60% (delta=3 degC)",
        { 57.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_HEATING, 0xFF, 0  // 0xFF = any non-zero
    },
    // 7 — STATE_HEATING: delta=0.25°C → ~5%
    {
        "STATE_HEATING: near target ~5% (delta=0.25 degC)",
        { 59.75f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_HEATING, 0xFF, 0
    },
    // 8 — STATE_STANDBY: temp exactly at target
    {
        "STATE_STANDBY: temp==target (60 degC)",
        { 60.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_STANDBY, 0, 0
    },
    // 9 — STATE_STANDBY: tank above target
    {
        "STATE_STANDBY: temp above target (65 degC)",
        { 65.0f, 0.0f, 60.0f, true, true },
        BoilerState::STATE_STANDBY, 0, 0
    },
    // 10 — STATE_BOOST: flow active, cold tank → both heaters running
    {
        "STATE_BOOST: cold tank + flow=7.5 L/min",
        { 40.0f, 7.5f, 60.0f, true, true },
        BoilerState::STATE_BOOST, 100, 100
    },
    // 11 — STATE_BOOST: flow active, warm tank (inside proportional window)
    {
        "STATE_BOOST: warm tank + flow=7.5 L/min (delta=3 degC)",
        { 57.0f, 7.5f, 60.0f, true, true },
        BoilerState::STATE_BOOST, 0xFF, 100  // pwmInternal proportional, pwmBoost=100
    },
    // 12 — STATE_BOOST: flow active, tank already hot → boost=100, internal=0
    {
        "STATE_BOOST: hot tank + flow=7.5 L/min (temp>=target)",
        { 62.0f, 7.5f, 60.0f, true, true },
        BoilerState::STATE_BOOST, 0, 100
    },
    // 13 — SAFETY beats everything: overtemp + flow + uiOn → must be SAFETY
    {
        "SAFETY beats BOOST: overtemp + flow active",
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
    // Determine pass/fail
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
    Serial.printf("  INPUT  currentTemp  : %5.2f degC\n",   s.in.currentTemp);
    Serial.printf("  INPUT  flowRateLPM  : %5.2f L/min\n",  s.in.flowRateLPM);
    Serial.printf("  INPUT  targetTemp   : %5.2f degC\n",   s.in.targetTemp);
    Serial.printf("  INPUT  uiStateOn    : %s\n",           s.in.uiStateOn    ? "ON"  : "OFF");
    Serial.printf("  INPUT  plcConnected : %s\n",           s.in.plcConnected ? "YES" : "NO");
    Serial.println("  ------------------------------------------------------------");
    Serial.printf("  OUTPUT pwmInternal  : %3u %%\n",  cmd.pwmInternal);
    Serial.printf("  OUTPUT pwmBoost     : %3u %%\n",  cmd.pwmBoost);
    Serial.printf("  OUTPUT state        : %-20s  %s\n",
                  cmd.stateLabel, pass ? "PASS" : "*** FAIL ***");

    if (!stateOk)
        Serial.printf("         EXPECTED state : %s\n",
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

            // Mirror current scenario to the UI so you can watch the screen
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
