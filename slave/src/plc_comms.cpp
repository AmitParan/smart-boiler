#include "plc_comms.h"

#include <Arduino.h>
#include "config.h"
#include "shared/slave_state.h"
#include "shared_data.h"   // legacy SSR/fault status flags until final cleanup
#include "task_config.h"

// ---------------------------------------------------------------------------
//  Sequence counters
// ---------------------------------------------------------------------------
static uint8_t tx_seq = 0u;
static uint8_t last_rx_seq = 0xFFu;   // 0xFF = no packet received yet

// ---------------------------------------------------------------------------
//  Receive state machine
// ---------------------------------------------------------------------------
enum RxState : uint8_t {
    RX_WAIT_START = 0,
    RX_READ_LENGTH,
    RX_READ_PAYLOAD,
    RX_READ_CRC,
    RX_READ_END
};

static RxState rx_state = RX_WAIT_START;
static uint8_t rx_buf[32] = {};
static uint8_t rx_buf_idx = 0u;
static uint8_t rx_expected = 0u;
static uint32_t rx_last_byte_ms = 0u;
static uint32_t rx_channel_free_ms = 0u;

namespace {
void resetRxParser() {
    rx_state = RX_WAIT_START;
    rx_buf_idx = 0u;
    rx_expected = 0u;
}

bool validPayloadLength(uint8_t length) {
    return length > 0u && length <= CMD_PAYLOAD_LEN &&
           (length + 4u) <= sizeof(rx_buf);
}

void publishCommandPacket(const BoilerCmdPacket_t* cmd) {
    if (last_rx_seq != 0xFFu) {
        const uint8_t expected_seq = (uint8_t)(last_rx_seq + 1u);
        if (cmd->sequence != expected_seq) {
            Serial.printf("[PLC RX] Packet loss: expected seq %u got %u\n",
                          expected_seq,
                          cmd->sequence);
        }
    }
    last_rx_seq = cmd->sequence;

    CommandSnapshot snapshot{};
    snapshot.pwmInternal = cmd->pwmInternal;
    snapshot.pwmBoost = cmd->pwmBoost;
    snapshot.flags = cmd->cmdFlags;
    snapshot.receivedAtTick = xTaskGetTickCount();
    snapshot.valid = true;
    SlaveState_UpdateCommand(snapshot);

    Serial.printf("[PLC RX] CMD seq=%u pwmInt=%u pwmBst=%u flags=0x%02X\n",
                  cmd->sequence,
                  snapshot.pwmInternal,
                  snapshot.pwmBoost,
                  snapshot.flags);
}

uint16_t clampPowerToWire(float powerW) {
    if (powerW <= 0.0f) {
        return 0u;
    }
    if (powerW >= 65535.0f) {
        return 65535u;
    }
    return (uint16_t)powerW;
}
}

void PLC_Init() {
    Serial1.begin(PLC_BAUD, SERIAL_8N1, PLC_RX_PIN, PLC_TX_PIN);
    Serial.println("[PLC] Initialised on Serial1 (KQ-330, 9600 baud)");
}

void PLC_SendStatus() {
    SensorSnapshot sensors{};
    const bool hasSensors = SlaveState_ReadSensors(sensors) && sensors.valid;

    BoilerStatusPacket_t pkt{};
    pkt.startByte = PROTO_START;
    pkt.length = STATUS_PAYLOAD_LEN;
    pkt.packetType = PROTO_TYPE_STATUS;
    pkt.sequence = tx_seq++;

    if (hasSensors) {
        pkt.tempInternal = (int16_t)(sensors.tempsC[0] * 10.0f);
        pkt.tempBoilerOut = (int16_t)(sensors.tempsC[1] * 10.0f);
        pkt.tempBoostOut = (int16_t)(sensors.tempsC[2] * 10.0f);
        pkt.flowRate = (uint16_t)(sensors.flowLpm * 10.0f);
        pkt.powerWatts = clampPowerToWire(sensors.powerW);
    }

    uint8_t status = 0u;
    if (hasSensors && sensors.flowLpm >= 1.0f) status |= STATUS_FLOW_ACTIVE;
    if (internal_ssr_on)                       status |= STATUS_INTERNAL_ON;
    if (boost_ssr_on)                          status |= STATUS_BOOST_ON;
    if (system_fault)                          status |= STATUS_FAULT;
    pkt.statusByte = status;

    pkt.crc8 = proto_status_crc(&pkt);
    pkt.endByte = PROTO_END;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); ++i) {
        Serial1.write(raw[i]);
        vTaskDelay(pdMS_TO_TICKS(PLC_INTER_BYTE_GAP_MS));
    }

    if (hasSensors) {
        Serial.printf("[PLC TX] STATUS seq=%u t1=%.1f t2=%.1f t3=%.1f "
                      "flow=%.1f pwr=%uW status=0x%02X\n",
                      pkt.sequence,
                      sensors.tempsC[0],
                      sensors.tempsC[1],
                      sensors.tempsC[2],
                      sensors.flowLpm,
                      (unsigned)pkt.powerWatts,
                      pkt.statusByte);
    } else {
        Serial.printf("[PLC TX] STATUS seq=%u no valid sensors status=0x%02X\n",
                      pkt.sequence,
                      pkt.statusByte);
    }
}

