#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "ui_manager.h"
#include "SystemManager.h"
#include "app_mode.h"

// Access UI state set by the user on the touch screen (defined in ui_manager.cpp)
extern bool boiler_state;
extern int  target_temperature;

// ---------------------------------------------------------------------------
//  Receive state machine (mirrors the slave implementation)
// ---------------------------------------------------------------------------
enum RxState : uint8_t {
    RX_WAIT_START = 0,
    RX_READ_LENGTH,
    RX_READ_PAYLOAD,
    RX_READ_CRC,
    RX_READ_END
};

static RxState   rx_state        = RX_WAIT_START;
static uint8_t   rx_buf[32]      = {};
static uint8_t   rx_buf_idx      = 0u;
static uint8_t   rx_expected     = 0u;
static uint32_t  rx_last_byte_ms = 0u;
static const uint32_t RX_TIMEOUT_MS = 200u;

static uint8_t   tx_seq          = 0u;
static uint8_t   last_rx_seq     = 0xFFu;

// Last received sensor values (cached for sendCommand log)
static float last_t_internal = 0.0f;
static float last_flow       = 0.0f;
static float last_power_w    = 0.0f;

static SystemManager s_manager;

// ---------------------------------------------------------------------------
//  processStatusPacket
//  Decodes a validated raw STATUS buffer and updates the UI.
// ---------------------------------------------------------------------------
static void processStatusPacket(const uint8_t* raw) {
    const BoilerStatusPacket_t* pkt =
        reinterpret_cast<const BoilerStatusPacket_t*>(raw);

    // Loss detection
    if (last_rx_seq != 0xFFu) {
        uint8_t expected = (uint8_t)(last_rx_seq + 1u);
        if (pkt->sequence != expected) {
            Serial.printf("[M<-S] DROP: expected seq=%u got seq=%u\n",
                          expected, pkt->sequence);
        }
    }
    last_rx_seq = pkt->sequence;

    // Decode fixed-point values back to floats
    float t_internal  = pkt->tempInternal  / 10.0f;
    float t_boiler    = pkt->tempBoilerOut / 10.0f;
    float t_boost     = pkt->tempBoostOut  / 10.0f;
    float flow        = pkt->flowRate      / 10.0f;
    float power_w     = (float)pkt->powerWatts;

    // Cache for SystemInputs in sendCommand()
    last_t_internal = t_internal;
    last_flow       = flow;
    last_power_w    = power_w;

    Serial.printf("[M<-S] seq=%3u | t1=%5.1f  t2=%5.1f  t3=%5.1f | flow=%4.1f  pwr=%4.0fW | sts=0x%02X\n",
                  pkt->sequence, t_internal, t_boiler, t_boost,
                  flow, power_w, pkt->statusByte);

    // In REALTIME mode the UI shows actual sensor data from the slave.
    // In DEMO mode the UI is updated by sendCommand() using demo values instead.
    if (appMode == APP_MODE_REALTIME) {
        UI_UpdateSensorData(t_internal, t_boost, flow, power_w);
    }
    UI_UpdatePLCStatus(true);
}

// ---------------------------------------------------------------------------
//  sendCommand
//  Builds a BoilerCmdPacket_t using SystemManager and transmits it.
//  Called once per second from TaskMasterComms.
//
//  APP_MODE_DEMO     — SystemInputs from demo vars (set by TaskAutomatedTestBench).
//                      CMD_DEMO_ACTIVE flag is set; demoTempX10/demoFlowX10 are packed
//                      so the slave can mirror them as mock sensor values.
//                      If demo_stop_comms (scenario 6), processes SystemManager for UI
//                      but suppresses TX so the slave detects a PLC timeout.
//  APP_MODE_REALTIME — inputs from last STATUS packet; no demo fields in CMD.
// ---------------------------------------------------------------------------
static void sendCommand() {
    // Build SystemInputs based on mode
    SystemInputs inputs;
    inputs.targetShowerTemp = (float)target_temperature;

    if (appMode == APP_MODE_DEMO) {
        inputs.currentTemp  = demo_temp;
        inputs.flowRateLPM  = demo_flow;
        inputs.uiStateOn    = demo_ui_on;
        inputs.plcConnected = !demo_stop_comms;  // false during scenario 6
        UI_UpdateSensorData(demo_temp, demo_temp, demo_flow, 0.0f);
    } else {
        inputs.currentTemp  = last_t_internal;
        inputs.flowRateLPM  = last_flow;
        inputs.uiStateOn    = boiler_state;
        inputs.plcConnected = true;
    }

    SystemCommand cmd = s_manager.process(inputs);
    UI_UpdateSystemMode(cmd.stateLabel);

    // Scenario 6: suppress TX so slave triggers PLC-loss watchdog
    if (demo_stop_comms) {
        tx_seq++;
        Serial.printf("[DEMO] TX suppressed | [%s]\n", cmd.stateLabel);
        return;
    }

    // Build packet
    BoilerCmdPacket_t pkt;
    pkt.startByte   = PROTO_START;
    pkt.length      = CMD_PAYLOAD_LEN;
    pkt.packetType  = PROTO_TYPE_CMD;
    pkt.sequence    = tx_seq++;
    pkt.pwmInternal = cmd.pwmInternal;
    pkt.pwmBoost    = cmd.pwmBoost;
    pkt.cmdFlags    = (cmd.pwmInternal > 0) ? CMD_HEATER_ENABLE : 0u;
    pkt.cmdFlags   |= (cmd.pwmBoost    > 0) ? CMD_BOOST_ENABLE  : 0u;

    if (appMode == APP_MODE_DEMO) {
        pkt.cmdFlags    |= CMD_DEMO_ACTIVE;
        if (demo_fault_sim) pkt.cmdFlags |= CMD_DEMO_FAULT_SIM;
        pkt.demoTempX10  = (int16_t)(demo_temp * 10.0f);
        pkt.demoFlowX10  = (uint16_t)(demo_flow * 10.0f);
    } else {
        pkt.demoTempX10 = 0;
        pkt.demoFlowX10 = 0;
    }

    pkt.crc8    = proto_cmd_crc(&pkt);
    pkt.endByte = PROTO_END;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); i++) {
        Serial1.write(raw[i]);
        delay(2);
    }

    Serial.printf("[M->S] seq=%3u | pwmInt=%3u%%  pwmBst=%3u%% | flags=0x%02X | [%s]\n",
                  pkt.sequence, pkt.pwmInternal, pkt.pwmBoost, pkt.cmdFlags, cmd.stateLabel);
}

