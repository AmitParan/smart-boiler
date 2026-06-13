#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "ui_manager.h"
#include "SystemManager.h"

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

    // Update the LVGL UI (function is LVGL-lock safe)
    UI_UpdateSensorData(t_internal, t_boost, flow, power_w);
    UI_UpdatePLCStatus(true);
}

// ---------------------------------------------------------------------------
//  Manual test inputs — feed SystemManager with controlled values via serial
//  Press Enter at any time to see current state and available commands.
// ---------------------------------------------------------------------------
static bool  s_manualMode = true;    // true=manual inputs, false=live sensors
static float s_manTemp    = 25.0f;   // tank temperature [°C]
static float s_manFlow    = 0.0f;    // flow rate [L/min]
static bool  s_manUiOn    = false;   // UI boiler button
static bool  s_manPlcOk   = true;    // PLC connection state

static void handleManualInputs() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();

    // Empty Enter — show current state snapshot
    if (line.length() == 0) {
        SystemInputs snap;
        snap.currentTemp      = s_manualMode ? s_manTemp        : last_t_internal;
        snap.flowRateLPM      = s_manualMode ? s_manFlow        : last_flow;
        snap.targetShowerTemp = (float)target_temperature;
        snap.uiStateOn        = s_manualMode ? s_manUiOn        : boiler_state;
        snap.plcConnected     = s_manualMode ? s_manPlcOk       : true;
        SystemCommand snap_cmd = s_manager.process(snap);
        Serial.println("══════════════════════════════════════════════════════");
        Serial.printf (" [TEST] Mode: %s\n", s_manualMode ? "MANUAL" : "LIVE");
        Serial.printf (" Inputs:  t=%.1f C  |  flow=%.1f L/min  |  ui=%s  |  plc=%s\n",
                       snap.currentTemp, snap.flowRateLPM,
                       snap.uiStateOn ? "ON" : "OFF",
                       snap.plcConnected ? "OK" : "LOST");
        Serial.printf (" Brain:   %-22s  ->  pwmInt=%3u%%  pwmBst=%3u%%\n",
                       snap_cmd.stateLabel, snap_cmd.pwmInternal, snap_cmd.pwmBoost);
        Serial.println(" Commands: t=XX.X | f=X.X | on | off | plc=0 | plc=1 | live | ?");
        Serial.println("══════════════════════════════════════════════════════");
        return;
    }
    if (line == "?") {
        Serial.println("[TEST] t=XX.X  — tank temperature (C)");
        Serial.println("[TEST] f=X.X   — flow rate (L/min)");
        Serial.println("[TEST] on/off  — UI boiler button");
        Serial.println("[TEST] plc=0   — simulate PLC lost");
        Serial.println("[TEST] plc=1   — restore PLC connection");
        Serial.println("[TEST] live    — switch to real sensor data");
        Serial.println("[TEST] Enter   — show current state");
        return;
    }
    if (line.equalsIgnoreCase("on")) {
        s_manUiOn = true;  s_manualMode = true;
        Serial.println("[TEST] ui=ON");
        return;
    }
    if (line.equalsIgnoreCase("off")) {
        s_manUiOn = false;  s_manualMode = true;
        Serial.println("[TEST] ui=OFF");
        return;
    }
    if (line == "plc=0") {
        s_manPlcOk = false;  s_manualMode = true;
        Serial.println("[TEST] plc=LOST");
        return;
    }
    if (line == "plc=1") {
        s_manPlcOk = true;  s_manualMode = true;
        Serial.println("[TEST] plc=OK");
        return;
    }
    if (line.equalsIgnoreCase("live")) {
        s_manualMode = false;
        Serial.println("[TEST] Switched to LIVE sensor mode");
        return;
    }
    if (line.startsWith("t=") || line.startsWith("T=")) {
        s_manTemp = line.substring(2).toFloat();
        s_manualMode = true;
        Serial.printf("[TEST] t=%.1f C\n", s_manTemp);
        return;
    }
    if (line.startsWith("f=") || line.startsWith("F=")) {
        s_manFlow = line.substring(2).toFloat();
        s_manualMode = true;
        Serial.printf("[TEST] flow=%.1f L/min\n", s_manFlow);
        return;
    }
    Serial.printf("[TEST] Unknown: '%s'  (? for help)\n", line.c_str());
}

// ---------------------------------------------------------------------------
//  sendCommand
//  Builds a BoilerCmdPacket_t using SystemManager and transmits it.
//  Called once per second from TaskMasterComms.
// ---------------------------------------------------------------------------
static SystemManager s_manager;

static void sendCommand() {
    BoilerCmdPacket_t pkt;
    pkt.startByte  = PROTO_START;
    pkt.length     = CMD_PAYLOAD_LEN;
    pkt.packetType = PROTO_TYPE_CMD;
    pkt.sequence   = tx_seq++;

    // Build inputs — manual mode overrides real sensor data
    SystemInputs inputs;
    if (s_manualMode) {
        inputs.currentTemp      = s_manTemp;
        inputs.flowRateLPM      = s_manFlow;
        inputs.targetShowerTemp = (float)target_temperature;
        inputs.uiStateOn        = s_manUiOn;
        inputs.plcConnected     = s_manPlcOk;
    } else {
        inputs.currentTemp      = last_t_internal;
        inputs.flowRateLPM      = last_flow;
        inputs.targetShowerTemp = (float)target_temperature;
        inputs.uiStateOn        = boiler_state;
        inputs.plcConnected     = true;
    }

    SystemCommand cmd = s_manager.process(inputs);

    pkt.pwmInternal = cmd.pwmInternal;
    pkt.pwmBoost    = cmd.pwmBoost;
    pkt.cmdFlags    = (cmd.pwmInternal > 0) ? CMD_HEATER_ENABLE  : 0u;
    pkt.cmdFlags   |= (cmd.pwmBoost    > 0) ? CMD_BOOST_ENABLE   : 0u;

    pkt.crc8    = proto_cmd_crc(&pkt);
    pkt.endByte = PROTO_END;

    // Transmit with small inter-byte gap
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

    unsigned long last_cmd_ms = millis();

    Serial.println("[TEST] Manual input mode ON. Press Enter to see state, ? for commands.");

    for (;;) {
        handleManualInputs();

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
