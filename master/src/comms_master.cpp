#include <Arduino.h>
#include "boiler_protocol.h"
#include "config.h"
#include "ui_manager.h"

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

// Last received sensor values — used for the proportional PWM calculation
static float last_t_internal = 0.0f;
static float last_flow       = 0.0f;

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
            Serial.printf("[COMMS RX] Packet loss: expected seq %u got %u\n",
                          expected, pkt->sequence);
        }
    }
    last_rx_seq = pkt->sequence;

    // Decode fixed-point values back to floats
    float t_internal  = pkt->tempInternal  / 10.0f;
    float t_boiler    = pkt->tempBoilerOut / 10.0f;
    float t_boost     = pkt->tempBoostOut  / 10.0f;
    float flow        = pkt->flowRate      / 10.0f;
    // float power    = (float)pkt->powerWatts;  // available for future stats screen

    // Cache for PWM calculation in sendCommand()
    last_t_internal = t_internal;
    last_flow       = flow;

    // Update the LVGL UI (function is LVGL-lock safe)
    UI_UpdateSensorData(t_internal, t_boost, flow);

    Serial.printf("[COMMS RX] STATUS seq=%u t1=%.1f t2=%.1f t3=%.1f "
                  "flow=%.1f pwr=%uW status=0x%02X\n",
                  pkt->sequence,
                  t_internal, t_boiler, t_boost,
                  flow, (unsigned)pkt->powerWatts, pkt->statusByte);
}

// ---------------------------------------------------------------------------
//  sendCommand
//  Builds a BoilerCmdPacket_t from the current UI state and transmits it.
//  Called once per second from TaskMasterComms.
//
//  PWM strategy (master is the brain):
//    Internal heater: proportional — 100% when delta >= 5°C, linear below that
//    Boost heater:    binary       — full power only when water is flowing
// ---------------------------------------------------------------------------
static void sendCommand() {
    BoilerCmdPacket_t pkt;
    pkt.startByte  = PROTO_START;
    pkt.length     = CMD_PAYLOAD_LEN;
    pkt.packetType = PROTO_TYPE_CMD;
    pkt.sequence   = tx_seq++;

    if (!boiler_state) {
        // User pressed OFF — cut both heaters
        pkt.pwmInternal = 0u;
        pkt.pwmBoost    = 0u;
        pkt.cmdFlags    = 0u;
    } else {
        // --- Internal heater: proportional control ---
        float delta = (float)target_temperature - last_t_internal;
        uint8_t pwm_int = 0u;
        if (delta >= 5.0f) {
            pwm_int = 100u;                            // full power
        } else if (delta > 0.0f) {
            pwm_int = (uint8_t)(delta / 5.0f * 100.0f); // proportional
        }

        // --- Boost heater: on when water is flowing ---
        uint8_t pwm_bst = (last_flow > 0.5f) ? 100u : 0u;

        pkt.pwmInternal = pwm_int;
        pkt.pwmBoost    = pwm_bst;
        pkt.cmdFlags    = CMD_HEATER_ENABLE | CMD_BOOST_ENABLE;
    }

    pkt.crc8    = proto_cmd_crc(&pkt);
    pkt.endByte = PROTO_END;

    // Transmit with small inter-byte gap
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); i++) {
        Serial1.write(raw[i]);
        delay(2);
    }

    Serial.printf("[COMMS TX] CMD seq=%u pwmInt=%u pwmBst=%u flags=0x%02X\n",
                  pkt.sequence, pkt.pwmInternal, pkt.pwmBoost, pkt.cmdFlags);
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
                            Serial.printf("[COMMS RX] CRC error "
                                          "(calc 0x%02X recv 0x%02X)\n",
                                          calc_crc, recv_crc);
                        }

                    } else if (pkt_type == PROTO_TYPE_CMD) {
                        // Echo of our own CMD transmission — ignore
                        Serial.println("[COMMS RX] Own CMD echo ignored");
                    } else {
                        Serial.printf("[COMMS RX] Unknown type 0x%02X\n",
                                      pkt_type);
                    }
                } else {
                    Serial.printf("[COMMS RX] Bad end byte 0x%02X\n", b);
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
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
