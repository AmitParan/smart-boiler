#include "plc_test_sender.h"
#include "plc_comms.h"
#include "shared_data.h"
#include "boiler_protocol.h"
#include "config.h"
#include <Arduino.h>

// ===========================================================================
//  Slave PLC Test Sender
//
//  PURPOSE:
//    Drive the master's SystemManager through every possible state by sending
//    fabricated STATUS packets over the PLC.  No real sensors are needed.
//
//  HOW IT WORKS:
//    - Slave waits to receive a CMD from the master (normal request-response).
//    - Instead of filling STATUS from real sensors it injects scripted values.
//    - Each scenario runs for SCENARIO_HOLD_S seconds then advances.
//    - The master's serial monitor (COM6) shows PASS/FAIL from the test bench.
//    - The slave's serial monitor (COM8) shows which scenario is being sent.
//
//  ENABLE:  #define SLAVE_TEST_MODE 1  in slave/src/main.cpp
//
//  Scenario table (9 sensor-driven scenarios, matching master TEST_MODE #2,3,5-11):
//  Master scenarios #1 (PLC lost) and #4 (STATE_OFF) CANNOT be driven here —
//  they are master-side states, not sensor values. Test those with TEST_MODE only.
//
//  Slave#  Master#  tempInternal  flow     Expected master state
//  ------  -------  ------------  -------  ---------------------
//    1       2      86.0 °C       0.0      SAFETY_OVERRIDE  (overtemp)
//    2       3      85.0 °C       0.0      SAFETY_OVERRIDE  (boundary 85.0)
//    3       5      20.0 °C       0.0      STATE_HEATING_TANK  (cold start)
//    4       6      39.0 °C       0.0      STATE_HEATING_TANK  (near base)
//    5       7      40.0 °C       0.0      STATE_STANDBY  (at base temp)
//    6       8      55.0 °C       0.0      STATE_STANDBY  (above base)
//    7       9      20.0 °C       7.5      STATE_SHOWER_BOOST (cold tank)
//    8      10      40.0 °C       7.5      STATE_SHOWER_BOOST (warm tank)
//    9      11      86.0 °C       7.5      SAFETY_OVERRIDE  (overtemp + flow)
// ===========================================================================

static constexpr uint32_t SCENARIO_HOLD_S = 10u;   // seconds per scenario

struct SlaveTestScenario {
    const char* name;
    float       tempInternal;   // °C  — what the "tank sensor" reports
    float       tempBoilerOut;  // °C
    float       tempBoostOut;   // °C
    float       flow;           // L/min
    float       power;          // W  (simulated)
};

// Scenarios ordered to match master TEST_MODE #2,3,5,6,7,8,9,10,11
static const SlaveTestScenario kSlaveScenarios[] = {
    // name (Master#)                                tInt   tBoil  tBst   flow   pwr
    { "[M#02] SAFETY: Overtemp (86 degC)",          86.0f, 84.0f, 80.0f, 0.0f,  0.0f   },
    { "[M#03] SAFETY: Overtemp boundary (85 degC)", 85.0f, 83.0f, 79.0f, 0.0f,  0.0f   },
    { "[M#05] STATE_HEATING_TANK: cold (20 degC)",  20.0f, 18.0f, 18.0f, 0.0f,  2000.0f},
    { "[M#06] STATE_HEATING_TANK: near base (39)",  39.0f, 38.0f, 38.0f, 0.0f,  2000.0f},
    { "[M#07] STATE_STANDBY: at base (40 degC)",    40.0f, 39.0f, 39.0f, 0.0f,  0.0f   },
    { "[M#08] STATE_STANDBY: above base (55 degC)", 55.0f, 53.0f, 53.0f, 0.0f,  0.0f   },
    { "[M#09] STATE_SHOWER_BOOST: cold+flow",       20.0f, 18.0f, 45.0f, 7.5f,  3000.0f},
    { "[M#10] STATE_SHOWER_BOOST: warm+flow",       40.0f, 39.0f, 55.0f, 7.5f,  3000.0f},
    { "[M#11] SAFETY beats SHOWER_BOOST",           86.0f, 84.0f, 80.0f, 7.5f,  0.0f   },
};

