// =============================================================================
// system/InteractiveTestBench.cpp
// PURPOSE : Part 1 — Interactive serial control panel for SystemManager logic.
//
// ENABLE  : #define TEST_MODE 2  in master/src/main.cpp
//
// WHAT IT DOES:
//   Lets you type commands into the Serial Monitor at runtime to inject any
//   sensor value combination and instantly see what the SystemManager decides:
//     - Which state (HEATING / BOOST / STANDBY / SAFETY / OFF)?
//     - What PWM is sent to the slave for internal heater and boost?
//     - What flags are set in the CMD packet?
//
//   This replaces the need to physically change sensors for software testing.
//   Every scenario in the spec can be triggered in < 5 seconds.
//
// MENU COMMANDS  (type command + Enter in Serial Monitor):
//   t <value>   — set tank temperature, e.g. "t 35.5"
//   f <value>   — set flow rate L/min,   e.g. "f 8.0"
//   s <value>   — set target shower temp, e.g. "s 55"
//   on          — set UI boilerOn = true  (simulate user pressing ON)
//   off         — set UI boilerOn = false (simulate user pressing OFF)
//   plc 1       — simulate PLC connected
//   plc 0       — simulate PLC disconnected (triggers SAFETY_OVERRIDE)
//   auto        — run all 11 built-in scenarios automatically (1 s each)
//   status      — print current inputs and last decision
//   reset       — restore defaults  (35°C, 0 flow, 60° target, ON, PLC ok)
//   help        — print this menu
// =============================================================================

