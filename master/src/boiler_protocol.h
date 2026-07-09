#ifndef BOILER_PROTOCOL_H
#define BOILER_PROTOCOL_H

#include <stdint.h>

// ===========================================================================
//  Smart Boiler — Binary PLC Protocol
//  Shared by Master (ESP32-S3) and Slave (ESP32-C6)
//
//  Physical medium : KQ-330 PLC modem over 220V power line
//  UART baud       : 9600 bps  (MCU ↔ modem)
//  Effective rate  : ~100 bps  (modem ↔ modem over power line)
//  Rule            : MAX 1 packet per second — never flood the modem buffer
//
//  Wire frame layout (little-endian multi-byte fields):
//  [ 0xAA | LEN | TYPE | SEQ | ...payload... | CRC8 | 0x55 ]
//    ^                   |<--- LEN bytes --->|
//    start                                         end
//
//  CRC-8/SMBUS (poly 0x07) covers bytes [TYPE .. last payload byte].
//  startByte, length, crc8, endByte are NOT included in the CRC.
// ===========================================================================

// ---------------------------------------------------------------------------
//  Frame constants
// ---------------------------------------------------------------------------
#define PROTO_START           0xAAu
#define PROTO_END             0x55u
#define PROTO_TYPE_STATUS     0x01u   // Slave  → Master
#define PROTO_TYPE_CMD        0x02u   // Master → Slave

// Payload lengths (TYPE + SEQ + data fields, excluding framing bytes)
#define STATUS_PAYLOAD_LEN    13u     // 1+1+2+2+2+2+2+1 = 13
#define CMD_PAYLOAD_LEN        9u     // 1+1+1+1+1+2+2   =  9  (includes demoTempX10 + demoFlowX10)

// ---------------------------------------------------------------------------
//  statusByte bit-flags  (BoilerStatusPacket_t::statusByte)
// ---------------------------------------------------------------------------
#define STATUS_FLOW_ACTIVE    (1u << 0)   // water is flowing (flow >= 1 L/min)
#define STATUS_INTERNAL_ON    (1u << 1)   // internal tank SSR is energised
#define STATUS_BOOST_ON       (1u << 2)   // boost inline SSR is energised
#define STATUS_FAULT          (1u << 3)   // safety fault is active

// ---------------------------------------------------------------------------
//  cmdFlags bit-flags  (BoilerCmdPacket_t::cmdFlags)
// ---------------------------------------------------------------------------
#define CMD_HEATER_ENABLE     (1u << 0)   // allow internal heater to run
#define CMD_BOOST_ENABLE      (1u << 1)   // allow boost heater to run
#define CMD_EMERGENCY_STOP    (1u << 7)   // cut both SSRs immediately
#define CMD_DEMO_ACTIVE       (1u << 3)   // master is in demo mode; slave mirrors injected sensor data
#define CMD_DEMO_FAULT_SIM    (1u << 4)   // slave must simulate stuck-SSR fault (scenario 8)

// ---------------------------------------------------------------------------
//  Packet structures
//  #pragma pack(1) eliminates compiler padding → sizeof == wire size
// ---------------------------------------------------------------------------
#pragma pack(1)

// BoilerStatusPacket_t — 17 bytes on the wire
// Direction : Slave → Master, once per second
// Encoding  : temperatures stored as int16 × 10 (0.1 °C resolution)
//             flow stored as uint16 × 10 (0.1 L/min resolution)
struct BoilerStatusPacket_t {
    uint8_t  startByte;       // PROTO_START         = 0xAA
    uint8_t  length;          // STATUS_PAYLOAD_LEN  = 13
    uint8_t  packetType;      // PROTO_TYPE_STATUS   = 0x01
    uint8_t  sequence;        // rolling 0‥255, receiver detects lost packets
    int16_t  tempInternal;    // tank temp     [°C × 10]  e.g. 652  = 65.2 °C
    int16_t  tempBoilerOut;   // boiler outlet [°C × 10]
    int16_t  tempBoostOut;    // boost outlet  [°C × 10]
    uint16_t flowRate;        // flow          [L/min × 10]  e.g. 75 = 7.5
    uint16_t powerWatts;      // instantaneous power [W integer]
    uint8_t  statusByte;      // STATUS_* flags
    uint8_t  crc8;            // CRC-8 over bytes [packetType .. statusByte]
    uint8_t  endByte;         // PROTO_END = 0x55
};

// BoilerCmdPacket_t — 13 bytes on the wire
// Direction : Master → Slave, once per second
// demoTempX10 / demoFlowX10 are valid only when CMD_DEMO_ACTIVE is set.
struct BoilerCmdPacket_t {
    uint8_t  startByte;       // PROTO_START       = 0xAA
    uint8_t  length;          // CMD_PAYLOAD_LEN   = 9
    uint8_t  packetType;      // PROTO_TYPE_CMD    = 0x02
    uint8_t  sequence;        // rolling 0⁆55
    uint8_t  pwmInternal;     // internal heater duty cycle [0⁆50100 %]
    uint8_t  pwmBoost;        // boost heater duty cycle    [0⁆50100 %]
    uint8_t  cmdFlags;        // CMD_* flags
    int16_t  demoTempX10;     // injected tank temp × 10 [°C] (demo mode only)
    uint16_t demoFlowX10;     // injected flow × 10 [L/min]  (demo mode only)
    uint8_t  crc8;            // CRC-8 over bytes [packetType .. demoFlowX10]
    uint8_t  endByte;         // PROTO_END = 0x55
};

#pragma pack()

// ---------------------------------------------------------------------------
//  CRC-8 / SMBUS  —  polynomial 0x07, init value 0x00
//  Inline so both master and slave compile it without a separate .cpp file.
// ---------------------------------------------------------------------------
static inline uint8_t proto_crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0x00u;
    for (uint8_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0u; b < 8u; b++) {
            crc = (crc & 0x80u)
                ? (uint8_t)((crc << 1u) ^ 0x07u)
                : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

// Convenience helpers — compute CRC of a fully-populated packet.
// CRC covers STATUS_PAYLOAD_LEN bytes starting at index 2 (packetType).
static inline uint8_t proto_status_crc(const BoilerStatusPacket_t* p) {
    return proto_crc8(reinterpret_cast<const uint8_t*>(p) + 2u,
                      STATUS_PAYLOAD_LEN);
}

static inline uint8_t proto_cmd_crc(const BoilerCmdPacket_t* p) {
    return proto_crc8(reinterpret_cast<const uint8_t*>(p) + 2u,
                      CMD_PAYLOAD_LEN);
}

#endif // BOILER_PROTOCOL_H
