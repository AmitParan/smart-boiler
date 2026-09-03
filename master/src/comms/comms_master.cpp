#include <Arduino.h>
#include "boiler_protocol.h"
#include "link.h"
#include "link_config.h"
#include "config.h"
#include "ui_manager.h"
#include "SystemManager.h"
#include "app_mode.h"
#include "smart_preheat.h"
#include <time.h>

// Access UI state set by the user on the touch screen (defined in ui_manager.cpp)
extern bool boiler_state;
extern int  target_temperature;

// ---------------------------------------------------------------------------
//  Sequence counters.
//
//  Frame reassembly used to live here as a byte-level receive state machine.
//  It now belongs to the transport (link_plc.cpp), because it only ever
//  existed to cope with the KQ-330 trickling bytes across the mains. UDP
//  preserves message boundaries, so link_wifi.cpp needs none of it. Either
//  way link_poll() hands this file one complete frame at a time.
// ---------------------------------------------------------------------------
static uint8_t   tx_seq          = 0u;
static uint8_t   last_rx_seq     = 0xFFu;

// Last received sensor values (cached for sendCommand log)
static float last_t_internal = 0.0f;
static float last_flow       = 0.0f;
static float last_power_w    = 0.0f;

static SystemManager s_manager;

// Tracks whether the slave has reported a FAULT bit in its last STATUS packet.
// Used by sendCommand() to override the state label in demo mode.
static bool slave_has_fault = false;

// PLC connection watchdog (BUG-2 / SW-1): in REALTIME, plcConnected is derived
// from how recently a valid STATUS arrived (not hardcoded). Feeds SystemManager
// AND the smart-preheat brain, so a 5 s link loss forces SAFETY_OVERRIDE and
// stands the brain down. Before the first STATUS the link is treated as
// disconnected. Matches book section 10 (5s no-STATUS -> SAFETY_OVERRIDE).
static const uint32_t PLC_TIMEOUT_MS = 5000u;
static uint32_t last_status_ms = 0u;
static bool     status_ever    = false;

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

    // PLC watchdog: a valid STATUS just arrived — mark the link alive.
    last_status_ms = millis();
    status_ever    = true;

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

    // Smart brain: learn real shower times. A shower is the tap opening, i.e.
    // flow crossing the boost threshold from ~0. Only in REALTIME (real usage).
    if (appMode == MODE_REALTIME) {
        static float prev_flow_edge = 0.0f;
        if (prev_flow_edge < 1.0f && flow >= 1.0f) {
            SmartPreheat::recordShower((uint32_t)time(nullptr));
        }
        prev_flow_edge = flow;
    }

    // Always log STATUS receipt so the PLC link is visible in both modes.
    Serial.printf("[M<-S] seq=%3u | t1=%5.1f  t2=%5.1f  t3=%5.1f | flow=%4.1f  pwr=%4.0fW | sts=0x%02X\n",
                  pkt->sequence, t_internal, t_boiler, t_boost,
                  flow, power_w, pkt->statusByte);

    // Demo mode: event-driven slave fault banner (logged ONCE per fault event)
    if (appMode == MODE_DEMO) {
        static bool slave_fault_banner_shown = false;
        if ((pkt->statusByte & STATUS_FAULT) && !slave_fault_banner_shown) {
            Serial.println("[MASTER] \xe2\x9d\x8c RECEIVED STATUS_FAULT (0x08) FROM SLAVE! SYSTEM LOCKED.");
            slave_fault_banner_shown = true;
            slave_has_fault = true;
        } else if (!(pkt->statusByte & STATUS_FAULT)) {
            slave_fault_banner_shown = false;
            slave_has_fault = false;
        }
    }

    // Mode-mismatch detection: warn if the slave's actual mode disagrees with
    // what the master expects. Logged at most once per mismatch event.
    bool slave_is_realtime = (pkt->statusByte & STATUS_MODE_REALTIME) != 0;
    bool master_wants_realtime = (appMode == MODE_REALTIME);
    static bool mode_mismatch_logged = false;
    if (slave_is_realtime != master_wants_realtime) {
        if (!mode_mismatch_logged) {
            Serial.printf("[COMMS] WARNING: mode mismatch — master=%s slave=%s\n",
                          master_wants_realtime ? "REALTIME" : "DEMO",
                          slave_is_realtime     ? "REALTIME" : "DEMO");
            mode_mismatch_logged = true;
        }
    } else {
        mode_mismatch_logged = false;
    }

    // In REALTIME mode the UI shows actual sensor data from the slave.
    // In DEMO mode the UI is updated by sendCommand() using demo values instead.
    if (appMode == MODE_REALTIME) {
        UI_UpdateSensorData(t_internal, t_boost, flow, power_w);
    }
    UI_UpdatePLCStatus(true);
}

