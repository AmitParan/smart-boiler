// ===========================================================================
//  plc_comms.cpp — SLAVE protocol layer
//
//  Builds and parses the binary frames defined in boiler_protocol.h.
//  It does NOT know or care how those bytes travel: link_send() / link_poll()
//  hide that, so the identical code runs over the KQ-330 power-line modem or
//  over WiFi/UDP depending on the -DLINK_WIFI build flag. See link.h.
// ===========================================================================
#include "plc_comms.h"
#include "link.h"
#include "link_config.h"
#include "config.h"
#include "shared_data.h"
#include "system_mode.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Sequence counters
// ---------------------------------------------------------------------------
static uint8_t tx_seq      = 0u;
static uint8_t last_rx_seq = 0xFFu;   // 0xFF = "no packet received yet"

// ---------------------------------------------------------------------------
//  PLC_Init — bring the transport up
// ---------------------------------------------------------------------------
void PLC_Init() {
    link_begin();
    Serial.printf("[COMMS] Protocol layer ready over %s transport\n", link_name());
}

// ---------------------------------------------------------------------------
//  PLC_SendStatus
//  Builds a BoilerStatusPacket_t from shared_data and hands it to the link.
//  Called once per second from TaskPLC.
// ---------------------------------------------------------------------------
void PLC_SendStatus() {
    BoilerStatusPacket_t pkt;

    pkt.startByte     = PROTO_START;
    pkt.length        = STATUS_PAYLOAD_LEN;
    pkt.packetType    = PROTO_TYPE_STATUS;
    pkt.sequence      = tx_seq++;

    // ---- Snapshot sensor data under their respective mutexes ----
    float local_temps[3] = {0.0f, 0.0f, 0.0f};
    float local_flow     = 0.0f;
    float local_power    = 0.0f;
    float local_current  = 0.0f;

    if (xSemaphoreTake(guard_temps, pdMS_TO_TICKS(10)) == pdTRUE) {
        local_temps[0] = temps[0];
        local_temps[1] = temps[1];
        local_temps[2] = temps[2];
        xSemaphoreGive(guard_temps);
    }
    if (xSemaphoreTake(guard_flow, pdMS_TO_TICKS(10)) == pdTRUE) {
        local_flow = current_flow;
        xSemaphoreGive(guard_flow);
    }
    if (xSemaphoreTake(guard_current, pdMS_TO_TICKS(10)) == pdTRUE) {
        local_power   = power_watts;
        local_current = current_rms;
        xSemaphoreGive(guard_current);
    }

    // Temperatures encoded as int16 x 10  (e.g. 65.2C -> 652)
    pkt.tempInternal  = (int16_t)(local_temps[0] * 10.0f);
    pkt.tempBoilerOut = (int16_t)(local_temps[1] * 10.0f);
    pkt.tempBoostOut  = (int16_t)(local_temps[2] * 10.0f);

    // Flow encoded as uint16 x 10  (e.g. 7.5 L/min -> 75)
    pkt.flowRate      = (uint16_t)(local_flow * 10.0f);

    // Power (integer watts)
    pkt.powerWatts    = (uint16_t)local_power;

    // Status bit-flags (volatile bools, single-byte reads - no mutex needed)
    uint8_t status = 0u;
    if (local_flow     >= 1.0f) status |= STATUS_FLOW_ACTIVE;
    if (internal_ssr_on)        status |= STATUS_INTERNAL_ON;
    if (boost_ssr_on)           status |= STATUS_BOOST_ON;
    if (system_fault)           status |= STATUS_FAULT;
    pkt.statusByte = status;

    // CRC covers [packetType .. statusByte]
    pkt.crc8    = proto_status_crc(&pkt);
    pkt.endByte = PROTO_END;

    link_send(reinterpret_cast<const uint8_t*>(&pkt), (uint8_t)sizeof(pkt));

    // Serial output
    if (currentMode == MODE_DEMO) {
        Serial.printf("[SLAVE]  [seq=%03u] SSR_INT: %-3s | SSR_BST: %-3s | TEMP: %4.1f\xc2\xb0""C | FLOW: %4.1fLPM | PWR: %4dW | CURR: %4.1fA\n",
                      pkt.sequence,
                      internal_ssr_on ? "ON " : "OFF",
                      boost_ssr_on    ? "ON " : "OFF",
                      local_temps[0], local_flow,
                      (int)local_power, local_current);
    } else {
        Serial.printf("[S->M] seq=%3u | t1=%5.1f  t2=%5.1f  t3=%5.1f | flow=%4.1f  pwr=%4.0fW | sts=0x%02X\n",
                      pkt.sequence,
                      local_temps[0], local_temps[1], local_temps[2],
                      local_flow, (float)pkt.powerWatts, pkt.statusByte);
    }
}

