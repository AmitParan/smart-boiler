# Smart Boiler — PLC Communication Protocol

**Version:** 1.0  
**Last updated:** May 9, 2026  
**Status:** Production — verified end-to-end ✅

---

## 1. Physical Layer

### Hardware
The two ESP32 units do not talk directly to each other over wire. Instead, they communicate through **KQ-330 PLC modems** that carry the signal over the existing 220V power line in the building.

```
[Master ESP32-S3] ──UART──► [KQ-330 Modem] ──220V power line──► [KQ-330 Modem] ──UART──► [Slave ESP32-C6]
```

### UART Settings (MCU ↔ Modem)
| Parameter | Value |
|-----------|-------|
| Baud rate | 9600 bps |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Mode | Half-duplex (one side transmits at a time) |

### Effective Throughput
The KQ-330 modem over the power line achieves approximately **100 bps** effective throughput — much slower than the 9600 bps UART. This means packets must be sent slowly and infrequently to avoid overflowing the modem buffer.

**Rule: Maximum 1 packet per second. Never send bursts.**

### Master Pin Connections (ESP32-S3)
| Signal | GPIO |
|--------|------|
| TX (master → modem) | GPIO17 |
| RX (modem → master) | GPIO13 |

### Slave Pin Connections (ESP32-C6)
| Signal | GPIO |
|--------|------|
| TX (slave → modem) | GPIO11 |
| RX (modem → slave) | GPIO10 |

---

## 2. Communication Pattern

### Request-Response (Half-Duplex)
Because the power line is shared and the modem is half-duplex, only one side can transmit at a time. The protocol uses a strict **master-initiated request-response** pattern:

```
Master                          Slave
  │                               │
  │──── CMD packet ──────────────►│  (master sends every 1 second)
  │                               │  (slave waits 200ms guard delay)
  │◄─── STATUS packet ────────────│  (slave replies with sensor data)
  │                               │
  │  (master waits up to 3 seconds for STATUS)
  │  (if no reply → PLC link timeout → SAFETY_OVERRIDE)
  │                               │
```

**Why 200ms guard delay on slave?**  
The modem needs time to switch from receive mode to transmit mode after the last byte of the CMD arrives. Without this delay, the first bytes of the slave's reply collide with the modem's tail. 200ms was found to work reliably in testing.

**Why 3 second timeout on master?**  
At 100 bps effective throughput, a 17-byte STATUS packet takes ~1.4 seconds to arrive. 3 seconds allows one full CMD-STATUS cycle plus margin.

---

## 3. Packet Frame Format

Every packet, regardless of direction, follows this frame layout:

```
Byte index:  0      1      2      3      4 ... N    N+1    N+2
             ┌──────┬──────┬──────┬──────┬─────────┬──────┬──────┐
             │ 0xAA │ LEN  │ TYPE │ SEQ  │ PAYLOAD │ CRC8 │ 0x55 │
             └──────┴──────┴──────┴──────┴─────────┴──────┴──────┘
               Start  Len   Type   Seq   Data...    CRC   End
```

| Field | Size | Description |
|-------|------|-------------|
| `startByte` | 1 byte | Always `0xAA` — marks packet start |
| `length` | 1 byte | Number of payload bytes (TYPE + SEQ + data fields) |
| `packetType` | 1 byte | `0x01` = STATUS, `0x02` = CMD |
| `sequence` | 1 byte | Rolling counter 0–255, wraps around. Receiver uses this to detect lost packets |
| payload | variable | Sensor data or commands (see below) |
| `crc8` | 1 byte | Error check — covers bytes from TYPE through last payload byte |
| `endByte` | 1 byte | Always `0x55` — marks packet end |

**What the CRC covers:**  
CRC-8 is calculated over `[TYPE, SEQ, all payload fields]`. The `startByte`, `length`, `crc8`, and `endByte` themselves are NOT included in the CRC calculation.

---

## 4. CRC-8 Algorithm