static constexpr uint8_t kSlaveNumScenarios =
    (uint8_t)(sizeof(kSlaveScenarios) / sizeof(kSlaveScenarios[0]));

// ---------------------------------------------------------------------------
//  sendFakeStatus — builds and transmits a STATUS packet with scripted values
// ---------------------------------------------------------------------------
static uint8_t s_tx_seq = 0u;

static void sendFakeStatus(const SlaveTestScenario& s) {
    BoilerStatusPacket_t pkt;

    pkt.startByte     = PROTO_START;
    pkt.length        = STATUS_PAYLOAD_LEN;
    pkt.packetType    = PROTO_TYPE_STATUS;
    pkt.sequence      = s_tx_seq++;

    pkt.tempInternal  = (int16_t)(s.tempInternal  * 10.0f);
    pkt.tempBoilerOut = (int16_t)(s.tempBoilerOut * 10.0f);
    pkt.tempBoostOut  = (int16_t)(s.tempBoostOut  * 10.0f);
    pkt.flowRate      = (uint16_t)(s.flow * 10.0f);
    pkt.powerWatts    = (uint16_t)s.power;

    pkt.statusByte    = 0x00u;
    if (s.flow > 0.5f)   pkt.statusByte |= STATUS_FLOW_ACTIVE;

    pkt.crc8    = proto_status_crc(&pkt);
    pkt.endByte = PROTO_END;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); i++) {
        Serial1.write(raw[i]);
        delay(2);
    }

    Serial.printf("[TEST TX] seq=%u  tInt=%.1f  flow=%.1f  pwr=%.0fW\n",
                  pkt.sequence, s.tempInternal, s.flow, s.power);
}

// ---------------------------------------------------------------------------
//  TaskPLCTestSender — FreeRTOS entry point
// ---------------------------------------------------------------------------
void TaskPLCTestSender(void* pvParameters) {
    PLC_Init();
    Serial.println("[TEST] Slave PLC test sender started");
    Serial.printf("[TEST] %u scenarios x %u s = %u s per cycle\n",
                  kSlaveNumScenarios, SCENARIO_HOLD_S,
                  kSlaveNumScenarios * SCENARIO_HOLD_S);

    uint8_t  scenario_idx    = 0;
    uint32_t scenario_start  = millis();

    for (;;) {
        // Advance scenario after hold time expires
        if (millis() - scenario_start >= SCENARIO_HOLD_S * 1000UL) {
            scenario_idx = (scenario_idx + 1) % kSlaveNumScenarios;
            scenario_start = millis();

            const SlaveTestScenario& sc = kSlaveScenarios[scenario_idx];
            Serial.println("------------------------------------------------------------");
            Serial.printf("[TEST] Scenario %u/%u: %s\n",
                          scenario_idx + 1, kSlaveNumScenarios, sc.name);
            Serial.printf("       tInt=%.1f  flow=%.1f  pwr=%.0f\n",
                          sc.tempInternal, sc.flow, sc.power);
            Serial.println("------------------------------------------------------------");
        }

        // Wait for CMD from master (normal request-response handshake)
        bool cmd_received = PLC_ReceivePacket();
        if (cmd_received) {
            // Log what the master decided (pwm values decoded from CMD packet)
            extern volatile uint8_t cmd_pwm_internal;
            extern volatile uint8_t cmd_pwm_boost;
            Serial.printf("[TEST RX] CMD received → pwmInt=%u%%  pwmBst=%u%%\n",
                          cmd_pwm_internal, cmd_pwm_boost);

            // Reply with the current scripted STATUS after 200ms guard delay
            vTaskDelay(pdMS_TO_TICKS(200));
            sendFakeStatus(kSlaveScenarios[scenario_idx]);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
