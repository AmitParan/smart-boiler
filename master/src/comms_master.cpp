#include <Arduino.h>

#include "boiler_protocol.h"
#include "config.h"
#include "shared/master_state.h"
#include "task_config.h"
#include "tasks/TaskMasterComms.h"

// ===========================================================================
//  Master PLC communication task
//
//  Ownership:
//    - RX: parse STATUS packets from the slave and publish SensorSnapshot.
//    - TX: read the latest CommandSnapshot from TaskBrain and transmit CMD.
//    - No control decisions are made here.
// ===========================================================================

namespace {

enum RxState : uint8_t {
    RX_WAIT_START = 0,
    RX_READ_LENGTH,
    RX_READ_PAYLOAD,
    RX_READ_CRC,
    RX_READ_END
};

RxState rxState = RX_WAIT_START;
uint8_t rxBuf[32] = {};
uint8_t rxBufIdx = 0u;
uint8_t rxExpected = 0u;
uint32_t rxLastByteMs = 0u;

uint8_t txSeq = 0u;
uint8_t lastRxSeq = 0xFFu;

CommandSnapshot lastCommand = {
    .pwmInternal = 0u,
    .pwmBoost = 0u,
    .cmdFlags = CMD_EMERGENCY_STOP,
    .state = BoilerState::SAFETY_OVERRIDE,
    .decidedAtTick = 0,
    .plcConnected = false,
    .valid = true,
};

void resetRxParser() {
    rxState = RX_WAIT_START;
    rxBufIdx = 0u;
    rxExpected = 0u;
}

bool validPayloadLength(uint8_t length) {
    return length > 0u &&
           length <= STATUS_PAYLOAD_LEN &&
           (length + 4u) <= sizeof(rxBuf);
}

void publishStatusPacket(const uint8_t* raw) {
    const BoilerStatusPacket_t* pkt =
        reinterpret_cast<const BoilerStatusPacket_t*>(raw);

    if (lastRxSeq != 0xFFu) {
        const uint8_t expected = (uint8_t)(lastRxSeq + 1u);
        if (pkt->sequence != expected) {
            Serial.printf("[COMMS RX] Packet loss: expected seq %u got %u\n",
                          expected,
                          pkt->sequence);
        }
    }
    lastRxSeq = pkt->sequence;

    SensorSnapshot snapshot{};
    snapshot.tempInternalC = pkt->tempInternal / 10.0f;
    snapshot.tempBoilerOutC = pkt->tempBoilerOut / 10.0f;
    snapshot.tempBoostOutC = pkt->tempBoostOut / 10.0f;
    snapshot.flowLpm = pkt->flowRate / 10.0f;
    snapshot.powerW = (float)pkt->powerWatts;
    snapshot.statusByte = pkt->statusByte;
    snapshot.sequence = pkt->sequence;
    snapshot.receivedAtTick = xTaskGetTickCount();
    snapshot.valid = true;

    MasterState_PublishSensorSnapshot(snapshot);

    Serial.printf("[COMMS RX] STATUS seq=%u tInt=%.1f tBoost=%.1f "
                  "flow=%.1f power=%.0fW status=0x%02X\n",
                  snapshot.sequence,
                  snapshot.tempInternalC,
                  snapshot.tempBoostOutC,
                  snapshot.flowLpm,
                  snapshot.powerW,
                  snapshot.statusByte);
}

bool processCompleteFrame() {
    const uint8_t pktType = rxBuf[2];
    const uint8_t pktLen = rxBuf[1];

    if (pktType == PROTO_TYPE_STATUS && pktLen == STATUS_PAYLOAD_LEN) {
        const uint8_t calcCrc = proto_crc8(rxBuf + 2u, STATUS_PAYLOAD_LEN);
        const uint8_t recvCrc = rxBuf[2u + STATUS_PAYLOAD_LEN];

        if (calcCrc == recvCrc) {
            publishStatusPacket(rxBuf);
            return true;
        }

        Serial.printf("[COMMS RX] CRC error: calc 0x%02X recv 0x%02X\n",
                      calcCrc,
                      recvCrc);
        return false;
    }

    if (pktType == PROTO_TYPE_CMD) {
        Serial.println("[COMMS RX] Own CMD echo ignored");
        return false;
    }

    Serial.printf("[COMMS RX] Unknown frame type=0x%02X len=%u\n",
                  pktType,
                  pktLen);
    return false;
}

bool receivePacketsNonBlocking() {
    bool receivedStatus = false;

    if (rxState != RX_WAIT_START &&
        (millis() - rxLastByteMs) > MASTER_PLC_RX_TIMEOUT_MS) {
        Serial.printf("[COMMS RX] Parser timeout after %u byte(s)\n", rxBufIdx);
        resetRxParser();
    }

    while (Serial1.available() > 0) {
        const uint8_t b = (uint8_t)Serial1.read();
        rxLastByteMs = millis();

        switch (rxState) {
            case RX_WAIT_START:
                if (b == PROTO_START) {
                    rxBuf[0] = b;
                    rxBufIdx = 1u;
                    rxState = RX_READ_LENGTH;
                }
                break;

            case RX_READ_LENGTH:
                if (!validPayloadLength(b)) {
                    Serial.printf("[COMMS RX] Invalid length %u\n", b);
                    resetRxParser();
                } else {
                    rxBuf[1] = b;
                    rxBufIdx = 2u;
                    rxExpected = b;
                    rxState = RX_READ_PAYLOAD;
                }
                break;

            case RX_READ_PAYLOAD:
                if (rxBufIdx >= sizeof(rxBuf)) {
                    Serial.println("[COMMS RX] Buffer overflow prevented");
                    resetRxParser();
                    break;
                }

                rxBuf[rxBufIdx++] = b;
                if (--rxExpected == 0u) {
                    rxState = RX_READ_CRC;
                }
                break;

            case RX_READ_CRC:
                if (rxBufIdx >= sizeof(rxBuf)) {
                    Serial.println("[COMMS RX] Buffer overflow prevented");
                    resetRxParser();
                    break;
                }

                rxBuf[rxBufIdx++] = b;
                rxState = RX_READ_END;
                break;

            case RX_READ_END:
                if (b == PROTO_END) {
                    if (rxBufIdx < sizeof(rxBuf)) {
                        rxBuf[rxBufIdx++] = b;
                        receivedStatus = processCompleteFrame() || receivedStatus;
                    } else {
                        Serial.println("[COMMS RX] Buffer overflow prevented");
                    }
                } else {
                    Serial.printf("[COMMS RX] Bad end byte 0x%02X\n", b);
                }

                resetRxParser();
                break;
        }
    }

    return receivedStatus;
}

void refreshLatestCommand() {
    CommandSnapshot latest{};
    if (MasterState_ReadCommandSnapshot(latest) && latest.valid) {
        lastCommand = latest;
    }
}

void transmitCommand(const CommandSnapshot& command) {
    BoilerCmdPacket_t pkt{};
    pkt.startByte = PROTO_START;
    pkt.length = CMD_PAYLOAD_LEN;
    pkt.packetType = PROTO_TYPE_CMD;
    pkt.sequence = txSeq++;
    pkt.pwmInternal = command.pwmInternal;
    pkt.pwmBoost = command.pwmBoost;
    pkt.cmdFlags = command.cmdFlags;
    pkt.crc8 = proto_cmd_crc(&pkt);
    pkt.endByte = PROTO_END;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(pkt); ++i) {
        Serial1.write(raw[i]);
        vTaskDelay(pdMS_TO_TICKS(MASTER_PLC_INTER_BYTE_GAP_MS));
    }