**Algorithm:** CRC-8/SMBUS  
**Polynomial:** 0x07  
**Initial value:** 0x00  
**No input/output reflection**

```c
uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc = crc << 1;
        }
    }
    return crc;
}
```

**Example:**  
For a STATUS packet, the CRC input starts at byte index 2 (the `packetType` byte) and covers 13 bytes (STATUS_PAYLOAD_LEN = 13).

---

## 5. CMD Packet (Master → Slave)

**Direction:** Master sends to Slave  
**Frequency:** Once per second  
**Total wire size:** 9 bytes  
**Payload length (LEN field):** 5

### Byte Layout
```
Byte  0:  0xAA          startByte
Byte  1:  0x05          length = 5
Byte  2:  0x02          packetType = CMD
Byte  3:  sequence      rolling 0–255
Byte  4:  pwmInternal   0–100 (percent duty cycle for internal heater SSR)
Byte  5:  pwmBoost      0–100 (percent duty cycle for boost heater SSR)
Byte  6:  cmdFlags      bitmask (see flags below)
Byte  7:  crc8          CRC over bytes 2–6
Byte  8:  0x55          endByte
```

### CMD Flags (Byte 6)
| Bit | Constant | Meaning |
|-----|----------|---------|
| Bit 0 | `CMD_HEATER_ENABLE` | Allow internal tank heater to run |
| Bit 1 | `CMD_BOOST_ENABLE` | Allow boost inline heater to run |
| Bit 7 | `CMD_EMERGENCY_STOP` | Cut both SSRs immediately — overrides everything |

### Example CMD (heating tank at 100%, no boost)
```
AA 05 02 2A 64 00 01 XX 55
│  │  │  │  │  │  │  │  └─ end
│  │  │  │  │  │  │  └──── CRC
│  │  │  │  │  │  └─────── cmdFlags = 0x01 (HEATER_ENABLE)
│  │  │  │  │  └────────── pwmBoost = 0 (off)
│  │  │  │  └───────────── pwmInternal = 100 (100%)
│  │  │  └──────────────── sequence = 42
│  │  └─────────────────── type = 0x02 (CMD)
│  └────────────────────── length = 5
└───────────────────────── start = 0xAA
```

---

## 6. STATUS Packet (Slave → Master)

**Direction:** Slave sends to Master  
**Frequency:** Once per second (after receiving CMD)  
**Total wire size:** 17 bytes  
**Payload length (LEN field):** 13

### Byte Layout
```
Byte  0:  0xAA          startByte
Byte  1:  0x0D          length = 13
Byte  2:  0x01          packetType = STATUS
Byte  3:  sequence      rolling 0–255
Byte  4–5: tempInternal  int16, tank temperature [°C × 10]
Byte  6–7: tempBoilerOut int16, boiler outlet temperature [°C × 10]
Byte  8–9: tempBoostOut  int16, boost outlet temperature [°C × 10]
Byte 10–11: flowRate     uint16, flow rate [L/min × 10]
Byte 12–13: powerWatts   uint16, instantaneous power [W]
Byte 14:  statusByte    bitmask (see flags below)
Byte 15:  crc8          CRC over bytes 2–14
Byte 16:  0x55          endByte
```

### Fixed-Point Encoding
Temperatures and flow are stored as integers multiplied by 10 to preserve one decimal place without using floating-point on the wire:

| Field | Encoding | Example |
|-------|----------|---------|
| Temperature | int16 × 10 | 65.2°C → stored as `652` (0x028C) |
| Flow rate | uint16 × 10 | 7.5 L/min → stored as `75` (0x004B) |
| Power | uint16, no scaling | 3000W → stored as `3000` (0x0BB8) |

**Byte order:** Little-endian (ESP32 native). Low byte first.

