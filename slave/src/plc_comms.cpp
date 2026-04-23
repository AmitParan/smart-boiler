#include "plc_comms.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Sequence counters
// ---------------------------------------------------------------------------
static uint8_t tx_seq      = 0u;
static uint8_t last_rx_seq = 0xFFu;   // 0xFF = "no packet received yet"

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

static RxState   rx_state        = RX_WAIT_START;
static uint8_t   rx_buf[32]      = {};
static uint8_t   rx_buf_idx      = 0u;
static uint8_t   rx_expected     = 0u;
static uint32_t  rx_last_byte_ms = 0u;
static const uint32_t RX_TIMEOUT_MS = 200u;

// ---------------------------------------------------------------------------
//  PLC_Init
// ---------------------------------------------------------------------------
void PLC_Init() {
    Serial1.begin(PLC_BAUD, SERIAL_8N1, PLC_RX_PIN, PLC_TX_PIN);
    Serial.println("[PLC] Initialised on Serial1 (KQ-330, 9600 baud)");
}

// ---------------------------------------------------------------------------
//  PLC_SendStatus
//  Builds a BoilerStatusPacket_t from shared_data and transmits it.
//  Called once per second from TaskPLC.
// ---------------------------------------------------------------------------
void PLC_SendStatus() {
    BoilerStatusPacket_t pkt;

    pkt.startByte     = PROTO_START;
    pkt.length        = STATUS_PAYLOAD_LEN;
    pkt.packetType    = PROTO_TYPE_STATUS;
    pkt.sequence      = tx_seq++;

    // Temperatures encoded as int16 × 10  (e.g. 65.2°C → 652)
    pkt.tempInternal  = (int16_t)(temps[0] * 10.0f);
    pkt.tempBoilerOut = (int16_t)(temps[1] * 10.0f);
    pkt.tempBoostOut  = (int16_t)(temps[2] * 10.0f);

    // Flow encoded as uint16 × 10  (e.g. 7.5 L/min → 75)
    pkt.flowRate      = (uint16_t)(current_flow * 10.0f);

    // Power (integer watts)
    pkt.powerWatts    = (uint16_t)power_watts;

    // Status bit-flags
    uint8_t status = 0u;
    if (current_flow   >= 1.0f) status |= STATUS_FLOW_ACTIVE;
    if (internal_ssr_on)        status |= STATUS_INTERNAL_ON;
    if (boost_ssr_on)           status |= STATUS_BOOST_ON;
    if (system_fault)           status |= STATUS_FAULT;
    pkt.statusByte = status;

    // CRC covers [packetType .. statusByte]
    pkt.crc8    = proto_status_crc(&pkt);
    pkt.endByte = PROTO_END;

    // Transmit byte-by-byte with a small inter-byte gap for KQ-330 stability.
    // 2 ms × 17 bytes = 34 ms total — well within the 1-second budget.
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); i++) {
        Serial1.write(raw[i]);
        delay(2);
    }

    Serial.printf("[PLC TX] STATUS seq=%u t1=%.1f t2=%.1f t3=%.1f "
                  "flow=%.1f pwr=%uW status=0x%02X\n",
                  pkt.sequence,
                  temps[0], temps[1], temps[2],
                  current_flow, (unsigned)pkt.powerWatts, pkt.statusByte);
}