    Serial.printf("[COMMS TX] CMD seq=%u pwmInt=%u pwmBoost=%u flags=0x%02X "
                  "state=%s plc=%s\n",
                  pkt.sequence,
                  pkt.pwmInternal,
                  pkt.pwmBoost,
                  pkt.cmdFlags,
                  SystemManager::labelFor(command.state),
                  command.plcConnected ? "connected" : "stale");
}

} // namespace

void TaskMasterComms(void* pvParameters) {
    (void)pvParameters;

    Serial1.begin(MASTER_PLC_BAUD, SERIAL_8N1, MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.printf("[COMMS] Task started (baud=%u RX=GPIO%d TX=GPIO%d)\n",
                  MASTER_PLC_BAUD,
                  MASTER_RX_PIN,
                  MASTER_TX_PIN);

    TickType_t lastTxTick = xTaskGetTickCount();

    for (;;) {
        refreshLatestCommand();
        receivePacketsNonBlocking();

        const TickType_t now = xTaskGetTickCount();
        if ((now - lastTxTick) >= pdMS_TO_TICKS(TASK_MASTER_COMMS_TX_PERIOD_MS)) {
            lastTxTick = now;
            transmitCommand(lastCommand);
        }

        vTaskDelay(pdMS_TO_TICKS(MASTER_COMMS_PERIOD_MS));
    }
}