### STATUS Flags (Byte 14)
| Bit | Constant | Meaning |
|-----|----------|---------|
| Bit 0 | `STATUS_FLOW_ACTIVE` | Water is flowing (flow ≥ 1.0 L/min) |
| Bit 1 | `STATUS_INTERNAL_ON` | Internal tank SSR is energised |
| Bit 2 | `STATUS_BOOST_ON` | Boost inline SSR is energised |
| Bit 3 | `STATUS_FAULT` | A safety fault is currently active |

### Example STATUS (cold tank, no flow, no load)
```
AA 0D 01 5A 00 C8 00 BE 00 BE 00 00 00 00 00 XX 55
│  │  │  │  ├──┤  ├──┤  ├──┤  ├────┤  ├────┤ │  │
│  │  │  │  │200│  190   190    0      0     │ │  │
│  │  │  │  └─20.0°C     19.0°C  0L/m   0W  │ │  │
│  │  │  └── sequence = 90                    │ │  │
│  │  └────── type = 0x01 (STATUS)            │ │  │
│  └───────── length = 13                     │ └─end
└──────────── start = 0xAA               CRC─┘
```

---

## 7. Sequence Numbers and Loss Detection

Both CMD and STATUS packets carry a 1-byte rolling sequence counter (0–255, wraps to 0 after 255).

**Master** increments its TX sequence on every CMD sent.  
**Slave** increments its TX sequence on every STATUS sent.

The receiver checks if the incoming sequence is `(last_received + 1) mod 256`. If not, it logs a packet loss warning:
```
[COMMS RX] Packet loss: expected seq 91 got 0
```

This is **non-fatal** — the receiver accepts the packet anyway and updates its last-seen sequence. The sequence counter resets to 0 when either ESP32 reboots, which also appears as a "loss" in the log on the first packet after restart.

---

## 8. Error Handling

| Error | Detection | Action |
|-------|-----------|--------|
| Bad CRC | Calculated CRC ≠ received CRC | Packet discarded, logged |
| Wrong packet type | Slave receives its own CMD echo | Ignored (logged as "Own CMD echo ignored") |
| Unknown packet type | Unexpected type byte | Discarded, logged |
| Short/corrupted frame | Bad end byte `0x55` | Discarded, logged |
| RX timeout (mid-packet) | No new byte for 200ms | State machine resets to WAIT_START |
| PLC link timeout | No STATUS received within 3s | Master → `SAFETY_OVERRIDE`, UI shows "No Signal" |

---

## 9. SystemManager — How CMD Values Are Decided

The master does not just forward user input to the slave. It runs a state machine called **SystemManager** which calculates the correct `pwmInternal`, `pwmBoost`, and `cmdFlags` based on sensor data and UI state.

### States (priority order, highest first)
| Priority | State | Condition | pwmInternal | pwmBoost |
|----------|-------|-----------|-------------|----------|
| 1 | `SAFETY_OVERRIDE` | No PLC link OR tank ≥ 85°C | 0 | 0 |
| 2 | `STATE_OFF` | User pressed OFF on UI | 0 | 0 |
| 3 | `STATE_SHOWER_BOOST` | Flow > 0.5 L/min | 0 | 100 |
| 4 | `STATE_HEATING_TANK` | Tank temp < 40°C | 100 | 0 |
| 5 | `STATE_STANDBY` | Tank temp ≥ 40°C, no flow | 0 | 0 |

### Dual-Target Energy Saving Philosophy
- Internal heater only maintains the tank at **40°C** (base temperature — low standing heat loss)
- Boost heater fires **only when tap is open** (flow detected), bringing water up to shower temperature
- **Both heaters never run simultaneously** — prevents tripping the 16A breaker with two 3000W elements

---

## 10. Serial Monitor Log Format

When running normally (TEST_MODE 0), the master prints one line per cycle:

```
[MASTER] seq=183  tInt=20.0 flow=0.0 pwr=2000W  ->  pwmInt=100% pwmBst=0% [STATE_HEATING_TANK]
```

Fields: sequence number, tank temperature, flow rate, power measured by slave, then the decision: internal PWM, boost PWM, state name.