// ---------------------------------------------------------------------------
//  receivePacket
//  Non-blocking receive. Call every 10 ms.
// ---------------------------------------------------------------------------
static bool receivePacket() {
    // Reset on timeout
    if (rx_state != RX_WAIT_START &&
        (millis() - rx_last_byte_ms) > RX_TIMEOUT_MS) {
        rx_state   = RX_WAIT_START;
        rx_buf_idx = 0u;
    }

    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();
        rx_last_byte_ms = millis();

        switch (rx_state) {

            case RX_WAIT_START:
                if (b == PROTO_START) {
                    rx_buf[0]  = b;
                    rx_buf_idx = 1u;
                    rx_state   = RX_READ_LENGTH;
                }
                break;

            case RX_READ_LENGTH:
                if (b == 0u || b > 20u) {
                    rx_state = RX_WAIT_START; rx_buf_idx = 0u;
                } else {
                    rx_buf[1]   = b;
                    rx_buf_idx  = 2u;
                    rx_expected = b;
                    rx_state    = RX_READ_PAYLOAD;
                }
                break;

            case RX_READ_PAYLOAD:
                rx_buf[rx_buf_idx++] = b;
                if (--rx_expected == 0u) rx_state = RX_READ_CRC;
                break;

            case RX_READ_CRC:
                rx_buf[rx_buf_idx++] = b;
                rx_state = RX_READ_END;
                break;

            case RX_READ_END:
                if (b == PROTO_END) {
                    rx_buf[rx_buf_idx++] = b;

                    uint8_t pkt_type = rx_buf[2];
                    uint8_t pkt_len  = rx_buf[1];

                    if (pkt_type == PROTO_TYPE_STATUS &&
                        pkt_len  == STATUS_PAYLOAD_LEN) {

                        uint8_t calc_crc = proto_crc8(rx_buf + 2u,
                                                      STATUS_PAYLOAD_LEN);
                        uint8_t recv_crc = rx_buf[2u + STATUS_PAYLOAD_LEN];

                        if (calc_crc == recv_crc) {
                            processStatusPacket(rx_buf);
                            rx_state = RX_WAIT_START; rx_buf_idx = 0u;
                            return true;
                        } else {
                            Serial.printf("[M<-S] CRC error "
                                          "(calc 0x%02X recv 0x%02X)\n",
                                          calc_crc, recv_crc);
                        }

                    } else if (pkt_type == PROTO_TYPE_CMD) {
                        // Echo of our own CMD transmission — ignore
                        Serial.println("[M<-S] own CMD echo ignored");
                    } else {
                        Serial.printf("[M<-S] unknown type 0x%02X\n",
                                      pkt_type);
                    }
                } else {
                    Serial.printf("[M<-S] bad end byte 0x%02X\n", b);
                }
                rx_state = RX_WAIT_START; rx_buf_idx = 0u;
                break;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  TaskMasterComms  — FreeRTOS task entry point
//  Runs on Core 1.
//  Receive loop runs every 10 ms.
//  CMD is sent once per second, offset 500 ms from the slave's STATUS send
//  so they are unlikely to transmit at the same time.
// ---------------------------------------------------------------------------
void TaskMasterComms(void* pvParameters) {
    Serial1.begin(9600, SERIAL_8N1, MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.printf("[COMMS] Master comms task started (RX=GPIO%d TX=GPIO%d)\n",
                  MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.printf("[COMMS] Mode: %s\n",
                  appMode == APP_MODE_DEMO ? "DEMO" : "REALTIME");

    unsigned long last_cmd_ms = millis();

    for (;;) {

        if (millis() - last_cmd_ms >= 1000UL) {
            last_cmd_ms = millis();

            // 1. Send CMD
            sendCommand();

            // 2. Wait up to 3000ms for STATUS response
            unsigned long wait_start = millis();
            bool got_status = false;
            while (millis() - wait_start < 3000UL) {
                if (receivePacket()) {
                    got_status = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!got_status) {
                Serial.println("[COMMS] No STATUS received within 3s");
                UI_UpdatePLCStatus(false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