// ---------------------------------------------------------------------------
//  PLC_ReceivePacket
//  Non-blocking. Pulls one complete frame from the link, validates it, and
//  applies it to shared_data. Returns true when a valid CMD was applied.
// ---------------------------------------------------------------------------
bool PLC_ReceivePacket() {
    link_service();

    uint8_t frame[LINK_MAX_FRAME];
    uint8_t len = link_poll(frame, (uint8_t)sizeof(frame));
    if (len == 0u) return false;

    uint8_t pkt_type = frame[2];
    uint8_t pkt_len  = frame[1];

    // ---- Not a CMD: ignore, but say why ----
    if (pkt_type != PROTO_TYPE_CMD || pkt_len != CMD_PAYLOAD_LEN) {
        if (pkt_type == PROTO_TYPE_STATUS) {
            // Only possible on PLC, where the modem echoes our own transmission.
            Serial.println("[S<-M] own STATUS echo ignored");
        } else {
            Serial.printf("[S<-M] unknown type 0x%02X len=%u\n", pkt_type, pkt_len);
        }
        return false;
    }

    // ---- CRC covers [index 2 .. index 2+CMD_PAYLOAD_LEN-1] ----
    uint8_t calc_crc = proto_crc8(frame + 2u, CMD_PAYLOAD_LEN);
    uint8_t recv_crc = frame[2u + CMD_PAYLOAD_LEN];
    if (calc_crc != recv_crc) {
        Serial.printf("[S<-M] CRC error (calc 0x%02X recv 0x%02X)\n", calc_crc, recv_crc);
        return false;
    }

    const BoilerCmdPacket_t* cmd =
        reinterpret_cast<const BoilerCmdPacket_t*>(frame);

    // Sequence-based loss detection
    if (last_rx_seq != 0xFFu) {
        uint8_t expected_seq = (uint8_t)(last_rx_seq + 1u);
        if (cmd->sequence != expected_seq) {
            Serial.printf("[S<-M] DROP: expected seq=%u got seq=%u\n",
                          expected_seq, cmd->sequence);
        }
    }
    last_rx_seq = cmd->sequence;

    // Update comms watchdog
    last_cmd_received_ms = millis();
    cmd_ever_received    = true;

    // Emergency stop overrides everything
    if (cmd->cmdFlags & CMD_EMERGENCY_STOP) {
        if (xSemaphoreTake(guard_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            cmd_pwm_internal = 0u;
            cmd_pwm_boost    = 0u;
            cmd_flags        = 0u;
            xSemaphoreGive(guard_cmd);
        }
        system_fault = true;
        Serial.println("[COMMS RX] *** EMERGENCY STOP ***");
    } else {
        if (xSemaphoreTake(guard_cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            cmd_pwm_internal = cmd->pwmInternal;
            cmd_pwm_boost    = cmd->pwmBoost;
            cmd_flags        = cmd->cmdFlags;
            xSemaphoreGive(guard_cmd);
        }
    }

    // Handle demo mode fields (flag-based, no extra payload bytes)
    if (cmd->cmdFlags & CMD_DEMO_ACTIVE) {
        slave_demo_flow_active = (cmd->cmdFlags & CMD_DEMO_FLOW)      != 0;
        slave_demo_overtemp    = (cmd->cmdFlags & CMD_DEMO_OVERTEMP)  != 0;
        slave_demo_fault_sim   = (cmd->cmdFlags & CMD_DEMO_FAULT_SIM) != 0;
        if (currentMode != MODE_DEMO) {
            currentMode = MODE_DEMO;
            Serial.println("[MODE] -> DEMO (master activated)");
        }
    } else {
        slave_demo_flow_active = false;
        slave_demo_overtemp    = false;
        slave_demo_fault_sim   = false;
        if (currentMode == MODE_DEMO) {
            currentMode = MODE_REALTIME;
            Serial.println("[MODE] -> REALTIME (master deactivated demo)");
        }
    }

    if (currentMode != MODE_DEMO) {
        Serial.printf("[S<-M] seq=%3u | pwmInt=%3u%%  pwmBst=%3u%% | flags=0x%02X\n",
                      cmd->sequence,
                      cmd->pwmInternal,
                      cmd->pwmBoost,
                      cmd->cmdFlags);
    }

    return true;
}
