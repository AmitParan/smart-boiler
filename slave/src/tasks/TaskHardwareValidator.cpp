// =============================================================================
// tasks/TaskHardwareValidator.cpp  (SLAVE)
// PURPOSE : Part 2 — HIL (Hardware-in-the-Loop) LED validation test.
//
// ENABLE  : #define HW_TEST_MODE 1  in slave/src/main.cpp
//
// WHAT IT DOES:
//   Runs a scripted sequence of hardware scenarios on the slave.
//   The slave cycles through each step and drives the SSR pins (with LEDs
//   attached instead of heaters) while printing a clear log to Serial.
//   You verify by watching the LEDs match the expected pattern exactly.
//
// WIRING:
//   GPIO4 (PIN_SSR_INT) → 330Ω → LED_INT  → GND   (internal heater indicator)
//   GPIO5 (PIN_SSR_EXT) → 330Ω → LED_BOOST→ GND   (boost heater indicator)
//   All safety hardware (LM393N comparator, watchdog caps) stay connected.
//
// STEP-BY-STEP PROCEDURE:
//
//  Step 1  NORMAL: Internal heater only
//          Master sends CMD: pwmInternal=100, pwmBoost=0, flag=HEATER_ENABLE
//          EXPECT: LED_INT ON, LED_BOOST OFF
//
//  Step 2  NORMAL: Boost heater only (shower)
//          Master sends CMD: pwmInternal=0, pwmBoost=100, flag=BOOST_ENABLE
//          EXPECT: LED_INT OFF, LED_BOOST ON
//
//  Step 3  SAFETY: Both heaters commanded simultaneously
//          Slave TaskSafety should REFUSE and cut both outputs immediately.
//          EXPECT: both LEDs OFF (slave rejects the command)
//
//  Step 4  SAFETY: Overtemperature (software cutoff 80°C)
//          Disconnect DS18B20 sensor wire — sensor reports 127°C (fault value).
//          EXPECT: both LEDs OFF within 50ms (TaskSafety fires)
//
//  Step 5  SAFETY: PLC watchdog timeout
//          Unplug the PLC modem USB from the MASTER.
//          After 3 seconds, slave sees no new CMD packets.
//          EXPECT: both LEDs OFF within 3s of disconnect
//
//  Step 6  SAFETY: Boost without flow (flow interlock)
//          Master commands pwmBoost=100 but flow sensor is 0.
//          EXPECT: LED_BOOST OFF (slave refuses boost with no flow)
//
//  Step 7  HARDWARE: LM393N analog comparator cutoff
//          Disconnect the NTC thermistor wire.
//          The comparator output goes LOW, cutting the BJT base drive.
//          EXPECT: both LEDs OFF IMMEDIATELY — even if slave is running normally.
//          This test is INDEPENDENT of software; MCU state does not matter.
//
//  Step 8  HARDWARE: Watchdog capacitor (AC-coupling)
//          Force GPIO4 HIGH with no PWM (simulate frozen firmware).
//          The capacitor blocks DC → SSR gate sees no signal.
//          EXPECT: LED_INT OFF even while GPIO4 is held HIGH.
//
// =============================================================================

#include "tasks/TaskHardwareValidator.h"

#include <Arduino.h>
#include "config.h"
#include "boiler_protocol.h"
#include "state/slave_state.h"
#include "task_config.h"

// Step duration — long enough to read the LEDs and serial output clearly
static constexpr uint32_t STEP_HOLD_MS  = 8000u;   // 8 s per step
static constexpr uint32_t STEP_PAUSE_MS = 2000u;   // 2 s blank between steps

// ---------------------------------------------------------------------------
//  Low-level helpers that bypass LEDC (for direct GPIO tests in steps 7-8)
// ---------------------------------------------------------------------------
namespace {

static constexpr uint32_t PWM_HZ   = 1000u;
static constexpr uint8_t  PWM_BITS = 8u;
static constexpr uint32_t PWM_ON   = 127u;  // ~50% duty
static constexpr uint32_t PWM_OFF  = 0u;

void setInternalLED(bool on) {
    ledcWrite(PIN_SSR_INT, on ? PWM_ON : PWM_OFF);
}

void setBoostLED(bool on) {
    ledcWrite(PIN_SSR_EXT, on ? PWM_ON : PWM_OFF);
}

void bothOff() {
    setInternalLED(false);
    setBoostLED(false);
}

void printStep(uint8_t step, uint8_t total, const char* title) {
    Serial.println(F("============================================================"));
    Serial.printf("[HW STEP %u/%u] %s\n", step, total, title);
    Serial.println(F("============================================================"));
}

void printExpect(const char* internal_led, const char* boost_led) {
    Serial.printf("  EXPECT: LED_INT = %-4s  |  LED_BOOST = %s\n",
                  internal_led, boost_led);
}

void printInstruction(const char* action) {
    Serial.printf("  ACTION: %s\n", action);
}

// Publish a CommandSnapshot so TaskPWM and TaskSafety react as in production.
void publishCmd(uint8_t pwmInt, uint8_t pwmBst, uint8_t flags) {
    CommandSnapshot cmd{};
    cmd.pwmInternal    = pwmInt;
    cmd.pwmBoost       = pwmBst;
    cmd.flags          = flags;
    cmd.receivedAtTick = xTaskGetTickCount();
    cmd.valid          = true;
    SlaveState_PublishCommand(cmd);
}

} // namespace