// ---------------------------------------------------------------------------
//  PLC_ReceivePacket
//  Non-blocking receive. Call every 10 ms.
//  Returns true if a valid BoilerCmdPacket_t was received and shared_data
//  was updated with the new PWM values and flags.
// ---------------------------------------------------------------------------
bool PLC_ReceivePacket() {
    // Reset state machine on byte-gap timeout
    if (rx_state != RX_WAIT_START &&
        (millis() - rx_last_byte_ms) > RX_TIMEOUT_MS) {
        Serial.println("[PLC RX] Timeout — resetting state machine");
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
                // else: noise on the power line — silently discard
                break;

            case RX_READ_LENGTH:
                if (b == 0u || b > 20u) {
                    // Sanity check fails — noise or own echo start byte
                    rx_state   = RX_WAIT_START;
                    rx_buf_idx = 0u;
                } else {
                    rx_buf[1]   = b;
                    rx_buf_idx  = 2u;
                    rx_expected = b;   // read this many payload bytes
                    rx_state    = RX_READ_PAYLOAD;
                }
                break;

            case RX_READ_PAYLOAD:
                rx_buf[rx_buf_idx++] = b;
                if (--rx_expected == 0u) {
                    rx_state = RX_READ_CRC;
                }
                break;

            case RX_READ_CRC:
                rx_buf[rx_buf_idx++] = b;   // store CRC byte
                rx_state = RX_READ_END;
                break;

            case RX_READ_END:
                if (b == PROTO_END) {
                    rx_buf[rx_buf_idx++] = b;

                    uint8_t pkt_type = rx_buf[2];
                    uint8_t pkt_len  = rx_buf[1];

                    // ---- Process CMD packet (master → slave) ----
                    if (pkt_type == PROTO_TYPE_CMD &&
                        pkt_len  == CMD_PAYLOAD_LEN) {

                        // CRC covers [index 2 .. index 2+CMD_PAYLOAD_LEN-1]
                        uint8_t calc_crc = proto_crc8(rx_buf + 2u, CMD_PAYLOAD_LEN);
                        uint8_t recv_crc = rx_buf[2u + CMD_PAYLOAD_LEN]; // index 7

                        if (calc_crc == recv_crc) {
                            const BoilerCmdPacket_t* cmd =
                                reinterpret_cast<const BoilerCmdPacket_t*>(rx_buf);

                            // Sequence-based loss detection
                            if (last_rx_seq != 0xFFu) {
                                uint8_t expected_seq = (uint8_t)(last_rx_seq + 1u);
                                if (cmd->sequence != expected_seq) {
                                    Serial.printf("[PLC RX] Packet loss: "
                                                  "expected seq %u got %u\n",
                                                  expected_seq, cmd->sequence);
                                }
                            }
                            last_rx_seq = cmd->sequence;

                            // Emergency stop overrides everything
                            if (cmd->cmdFlags & CMD_EMERGENCY_STOP) {
                                cmd_pwm_internal = 0u;
                                cmd_pwm_boost    = 0u;
                                cmd_flags        = 0u;
                                system_fault     = true;
                                Serial.println("[PLC RX] *** EMERGENCY STOP ***");
                            } else {
                                cmd_pwm_internal = cmd->pwmInternal;
                                cmd_pwm_boost    = cmd->pwmBoost;
                                cmd_flags        = cmd->cmdFlags;
                            }

                            Serial.printf("[PLC RX] CMD seq=%u "
                                          "pwmInt=%u pwmBst=%u flags=0x%02X\n",
                                          cmd->sequence,
                                          cmd->pwmInternal,
                                          cmd->pwmBoost,
                                          cmd->cmdFlags);

                            rx_state   = RX_WAIT_START;
                            rx_buf_idx = 0u;
                            return true;

                        } else {
                            Serial.printf("[PLC RX] CRC error "
                                          "(calc 0x%02X recv 0x%02X) — discarded\n",
                                          calc_crc, recv_crc);
                        }

                    } else if (pkt_type == PROTO_TYPE_STATUS) {
                        // Echo of our own STATUS transmission — ignore
                        Serial.println("[PLC RX] Own STATUS echo ignored");
                    } else {
                        Serial.printf("[PLC RX] Unknown type 0x%02X len=%u\n",
                                      pkt_type, pkt_len);
                    }
                } else {
                    Serial.printf("[PLC RX] Bad end byte 0x%02X\n", b);
                }

                rx_state   = RX_WAIT_START;
                rx_buf_idx = 0u;
                break;
        }
    }

    return false;
}
