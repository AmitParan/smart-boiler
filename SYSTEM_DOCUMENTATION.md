# Smart Boiler System — Documentation
**Last updated: May 9, 2026**
**Status: PLC communication ✅ | Temp sensor ✅ | Flow sensor ✅ | FreeRTOS refactor 🔄**

---

## Hardware Overview

### Master Unit — ESP32-S3 (5-inch touchscreen)
- **Board:** VIEWE UEDX80480050E-WB-A
- **Display:** 800×480 RGB IPS, ST7262 driver
- **Touch:** GT911 I2C
- **USB:** COM6 on work (Intel) laptop
- **Role:** UI display, user control, sends commands to slave every second

### Slave Unit — ESP32-C6 (PCB)
- **Board:** esp32-c6-devkitc-1
- **CPU:** RISC-V, single-core (Core 0 only — important!)
- **USB:** COM8 on personal computer
- **Role:** Reads sensors, controls SSRs, reports status to master

### KQ-330 PLC Modems (×2)
- **Protocol:** UART over 220V power line
- **Baud rate:** 9600
- **Effective throughput:** ~100 bps
- **Requirement:** Both modems must be on the same electrical circuit (same breaker)

---

## Wiring

### Master (ESP32-S3) ↔ KQ-330 Modem
| Signal | ESP32-S3 GPIO | KQ-330 Pin |
|--------|--------------|------------|
| TX (master sends) | GPIO17 | RX |
| RX (master receives) | GPIO13 | TX |

> **Note:** GPIO4/5 = RGB display data, GPIO10 = SD card CS, GPIO18 = Touch INT — all occupied. GPIO13 (SD MISO) and GPIO17 are free.

### Slave (ESP32-C6) ↔ KQ-330 Modem
| Signal | ESP32-C6 GPIO | KQ-330 Pin |
|--------|--------------|------------|
| TX (slave sends) | GPIO11 | RX |
| RX (slave receives) | GPIO10 | TX |

### Slave Sensors
| Sensor | GPIO | Status | Notes |
|--------|------|--------|-------|
| DS18B20 temp bus | GPIO7 | ✅ Working | Up to 3 sensors on one wire, 10-bit resolution |
| YF-B6 flow sensor | GPIO6 | ✅ Working | Interrupt-driven pulse counter |
| ACS758 current sensor | GPIO3 | ⚠️ Calibrated, not load-tested | Boot auto-calibrates VREF and sensitivity from actual VCC |
| SSR Internal heater | GPIO4 | ❌ Not yet tested | PWM output — needs LED/lamp test |
| SSR Boost heater | GPIO5 | ❌ Not yet tested | PWM output — needs flow confirmed first |

---

## Software Architecture