#include "system/InteractiveTestBench.h"
#include "system/SystemManager.h"
#include "shared/master_state.h"
#include "boiler_protocol.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Auto-scenario table (same as SystemManagerTest but condensed to 1s each)
// ---------------------------------------------------------------------------
namespace {

struct QuickScenario {
    const char*  label;
    SystemInputs in;
    BoilerState  expectedState;
    uint8_t      expectedInt;   // 0xFF = "any non-zero"
    uint8_t      expectedBst;
};

static const QuickScenario kAuto[] = {
    { "SAFETY: PLC lost",                  { 35.f, 0.f, 60.f, true,  false }, BoilerState::SAFETY_OVERRIDE,   0,   0 },
    { "SAFETY: overtemp 86C",              { 86.f, 0.f, 60.f, true,  true  }, BoilerState::SAFETY_OVERRIDE,   0,   0 },
    { "SAFETY: boundary 85C",              { 85.f, 0.f, 60.f, true,  true  }, BoilerState::SAFETY_OVERRIDE,   0,   0 },
    { "OFF: user pressed OFF",             { 35.f, 0.f, 60.f, false, true  }, BoilerState::STATE_OFF,          0,   0 },
    { "HEATING: cold tank 20C",            { 20.f, 0.f, 60.f, true,  true  }, BoilerState::STATE_HEATING_TANK, 100, 0 },
    { "HEATING: near base 39C",            { 39.f, 0.f, 60.f, true,  true  }, BoilerState::STATE_HEATING_TANK, 100, 0 },
    { "STANDBY: at base 40C",              { 40.f, 0.f, 60.f, true,  true  }, BoilerState::STATE_STANDBY,      0,   0 },
    { "STANDBY: above base 55C",           { 55.f, 0.f, 60.f, true,  true  }, BoilerState::STATE_STANDBY,      0,   0 },
    { "BOOST: flow=7.5, cold tank 20C",    { 20.f, 7.5f,60.f, true,  true  }, BoilerState::STATE_SHOWER_BOOST, 0, 100 },
    { "BOOST: flow=7.5, warm tank 40C",    { 40.f, 7.5f,60.f, true,  true  }, BoilerState::STATE_SHOWER_BOOST, 0, 100 },
    { "SAFETY beats BOOST: 86C + flow",    { 86.f, 7.5f,60.f, true,  true  }, BoilerState::SAFETY_OVERRIDE,   0,   0 },
    // --- Edge cases ---
    { "BOOST boundary: flow=0.5 exact",    { 40.f, 0.5f,60.f, true,  true  }, BoilerState::STATE_STANDBY,      0,   0 }, // 0.5 is NOT > threshold
    { "BOOST boundary: flow=0.51",         { 40.f, 0.51f,60.f,true,  true  }, BoilerState::STATE_SHOWER_BOOST, 0, 100 },
    { "SAFETY boundary: 84.9C (no trip)",  { 84.9f,0.f, 60.f, true,  true  }, BoilerState::STATE_STANDBY,      0,   0 },
    { "OFF beats HEATING: cold + OFF",     { 20.f, 0.f, 60.f, false, true  }, BoilerState::STATE_OFF,          0,   0 },
    { "OFF beats BOOST: flow + OFF",       { 20.f, 7.5f,60.f, false, true  }, BoilerState::STATE_OFF,          0,   0 },
};

static constexpr uint8_t kNumAuto = sizeof(kAuto) / sizeof(kAuto[0]);

// ---------------------------------------------------------------------------
//  Live state (mutated by serial commands)
// ---------------------------------------------------------------------------
static SystemInputs  s_in  = { 35.0f, 0.0f, 60.0f, true, true };
static SystemManager s_mgr;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static void printLine() {
    Serial.println(F("------------------------------------------------------------"));
}

static void printBanner() {
    Serial.println(F("\n============================================================"));
    Serial.println(F("  SMART BOILER — Interactive Test Bench  (TEST_MODE 2)"));
    Serial.println(F("  Type 'help' for commands.  115200 baud."));
    Serial.println(F("============================================================\n"));
}

static void printHelp() {
    printLine();
    Serial.println(F("  COMMANDS:"));
    Serial.println(F("  t <val>   Set tank temp °C        e.g. t 35.5"));
    Serial.println(F("  f <val>   Set flow rate L/min     e.g. f 8.0"));
    Serial.println(F("  s <val>   Set target shower °C    e.g. s 55"));
    Serial.println(F("  on        Boiler ON  (simulate touchscreen press)"));
    Serial.println(F("  off       Boiler OFF (simulate touchscreen press)"));
    Serial.println(F("  plc 1     Simulate PLC connected"));
    Serial.println(F("  plc 0     Simulate PLC disconnected → SAFETY_OVERRIDE"));
    Serial.println(F("  auto      Run all built-in scenarios (1 s each)"));
    Serial.println(F("  status    Print current inputs + last decision"));
    Serial.println(F("  reset     Restore defaults (35°C, 0 flow, 60° target, ON, PLC ok)"));
    Serial.println(F("  help      Print this menu"));
    printLine();
}

static void printInputs() {
    Serial.printf("  INPUTS  tankTemp    : %.1f °C  (cutoff >= %.0f | base < %.0f)\n",
                  s_in.currentTemp, TEMP_CUTOFF_C, TARGET_TANK_TEMP);
    Serial.printf("  INPUTS  flowRate    : %.2f L/min  (threshold > %.1f)\n",
                  s_in.flowRateLPM, FLOW_THRESHOLD_LPM);
    Serial.printf("  INPUTS  targetShower: %.0f °C\n", s_in.targetShowerTemp);
    Serial.printf("  INPUTS  uiBoilerOn  : %s\n",   s_in.uiStateOn    ? "ON"  : "OFF");
    Serial.printf("  INPUTS  plcConnected: %s\n",   s_in.plcConnected ? "YES" : "NO");
}

static void printDecision(const SystemCommand& cmd, const char* expectedLabel = nullptr) {
    printLine();
    printInputs();
    printLine();
    Serial.printf("  DECISION  state       : %s\n", cmd.stateLabel);
    Serial.printf("  DECISION  pwmInternal : %3u %%  → internal heater %s\n",
                  cmd.pwmInternal, cmd.pwmInternal > 0 ? "ON" : "OFF");
    Serial.printf("  DECISION  pwmBoost    : %3u %%  → boost heater    %s\n",
                  cmd.pwmBoost,    cmd.pwmBoost > 0    ? "ON" : "OFF");

    // Build CMD flags description
    Serial.print(F("  DECISION  cmdFlags    : "));
    bool anyFlag = false;
    if (cmd.pwmInternal > 0)  { Serial.print(F("CMD_HEATER_ENABLE ")); anyFlag = true; }
    if (cmd.pwmBoost > 0)     { Serial.print(F("CMD_BOOST_ENABLE "));  anyFlag = true; }
    if (cmd.state == BoilerState::SAFETY_OVERRIDE) {
        Serial.print(F("CMD_EMERGENCY_STOP")); anyFlag = true;
    }
    if (!anyFlag) Serial.print(F("none"));
    Serial.println();

    if (expectedLabel) {
        bool pass = (strcmp(cmd.stateLabel, expectedLabel) == 0);
        Serial.printf("  RESULT    expected    : %s  → %s\n",
                      expectedLabel, pass ? "✓ PASS" : "*** FAIL ***");
    }
    printLine();
}

static void publishDecision(const SystemCommand& cmd) {
    // Push the decision into the real FreeRTOS queues so TaskComms
    // would transmit it to the slave (useful when running on real hardware
    // with the slave connected).
    CommandSnapshot snap{};
    snap.pwmInternal   = cmd.pwmInternal;
    snap.pwmBoost      = cmd.pwmBoost;
    snap.cmdFlags      = 0u;
    snap.cmdFlags     |= (cmd.pwmInternal > 0) ? CMD_HEATER_ENABLE  : 0u;
    snap.cmdFlags     |= (cmd.pwmBoost > 0)    ? CMD_BOOST_ENABLE   : 0u;
    if (cmd.state == BoilerState::SAFETY_OVERRIDE) snap.cmdFlags |= CMD_EMERGENCY_STOP;
    snap.state         = cmd.state;
    snap.decidedAtTick = xTaskGetTickCount();
    snap.plcConnected  = s_in.plcConnected;
    snap.valid         = true;
    MasterState_PublishCommandSnapshot(snap);
}

static void runAutoScenarios() {
    uint8_t pass = 0, fail = 0;

    Serial.printf("\n>>> AUTO RUN — %u scenarios\n\n", kNumAuto);

    for (uint8_t i = 0; i < kNumAuto; i++) {
        const QuickScenario& sc = kAuto[i];
        SystemManager        m;
        SystemCommand        cmd = m.process(sc.in);

        bool stateOk = (cmd.state == sc.expectedState);
        bool intOk   = (sc.expectedInt == 0xFF) ? (cmd.pwmInternal > 0)
                                                 : (cmd.pwmInternal == sc.expectedInt);
        bool bstOk   = (sc.expectedBst == 0xFF) ? (cmd.pwmBoost > 0)
                                                 : (cmd.pwmBoost == sc.expectedBst);
        bool ok      = stateOk && intOk && bstOk;

        if (ok) pass++; else fail++;

        Serial.printf("[%2u/%u] %-42s  pwmInt=%3u  pwmBst=%3u  → %s  %s\n",
                      i + 1, kNumAuto,
                      sc.label,
                      cmd.pwmInternal, cmd.pwmBoost,
                      cmd.stateLabel,
                      ok ? "PASS" : "*** FAIL ***");

        vTaskDelay(pdMS_TO_TICKS(1000u));
    }

    Serial.println();
    printLine();
    Serial.printf("  AUTO RESULT: %u PASS / %u FAIL / %u TOTAL\n", pass, fail, kNumAuto);
    if (fail == 0)
        Serial.println(F("  ALL PASS — SystemManager logic is CORRECT ✓"));
    else
        Serial.println(F("  FAILURES FOUND — review output above"));
    printLine();
    Serial.println();
}

static void processCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.equalsIgnoreCase("help")) {
        printHelp();
        return;
    }

    if (cmd.equalsIgnoreCase("on")) {
        s_in.uiStateOn = true;
        Serial.println(F("  >> boilerOn = true"));
    } else if (cmd.equalsIgnoreCase("off")) {
        s_in.uiStateOn = false;
        Serial.println(F("  >> boilerOn = false"));
    } else if (cmd.equalsIgnoreCase("reset")) {
        s_in = { 35.0f, 0.0f, 60.0f, true, true };
        Serial.println(F("  >> Reset to defaults"));
    } else if (cmd.equalsIgnoreCase("status")) {
        SystemCommand decision = s_mgr.process(s_in);
        printDecision(decision);
        publishDecision(decision);
        return;
    } else if (cmd.equalsIgnoreCase("auto")) {
        runAutoScenarios();
        return;
    } else if (cmd.startsWith("t ")) {
        s_in.currentTemp = cmd.substring(2).toFloat();
        Serial.printf("  >> tankTemp = %.1f °C\n", s_in.currentTemp);
    } else if (cmd.startsWith("f ")) {
        s_in.flowRateLPM = cmd.substring(2).toFloat();
        Serial.printf("  >> flowRate = %.2f L/min\n", s_in.flowRateLPM);
    } else if (cmd.startsWith("s ")) {
        s_in.targetShowerTemp = cmd.substring(2).toFloat();
        Serial.printf("  >> targetShower = %.0f °C\n", s_in.targetShowerTemp);
    } else if (cmd.startsWith("plc ")) {
        s_in.plcConnected = (cmd.substring(4).toInt() != 0);
        Serial.printf("  >> plcConnected = %s\n", s_in.plcConnected ? "YES" : "NO");
    } else {
        Serial.printf("  >> Unknown command: '%s'  (type 'help')\n", cmd.c_str());
        return;
    }

    // After every state change, re-evaluate and print the decision
    SystemCommand decision = s_mgr.process(s_in);
    Serial.println();
    printDecision(decision);
    publishDecision(decision);
}

} // namespace

// ---------------------------------------------------------------------------
//  TaskInteractiveTestBench — FreeRTOS entry point
// ---------------------------------------------------------------------------
void TaskInteractiveTestBench(void* pvParameters) {
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(1000u));  // wait for Serial to settle
    printBanner();
    printHelp();

    // Print initial state
    Serial.println(F("  Initial state:"));
    SystemCommand initial = s_mgr.process(s_in);
    printDecision(initial);
    publishDecision(initial);
    Serial.print(F("> "));

    String inputBuf;
    inputBuf.reserve(32);

    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (inputBuf.length() > 0) {
                    Serial.println();
                    processCommand(inputBuf);
                    inputBuf = "";
                    Serial.print(F("> "));
                }
            } else if (c >= 0x20) {  // printable chars only
                inputBuf += c;
                Serial.print(c);     // echo
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10u));
    }
}
