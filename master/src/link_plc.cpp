// ===========================================================================
//  link_plc.cpp — MASTER transport over the KQ-330 power-line modem
//
//  Compiled only when LINK_WIFI is NOT defined.
//
//  Mirror image of slave/src/link_plc.cpp. The KQ-330 is a half-duplex UART
//  bridge that trickles bytes across the mains, so frames must be reassembled
//  byte by byte and transmitted with a deliberate inter-byte gap. Both of
//  those concerns live here and nowhere else.
//
//  Timing constants below are hardware-derived and were established by
//  measurement — see README section 3 and TROUBLESHOOTING_PLC.md.
// ===========================================================================
#include "link.h"
#include "link_config.h"
#include "config.h"
#include "boiler_protocol.h"
#include <Arduino.h>

#ifndef LINK_WIFI

// ---------------------------------------------------------------------------
//  KQ-330 TX TIMING — DO NOT CHANGE
//  Verified by measurement: a 2 ms gap per byte is required for reliable
//  delivery. A bulk write with no gap made the modem drop the last 3-4 bytes.
//  Core 0 isolation keeps delay(2) close to a real 2 ms (no LVGL jitter).
// ---------------------------------------------------------------------------
static const uint8_t  KQ330_INTER_BYTE_DELAY_MS = 2u;

// Abandon a partially received frame if the next byte never arrives.
static const uint32_t RX_TIMEOUT_MS = 200u;

enum RxState : uint8_t {
    RX_WAIT_START = 0,
    RX_READ_LENGTH,
    RX_READ_PAYLOAD,
    RX_READ_CRC,
    RX_READ_END
};

static RxState  rx_state        = RX_WAIT_START;
static uint8_t  rx_buf[LINK_MAX_FRAME] = {};
static uint8_t  rx_buf_idx      = 0u;
static uint8_t  rx_expected     = 0u;
static uint32_t rx_last_byte_ms = 0u;

void link_begin() {
    Serial1.begin(9600, SERIAL_8N1, MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.printf("[LINK] PLC transport up (KQ-330, RX=GPIO%d TX=GPIO%d @ 9600 baud)\n",
                  MASTER_RX_PIN, MASTER_TX_PIN);
}

bool link_ready() {
    return true;   // the UART is always available once opened
}

void link_service() {
    // Nothing to maintain on a wired UART.
}

void link_send(const uint8_t* data, uint8_t len) {
    for (uint8_t i = 0u; i < len; i++) {
        Serial1.write(data[i]);
        delay(KQ330_INTER_BYTE_DELAY_MS);
    }
}

uint8_t link_poll(uint8_t* buf, uint8_t maxlen) {
    // Drop a stalled partial frame so a lost byte cannot wedge the receiver.
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
                // else: noise on the power line - discard silently
                break;

            case RX_READ_LENGTH:
                if (b == 0u || b > 20u) {
                    rx_state   = RX_WAIT_START;   // implausible length: noise
                    rx_buf_idx = 0u;
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

            case RX_READ_END: {
                uint8_t len = 0u;
                if (b == PROTO_END) {
                    rx_buf[rx_buf_idx++] = b;
                    len = rx_buf_idx;
                } else {
                    Serial.printf("[LINK] PLC bad end byte 0x%02X\n", b);
                }
                rx_state   = RX_WAIT_START;
                rx_buf_idx = 0u;

                if (len > 0u && len <= maxlen) {
                    memcpy(buf, rx_buf, len);
                    return len;      // one complete frame delivered upward
                }
                break;
            }
        }
    }
    return 0u;
}

const char* link_name() {
    return "PLC";
}

#endif // !LINK_WIFI