// ---------------------------------------------------------------------------
//  sendCommand
//  Builds a BoilerCmdPacket_t using SystemManager and transmits it.
//  Called once per second from TaskMasterComms.
//
//  MODE_DEMO     — SystemInputs from demo vars (set by TaskAutomatedTestBench).
//                      CMD_DEMO_ACTIVE flag is set; demoTempX10/demoFlowX10 are packed
//                      so the slave can mirror them as mock sensor values.
//                      If demo_stop_comms (scenario 6), processes SystemManager for UI
//                      but suppresses TX so the slave detects a PLC timeout.
//  MODE_REALTIME — inputs from last STATUS packet; no demo fields in CMD.
// ---------------------------------------------------------------------------
static void sendCommand() {
    // Build SystemInputs based on mode
    SystemInputs inputs;
    inputs.targetShowerTemp = (float)target_temperature;

    if (appMode == MODE_DEMO) {
        inputs.currentTemp  = demo_temp;
        inputs.flowRateLPM  = demo_flow;
        inputs.uiStateOn    = demo_ui_on;
        inputs.plcConnected = !demo_stop_comms;  // false during scenario 6
        // Use last_power_w from slave STATUS so mock 3kW load shows on Stats page
        UI_UpdateSensorData(demo_temp, demo_temp, demo_flow, last_power_w);
    } else {
        inputs.currentTemp  = last_t_internal;
        inputs.flowRateLPM  = last_flow;
        // PLC watchdog: the link is "connected" only if a STATUS arrived within
        // the timeout. Fail-safe before the first STATUS (status_ever == false).
        bool plc_ok = status_ever && (millis() - last_status_ms < PLC_TIMEOUT_MS);
        inputs.plcConnected = plc_ok;

        // Smart brain: run the preheat decision tree ~every 60 s. It may request
        // heating ahead of a scheduled/predicted shower. It never overrides a
        // manual ON and never touches safety — SystemManager still regulates 40 C,
        // and a lost PLC link (plc_ok == false) stands the brain down.
        static uint32_t last_brain_ms = 0u;
        if (millis() - last_brain_ms >= 60000UL) {
            last_brain_ms = millis();
            PreheatInputs pin;
            pin.unixNow       = (uint32_t)time(nullptr);
            pin.tankTempC     = last_t_internal;
            pin.mode          = (OpMode)UI_GetOpMode();
            pin.readyByCount  = UI_GetReadyByMinutes(pin.readyByMinutes, 4);
            pin.household     = UI_GetHouseholdSize();
            pin.manualOn      = boiler_state;
            pin.plcConnected  = plc_ok;
            SmartPreheat::update(pin);
        }

        // Boiler is ON if the user pressed ON, OR the brain is preheating.
        inputs.uiStateOn = boiler_state || SmartPreheat::wantsHeat();
    }

    SystemCommand cmd = s_manager.process(inputs);

    // Scenario 5: Predictive Solar Bypass — override SystemManager output to 0%
    // The boiler proactively stays off because it knows solar is heating the tank.
    // Without this override, SystemManager would fire the heater at temps < 40°C.
    if (demo_solar_active) {
        cmd.pwmInternal = 0u;
        cmd.pwmBoost    = 0u;
    }

    UI_UpdateSystemMode(cmd.stateLabel);

    // Scenario 6: suppress TX so slave triggers PLC-loss watchdog
    // Only applies in DEMO mode — never suppress TX in REALTIME
    if (appMode == MODE_DEMO && demo_stop_comms) {
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

    if (appMode == MODE_DEMO) {
        pkt.cmdFlags    |= CMD_DEMO_ACTIVE;
        if (demo_fault_sim)       pkt.cmdFlags |= CMD_DEMO_FAULT_SIM;
        if (demo_flow > 0.5f)     pkt.cmdFlags |= CMD_DEMO_FLOW;      // slave injects 6.5 L/min
        if (demo_temp >= 80.0f)   pkt.cmdFlags |= CMD_DEMO_OVERTEMP;  // slave injects 87°C (S7)
    }

    pkt.crc8    = proto_cmd_crc(&pkt);
    pkt.endByte = PROTO_END;

    // ---------------------------------------------------------------------------
    //  Hand the finished frame to the transport.
    //  The KQ-330 inter-byte timing that used to live here now belongs to
    //  link_plc.cpp, where it is hardware-specific and documented.
    // ---------------------------------------------------------------------------
    link_send(reinterpret_cast<const uint8_t*>(&pkt), (uint8_t)sizeof(pkt));

    // ---------------------------------------------------------------------------
    //  Serial logging
    // ---------------------------------------------------------------------------
    if (appMode == MODE_DEMO) {
        // Event-driven: SAFETY_OVERRIDE from PLC loss (log ONCE)
        static bool plc_loss_banner_shown = false;
        if (cmd.state == BoilerState::SAFETY_OVERRIDE && demo_stop_comms && !plc_loss_banner_shown) {
            Serial.println("[MASTER] \xe2\x9a\xa0\xef\xb8\x8f PLC TIMEOUT > 5s! ENTERING SAFETY_OVERRIDE");
            plc_loss_banner_shown = true;
        } else if (!demo_stop_comms) {
            plc_loss_banner_shown = false;
        }

        // Periodic telemetry line — fixed-width columns
        if (slave_has_fault) {
            Serial.printf("[MASTER] [seq=%03u] STATE: %-15s | STATUS: 0x08 | SYSTEM HARD LOCKED\n",
                          pkt.sequence, "FAULT");
        } else if (demo_stop_comms) {
            Serial.printf("[MASTER] [seq=%03u] STATE: %-15s | TEMP: %4.1f\xc2\xb0""C | PLC: LOST     | SEND -> INT:   0%% | BST:   0%%\n",
                          pkt.sequence, "SAFETY_OVERRIDE", demo_temp);
        } else if (demo_solar_active) {
            // Use actual SystemManager state label (must be STANDBY when temp >= 40C)
            const char* solar_state;
            switch (cmd.state) {
                case BoilerState::STATE_STANDBY: solar_state = "STANDBY"; break;
                default:                        solar_state = cmd.stateLabel; break;
            }
            Serial.printf("[MASTER] [seq=%03u] STATE: %-15s | SOLAR SWEEP: T=%4.1f\xc2\xb0""C | FLOW: %4.1fLPM | PWR: %4dW | SEND -> INT: %3d%% | BST: %3d%%\n",
                          pkt.sequence, solar_state, demo_temp, demo_flow,
                          (int)last_power_w, cmd.pwmInternal, cmd.pwmBoost);
        } else if (cmd.state == BoilerState::STATE_SHOWER_BOOST && cmd.pwmBoost == 0u) {
            Serial.printf("[MASTER] [seq=%03u] STATE: %-15s | TEMP: %4.1f\xc2\xb0""C | FLOW: %4.1fLPM | PWR: %4dW | SEND -> INT: %3d%% | BST: %3d%% (Warm Enough)\n",
                          pkt.sequence, "SHOWER_BOOST", demo_temp, demo_flow,
                          (int)last_power_w, cmd.pwmInternal, cmd.pwmBoost);
        } else {
            const char* s;
            switch (cmd.state) {
                case BoilerState::STATE_HEATING_TANK:  s = "HEATING_TANK";  break;
                case BoilerState::STATE_SHOWER_BOOST:  s = "SHOWER_BOOST";  break;
                case BoilerState::STATE_STANDBY:       s = "STANDBY";       break;
                case BoilerState::SAFETY_OVERRIDE:     s = "SAFETY_OVERRIDE"; break;
                default:                               s = "OFF";           break;
            }
            Serial.printf("[MASTER] [seq=%03u] STATE: %-15s | TEMP: %4.1f\xc2\xb0""C | FLOW: %4.1fLPM | PWR: %4dW | SEND -> INT: %3d%% | BST: %3d%%\n",
                          pkt.sequence, s, demo_temp, demo_flow,
                          (int)last_power_w, cmd.pwmInternal, cmd.pwmBoost);
        }
    } else {
        Serial.printf("[M->S] seq=%3u | pwmInt=%3u%%  pwmBst=%3u%% | flags=0x%02X | [%s]\n",
                      pkt.sequence, pkt.pwmInternal, pkt.pwmBoost, pkt.cmdFlags, cmd.stateLabel);
    }
}

// ---------------------------------------------------------------------------
//  receivePacket
//  Non-blocking receive. Call every 10 ms.
// ---------------------------------------------------------------------------
static bool receivePacket() {
    link_service();

    uint8_t frame[LINK_MAX_FRAME];
    uint8_t len = link_poll(frame, (uint8_t)sizeof(frame));
    if (len == 0u) return false;

    uint8_t pkt_type = frame[2];
    uint8_t pkt_len  = frame[1];

    // ---- Not a STATUS: ignore, but say why ----
    if (pkt_type != PROTO_TYPE_STATUS || pkt_len != STATUS_PAYLOAD_LEN) {
        if (pkt_type == PROTO_TYPE_CMD) {
            // Only possible on PLC, where the modem echoes our own transmission.
            Serial.println("[M<-S] own CMD echo ignored");
        } else {
            Serial.printf("[M<-S] unknown type 0x%02X len=%u\n", pkt_type, pkt_len);
        }
        return false;
    }

    // ---- CRC covers [index 2 .. index 2+STATUS_PAYLOAD_LEN-1] ----
    uint8_t calc_crc = proto_crc8(frame + 2u, STATUS_PAYLOAD_LEN);
    uint8_t recv_crc = frame[2u + STATUS_PAYLOAD_LEN];
    if (calc_crc != recv_crc) {
        Serial.printf("[M<-S] CRC error (calc 0x%02X recv 0x%02X)\n",
                      calc_crc, recv_crc);
        return false;
    }

    processStatusPacket(frame);
    return true;
}

// ---------------------------------------------------------------------------
//  TaskMasterComms  — FreeRTOS task entry point
//  Runs on Core 1.
//  Receive loop runs every 10 ms.
//  CMD is sent once per second, offset 500 ms from the slave's STATUS send
//  so they are unlikely to transmit at the same time.
// ---------------------------------------------------------------------------
void TaskMasterComms(void* pvParameters) {
    link_begin();
    Serial.printf("[COMMS] Master comms task started over %s transport\n",
                  link_name());
    Serial.printf("[COMMS] Mode: %s\n",
                  appMode == MODE_DEMO ? "DEMO" : "REALTIME");

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
                // In demo mode keep UI showing Connected — loss is intentional (scenario 6)
                if (appMode != MODE_DEMO) {
                    UI_UpdatePLCStatus(false);
                }
            } else {
                // STATUS received: reset the 1s CMD timer so the next CMD waits a
                // full second. Without this, the next CMD fires ~10ms after STATUS,
                // which is too fast for the slave's KQ-330 carrier to settle, causing
                // the slave to miss the CMD (slave still in TX-settling mode).
                last_cmd_ms = millis();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