// ---------------------------------------------------------------------------
//  TaskHardwareValidator — FreeRTOS entry point
// ---------------------------------------------------------------------------
void TaskHardwareValidator(void* pvParameters) {
    (void)pvParameters;

    // Init PWM channels the same way pwm_task_internal/boost would
    ledcAttach(PIN_SSR_INT, PWM_HZ, PWM_BITS);
    ledcAttach(PIN_SSR_EXT, PWM_HZ, PWM_BITS);
    bothOff();

    vTaskDelay(pdMS_TO_TICKS(2000u));  // wait for Serial to settle

    Serial.println(F("\n============================================================"));
    Serial.println(F("  SLAVE HARDWARE VALIDATOR  (HW_TEST_MODE 1)"));
    Serial.println(F("  LEDs must be wired to GPIO4 (INT) and GPIO5 (BOOST)."));
    Serial.println(F("  Follow ACTION instructions for each step."));
    Serial.println(F("============================================================\n"));
    vTaskDelay(pdMS_TO_TICKS(3000u));

    static constexpr uint8_t TOTAL = 8u;
    uint32_t cycle = 0;

    for (;;) {
        cycle++;
        Serial.printf("\n>>> HW VALIDATION CYCLE %u\n\n", cycle);

        // ----------------------------------------------------------------
        // STEP 1 — Normal: internal heater only
        // ----------------------------------------------------------------
        printStep(1, TOTAL, "NORMAL — Internal heater ON, boost OFF");
        printInstruction("No action needed. Observe LEDs.");
        printExpect("ON", "OFF");
        publishCmd(100u, 0u, CMD_HEATER_ENABLE);
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS));

        // ----------------------------------------------------------------
        // STEP 2 — Normal: boost heater only (shower mode)
        // ----------------------------------------------------------------
        printStep(2, TOTAL, "NORMAL — Boost ON (shower), internal OFF");

        // Inject fake flow so TaskSafety doesn't block boost
        SensorSnapshot fakeSensors{};
        SlaveState_ReadSensors(fakeSensors);
        fakeSensors.flowLpm       = 8.0f;
        fakeSensors.tempsC[0]     = 38.0f;
        fakeSensors.updatedAtTick = xTaskGetTickCount();
        fakeSensors.valid         = true;
        SlaveState_UpdateSensors(fakeSensors);

        printInstruction("No action needed. Observe LEDs.");
        printExpect("OFF", "ON");
        publishCmd(0u, 100u, CMD_BOOST_ENABLE);
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();

        // Restore zero flow
        fakeSensors.flowLpm = 0.0f;
        SlaveState_UpdateSensors(fakeSensors);
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS));

        // ----------------------------------------------------------------
        // STEP 3 — Safety: both heaters commanded simultaneously
        // ----------------------------------------------------------------
        printStep(3, TOTAL, "SAFETY — Both heaters commanded (breaker rule)");
        printInstruction("No action needed. Slave TaskSafety should reject.");
        printExpect("OFF", "OFF");
        // CMD_HEATER_ENABLE | CMD_BOOST_ENABLE — slave must refuse
        publishCmd(100u, 100u, CMD_HEATER_ENABLE | CMD_BOOST_ENABLE);
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS));

        // ----------------------------------------------------------------
        // STEP 4 — Safety: overtemperature (software cutoff 80°C)
        // ----------------------------------------------------------------
        printStep(4, TOTAL, "SAFETY — Overtemperature software cutoff (80°C)");
        printInstruction("Disconnect ONE DS18B20 sensor wire NOW.");
        printInstruction("Sensor will report 127°C (error value) → TaskSafety cuts output.");
        printExpect("OFF", "OFF");
        // Start with internal heater ON so we can see TaskSafety cut it
        publishCmd(100u, 0u, CMD_HEATER_ENABLE);
        Serial.println(F("  (Heater commanded ON — disconnect sensor to see safety cut)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        Serial.println(F("  (Reconnect DS18B20 before next step)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS + 5000u)); // extra time to reconnect

        // ----------------------------------------------------------------
        // STEP 5 — Safety: PLC watchdog timeout (3 s)
        // ----------------------------------------------------------------
        printStep(5, TOTAL, "SAFETY — PLC watchdog (master goes silent)");
        printInstruction("Unplug PLC modem USB from MASTER now.");
        printInstruction("After 3 seconds of silence, both LEDs must turn off.");
        printExpect("OFF (within 3s of unplug)", "OFF");
        publishCmd(100u, 0u, CMD_HEATER_ENABLE);
        Serial.println(F("  (Heater started — unplug master PLC modem now)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        Serial.println(F("  (Reconnect PLC modem before next step)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS + 5000u));

        // ----------------------------------------------------------------
        // STEP 6 — Safety: boost without flow (flow interlock software)
        // ----------------------------------------------------------------
        printStep(6, TOTAL, "SAFETY — Boost commanded with no flow");
        printInstruction("Ensure flow sensor reads 0 (no water running).");
        printExpect("OFF", "OFF");
        publishCmd(0u, 100u, CMD_BOOST_ENABLE);
        Serial.println(F("  (Boost commanded — TaskSafety must block it, LED_BOOST stays OFF)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS));

        // ----------------------------------------------------------------
        // STEP 7 — Hardware: LM393N analog comparator cutoff
        //          This is INDEPENDENT of software. No MCU involvement.
        // ----------------------------------------------------------------
        printStep(7, TOTAL, "HARDWARE — LM393N analog comparator cutoff");
        printInstruction("Start with heater LED ON (confirming normal operation).");
        printExpect("ON (initially)", "OFF");
        publishCmd(100u, 0u, CMD_HEATER_ENABLE);
        Serial.println(F("  LED_INT should be ON now."));
        vTaskDelay(pdMS_TO_TICKS(3000u));

        Serial.println(F("------------------------------------------------------------"));
        Serial.println(F("  NOW: Disconnect the NTC thermistor wire from LM393N."));
        Serial.println(F("  Comparator output goes LOW → BJT base loses drive."));
        Serial.println(F("  LED_INT must turn OFF IMMEDIATELY — no software involved."));
        printExpect("OFF (instantly)", "OFF");
        Serial.println(F("------------------------------------------------------------"));
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
        bothOff();
        Serial.println(F("  (Reconnect NTC wire before next step)"));
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS + 3000u));

        // ----------------------------------------------------------------
        // STEP 8 — Hardware: Watchdog capacitor blocks DC (frozen firmware)
        // ----------------------------------------------------------------
        printStep(8, TOTAL, "HARDWARE — Watchdog capacitor (frozen firmware sim)");
        printInstruction("This step manually drives GPIO4 HIGH with no PWM.");
        printInstruction("The 22uF capacitor in the SSR gate path blocks DC.");
        printExpect("OFF (GPIO4 is HIGH but cap blocks DC)", "OFF");

        // Stop LEDC and drive GPIO directly HIGH (simulates stuck firmware)
        ledcDetach(PIN_SSR_INT);
        pinMode(PIN_SSR_INT, OUTPUT);
        digitalWrite(PIN_SSR_INT, HIGH);
        Serial.println(F("  GPIO4 driven HIGH (DC). If cap works: LED_INT stays OFF."));
        vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));

        // Restore LEDC
        digitalWrite(PIN_SSR_INT, LOW);
        ledcAttach(PIN_SSR_INT, PWM_HZ, PWM_BITS);
        ledcWrite(PIN_SSR_INT, PWM_OFF);
        vTaskDelay(pdMS_TO_TICKS(STEP_PAUSE_MS));

        // ----------------------------------------------------------------
        // Cycle summary
        // ----------------------------------------------------------------
        Serial.println(F("============================================================"));
        Serial.printf( "  HW CYCLE %u COMPLETE\n", cycle);
        Serial.println(F("  Review LED behavior above against EXPECT lines."));
        Serial.println(F("  All 8 steps must match — any mismatch = hardware fault."));
        Serial.println(F("============================================================\n"));
        vTaskDelay(pdMS_TO_TICKS(10000u));
    }
}