bool PLC_IsReceiving() {
    if (rx_state != RX_WAIT_START) return true;
    if ((millis() - rx_channel_free_ms) < PLC_TX_QUIET_TIME_MS) return true;
    return false;
}

bool PLC_ReceivePacket() {
    if (rx_state != RX_WAIT_START &&
        (millis() - rx_last_byte_ms) > PLC_RX_TIMEOUT_MS) {
        Serial.printf("[PLC RX] Timeout; got %u byte(s): ", rx_buf_idx);
        for (uint8_t i = 0u; i < rx_buf_idx; ++i) {
            Serial.printf("0x%02X ", rx_buf[i]);
        }
        Serial.println();
        rx_channel_free_ms = millis();
        resetRxParser();
    }

    while (Serial1.available()) {
        const uint8_t b = (uint8_t)Serial1.read();
        rx_last_byte_ms = millis();
        rx_channel_free_ms = millis();

        switch (rx_state) {
            case RX_WAIT_START:
                if (b == PROTO_START) {
                    rx_buf[0] = b;
                    rx_buf_idx = 1u;
                    rx_state = RX_READ_LENGTH;
                }
                break;

            case RX_READ_LENGTH:
                if (!validPayloadLength(b)) {
                    resetRxParser();
                } else {
                    rx_buf[1] = b;
                    rx_buf_idx = 2u;
                    rx_expected = b;
                    rx_state = RX_READ_PAYLOAD;
                }
                break;

            case RX_READ_PAYLOAD:
                if (rx_buf_idx >= sizeof(rx_buf)) {
                    Serial.println("[PLC RX] Buffer overflow prevented");
                    resetRxParser();
                    break;
                }
                rx_buf[rx_buf_idx++] = b;
                if (--rx_expected == 0u) {
                    rx_state = RX_READ_CRC;
                }
                break;

            case RX_READ_CRC:
                if (rx_buf_idx >= sizeof(rx_buf)) {
                    Serial.println("[PLC RX] Buffer overflow prevented");
                    resetRxParser();
                    break;
                }
                rx_buf[rx_buf_idx++] = b;
                rx_state = RX_READ_END;
                break;

            case RX_READ_END:
                if (b == PROTO_END) {
                    if (rx_buf_idx < sizeof(rx_buf)) {
                        rx_buf[rx_buf_idx++] = b;

                        const uint8_t pkt_type = rx_buf[2];
                        const uint8_t pkt_len = rx_buf[1];

                        if (pkt_type == PROTO_TYPE_CMD &&
                            pkt_len == CMD_PAYLOAD_LEN) {
                            const uint8_t calc_crc =
                                proto_crc8(rx_buf + 2u, CMD_PAYLOAD_LEN);
                            const uint8_t recv_crc =
                                rx_buf[2u + CMD_PAYLOAD_LEN];

                            if (calc_crc == recv_crc) {
                                const BoilerCmdPacket_t* cmd =
                                    reinterpret_cast<const BoilerCmdPacket_t*>(rx_buf);
                                publishCommandPacket(cmd);
                                resetRxParser();
                                rx_channel_free_ms = millis();
                                return true;
                            }

                            Serial.printf("[PLC RX] CRC error "
                                          "(calc 0x%02X recv 0x%02X)\n",
                                          calc_crc,
                                          recv_crc);
                        } else if (pkt_type == PROTO_TYPE_STATUS) {
                            Serial.println("[PLC RX] Own STATUS echo ignored");
                        } else {
                            Serial.printf("[PLC RX] Unknown type 0x%02X len=%u\n",
                                          pkt_type,
                                          pkt_len);
                        }
                    } else {
                        Serial.println("[PLC RX] Buffer overflow prevented");
                    }
                } else {
                    Serial.printf("[PLC RX] Bad end byte 0x%02X\n", b);
                }

                resetRxParser();
                break;
        }
    }

    return false;
}