### Repository
- **URL:** https://github.com/AmitParan/smart-boiler.git
- **Production branch:** `main_master_slave` — everything verified and working
- **Active development branches:**
  - `freertos-refactor` — FreeRTOS architecture refactor (friend's work)
  - `feature/system-manager-v2` — future SystemManager improvements

### Master Project: `smart-boiler/master/`
Built and uploaded from **work (Intel) laptop** via PlatformIO.

Key files:
- `src/main.cpp` — setup, WiFi, NTP, FreeRTOS tasks
- `src/comms_master.cpp` — PLC task: sends CMD every 1s, waits up to 3s for STATUS
- `src/ui_manager.cpp` — LVGL UI
- `src/config.h` — pin definitions (RX=GPIO13, TX=GPIO17)
- `src/boiler_protocol.h` — shared binary protocol structs

### Slave Project: `smart-boiler/slave/`
Built and uploaded from **personal computer** via PlatformIO.

Key files:
- `src/main.cpp` — FreeRTOS task creation (all on Core 0)
- `src/plc_task.cpp` — request-response: receives CMD → sends STATUS
- `src/plc_comms.cpp` — state machine RX/TX, CRC-8
- `src/config.h` — all GPIO pin definitions
- `src/boiler_protocol.h` — shared binary protocol structs
- `src/shared_data.cpp/.h` — global sensor values shared between tasks
- `src/safety_task.cpp` — overheat, dry-fire, uncommanded current protection
- `src/current_task.cpp` — ACS758 RMS current with boot calibration
- `src/temp_task.cpp` — DS18B20 one-wire temperature
- `src/flow_task.cpp` — YF-B6 pulse counter
- `src/pwm_task_internal.cpp` — SSR PWM for internal heater
- `src/pwm_task_boost.cpp` — SSR PWM for boost heater

---

## Communication Protocol

### Transport
- Half-duplex UART over KQ-330 power-line carrier
- **Request-response:** master sends CMD → slave replies with STATUS
- No spontaneous transmissions from slave (prevents collision)
- 200ms guard delay on slave after CMD received before replying

### Packet Format
```
[0xAA] [LEN] [TYPE] [PAYLOAD...] [CRC8] [0x55]
```
- Start byte: `0xAA`
- End byte: `0x55`
- CRC: CRC-8/SMBUS (poly 0x07) over [TYPE + PAYLOAD]

### CMD Packet (master → slave): 9 bytes
```c
struct BoilerCmdPacket_t {
    uint8_t startByte;    // 0xAA
    uint8_t length;       // 5
    uint8_t packetType;   // 0x02
    uint8_t sequence;
    uint8_t pwmInternal;  // 0-100%
    uint8_t pwmBoost;     // 0-100%
    uint8_t cmdFlags;     // CMD_HEATER_ENABLE, CMD_BOOST_ENABLE, CMD_EMERGENCY_STOP
    uint8_t crc8;
    uint8_t endByte;      // 0x55
};
```

### STATUS Packet (slave → master): 17 bytes
```c
struct BoilerStatusPacket_t {
    uint8_t  startByte;      // 0xAA
    uint8_t  length;         // 13
    uint8_t  packetType;     // 0x01
    uint8_t  sequence;
    int16_t  tempInternal;   // °C × 10 fixed-point
    int16_t  tempBoilerOut;
    int16_t  tempBoostOut;
    uint16_t flowRate;       // L/min × 10
    uint16_t powerWatts;
    uint8_t  statusByte;     // STATUS_FAULT = 0x08
    uint8_t  crc8;
    uint8_t  endByte;        // 0x55
};
```

---

## Build & Upload Workflow

### Work Computer (master)
```bash
cd smart-boiler/master
# Edit code here
git add -A
git commit -m "description"
git push origin <branch>
# PlatformIO: Build → Upload → Monitor (COM6)
```

### Personal Computer (slave)
```bash
cd smart-boiler/slave
git pull origin feature/esp32-c6-toolchain-fix
# PlatformIO: Build → Upload → Monitor (COM8)
# DO NOT edit, commit or push from this computer
```

---

## FreeRTOS Architecture (freertos-refactor branch)

### What Changed
The monolithic task structure was refactored into a clean producer-consumer architecture using FreeRTOS queues (length=1, "latest value wins").

### Master Task Map
| Task | Priority | Core | Period | Role |
|------|----------|------|--------|------|
| TaskBrain | 3 (highest) | 1 | 250ms | Runs SystemManager, publishes CommandSnapshot |
| TaskMasterComms | 2 | 1 | 10ms poll / 1s TX | PLC send/receive |
| TaskUI | 2 | 1 | 10ms | LVGL render |
| TaskNetwork | 1 | 0 | event-driven | WiFi, NTP, weather |

### Slave Task Map
| Task | Priority | Core | Period | Role |
|------|----------|------|--------|------|
| TaskSafety | 4 (highest) | 0 | 50ms | Overheat, dry-fire, current protection |
| TaskPwmInternal | 3 | 0 | 50ms slices | SSR internal heater PWM |
| TaskPwmBoost | 3 | 0 | 50ms slices | SSR boost heater PWM |
| TaskFlow | 2 | 0 | 1000ms | YF-B6 pulse counter |
| TaskTemp | 2 | 0 | 1000ms | DS18B20 temperature |
| TaskCurrent | 2 | 0 | 500ms | ACS758 RMS current |
| TaskPLC | 2 | 0 | 10ms poll | PLC request-response |

### Shared State (Queue-based, thread-safe)
- **SensorSnapshot** — slave sensors → PLC task → master brain
- **CommandSnapshot** — master brain → PLC task → slave PWM tasks
- **UiSnapshot** — touchscreen UI → brain task

### Key Files Added
- `master/src/task_config.h` — all stack sizes, priorities, core affinity, timing constants
- `master/src/tasks/TaskBrain.cpp` — SystemManager wrapper task
- `master/src/shared/master_state.h/.cpp` — FreeRTOS queue wrappers
- `slave/src/task_config.h` — slave task configuration
- `slave/src/shared/slave_state.h/.cpp` — slave queue wrappers

---

## Hardware Files

PCB files are stored in `hardware/`:
```
hardware/
├── boiler.sch              ← Eagle schematic source
├── boiler.brd              ← Eagle board layout source
├── schematic.pdf           ← readable without Eagle
├── board-layout.pdf        ← readable without Eagle
├── gerbers/                ← 10 Gerber files (sent to JLCPCB)
├── drill/                  ← drill file
└── bom-cpl/                ← BOM + pick & place files for JLCPCB
```

Datasheets folder: `datasheets/` — component datasheets (to be added)

---

## Known Issues & Resolved Problems

| Problem | Root Cause | Fix |
|---------|-----------|-----|
| xTaskCreatePinnedToCore assert crash | ESP32-C6 is single-core, code pinned to Core 1 | Changed all core IDs to 0 |
| Serial2 not declared | ESP32-C6 only has Serial0/Serial1 | Changed to Serial1 |
| `as.exe` CreateProcess failure on Intel laptop | CrowdStrike EDR quarantining RISC-V toolchain | Use personal computer for slave builds |
| OneWire library incompatible with ESP32-C6 | GPIO register structure changed | Use git HEAD of OneWire |
| ACS758 reading 2694W at idle | ADC default attenuation 0dB = max 0.8V, sensor output 1.6V → saturation | Added `ADC_11db`, boot calibration |
| PLC TX/RX collision (half-duplex) | Both sides transmitting simultaneously | Switched to request-response protocol |
| Master GPIO18 RX = 0 bytes | GPIO18 is Touch INT line, pulled by hardware | Moved to GPIO13 (SD MISO, unused) |
| Master GPIO10 RX = 0 bytes | GPIO10 is SD card CS | Moved to GPIO13 |

---

## Current Status (April 27, 2026)

✅ Master boots, connects WiFi, syncs NTP, shows UI  
✅ Slave boots, all FreeRTOS tasks running on Core 0  
✅ PLC communication working: CMD every 1s, STATUS reply received  
✅ Current sensor calibrated, reads 0W at idle  
❌ Temp sensors not connected (reading -127°C)  
❌ Flow sensor not connected (reading 0.0 L/min)  
❌ SSRs not tested under load  
❌ UI not yet updating from received sensor data  
