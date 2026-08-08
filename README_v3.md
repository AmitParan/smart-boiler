# Smart Boiler — Project README (Branch: v7)

Last updated: July 2026  
Git branch: `v7`  
Last commit: `41c3669` — fix(slave): S8 uncommanded-current always active in DEMO; REALTIME only when sensor calibrated

---

## 1. System Overview

Two-device IoT boiler controller communicating over the 220V power line (PLC modem).

| Role   | MCU             | Board                          | IDE        |
|--------|-----------------|--------------------------------|------------|
| Master | ESP32-S3        | Viewe UEDX80480050E-WB-A       | This PC    |
| Slave  | ESP32-C6        | Generic ESP32-C6-DevKitC-1     | Personal PC|

The master runs a touchscreen dashboard (LVGL v8, 800×480) and sends heater commands over the power line every second. The slave reads sensors, controls two SSRs, and sends status back.

---

## 2. Hardware Wiring

### 2.1 Master (ESP32-S3) — Comms Pins
| Signal       | GPIO | Notes                                      |
|--------------|------|--------------------------------------------|
| TX → KQ-330  | 17   | MASTER_TX_PIN in config.h                  |
| RX ← KQ-330  | 13   | MASTER_RX_PIN — SD-MISO pin, SD not used   |
| Serial debug | 43/44| UART0 (USB CDC off: ARDUINO_USB_CDC_ON_BOOT=0) |

> ⚠️ KQ-330 DOUT is **5V TTL**. GPIO13 has no pull-up so it works without a level shifter, but a proper 5V→3.3V level shifter should be added before production.

### 2.2 Slave (ESP32-C6) — All Pins
| Signal              | GPIO | Component                         |
|---------------------|------|-----------------------------------|
| DS18B20 1-Wire bus  | 7    | Three temp sensors on one wire    |
| Flow sensor         | 6    | YF-B6 via MOSFET Q2 + level shifter U1 |
| SSR Internal        | 4    | Tank heater SSR                   |
| SSR Boost           | 5    | Inline boost heater SSR           |
| Current sensor      | 3    | ACS758LCB-050B via 1.8kΩ/3.3kΩ divider |
| PLC RX              | 10   | KQ-330 via level shifter U3       |
| PLC TX              | 11   | KQ-330                            |
| Serial debug        | USB  | Serial at 115200                  |

### 2.3 KQ-330 PLC Modem
- Half-duplex UART 9600 baud over 220V power line
- Master TX → KQ-330 DIN, KQ-330 DOUT → Master RX
- Slave TX → KQ-330 DIN, KQ-330 DOUT → Slave RX
- Both modems must be on the **same electrical phase** of the mains

### 2.4 Current Sensor (ACS758LCB-050B)
- Powered at 5V, sensitivity 40 mV/A, quiescent output 2.5V (VCC/2)
- Output divided by 1.8kΩ / 3.3kΩ voltage divider before ESP32 ADC
- Divider ratio: 3.3 / 5.1 = 0.647
- Calibrates zero reference at boot (200 samples × 5ms = 1 second)
- **Known issue:** if current is flowing at boot, calibration offset is wrong → safety fault fires immediately. See Section 7.

### 2.5 SSRs
- Both SSRs are **zero-crossing AC solid-state relays**
- Controlled by LEDC PWM: `ledcAttach(pin, 1000Hz, 8-bit)`, duty = 127 when ON
- **Known limitation:** 1kHz 50% duty (127/255) causes the SSR to fire on only ~50% of AC half-cycles → load gets ~50% power. Works for a resistive boiler element but may not illuminate small LED loads
- **Minimum holding current:** Zero-crossing SSRs require a minimum load current (~50–200mA depending on model) to keep the triac latched. A 9W LED (40mA) may be below this threshold and will NOT light up. The real boiler element (~13A) will work correctly.

---

## 3. Software Architecture

### 3.1 Master Tasks (ESP32-S3)

```
main.cpp
  ├── LVGL render loop (loop())
  ├── TaskMasterComms (comms_master.cpp)
  │     ├── Serial1 RX → receive STATUS packets
  │     ├── SystemManager::process() → decide PWM/flags
  │     ├── Serial1 TX → send CMD packets (every 1 second)
  │     └── handleTestMenu() — Serial console: '1'=ON '0'=OFF 'r'=auto '?'=help
  └── UI (ui_manager.cpp) — LVGL dashboard, boiler_state, target_temperature
```

**SystemManager state machine** (`SystemManager.cpp`):
| Priority | State               | Condition                                         | Output                    |
|----------|---------------------|---------------------------------------------------|---------------------------|
| 1 (high) | SAFETY_OVERRIDE     | plcConnected = false OR temp ≥ 85°C               | pwmInt=0, boost=0         |
| 2        | STATE_OFF           | uiStateOn = false                                 | pwmInt=0, boost=0         |
| 3        | STATE_SHOWER_BOOST  | flow > 0.5 L/min AND currentTemp < 45°C           | pwmInt=0, boost=100       |
| 3        | STATE_SHOWER_BOOST  | flow > 0.5 L/min AND currentTemp ≥ 45°C           | pwmInt=0, boost=0 (warm enough) |
| 4        | STATE_HEATING_TANK  | no flow AND currentTemp < 40°C                    | pwmInt=100, boost=0       |
| 5 (low)  | STATE_STANDBY       | no flow AND currentTemp ≥ 40°C                    | pwmInt=0, boost=0         |

**Key constants** (`SystemManager.h`):
- `TARGET_TANK_TEMP = 40°C` — internal heater target
- `BOOST_CUTOFF_C = 45°C` — boost heater disables above this tank temp
- `TEMP_CUTOFF_C = 85°C` — hard safety cutoff
- `FLOW_THRESHOLD_LPM = 0.5 L/min` — minimum flow to trigger boost

> Note: `plcConnected` is hardcoded to `true` in `sendCommand()`. SAFETY_OVERRIDE never fires. This needs fixing in a future version (BUG-2).

### 3.2 Slave Tasks (ESP32-C6, FreeRTOS, all pinned to Core 0)

```
main.cpp
  ├── TaskSafety       — priority 4 — runs every 50ms, mutex snapshot, hard-cuts SSRs on fault
  ├── TaskPWM_Internal — priority 3 — mutex snapshot of cmd at burst cycle start
  ├── TaskPWM_Boost    — priority 3 — mutex snapshot of cmd+flow, runtime mode check
  ├── TaskFlow         — priority 2 — pulse counter ISR, guard_flow protects write
  ├── TaskTemp         — priority 2 — DS18B20 readings, guard_temps protects write
  ├── TaskCurrent      — priority 2 — ACS758 RMS sampling, guard_current protects write
  ├── TaskPLC          — priority 2 — triggered: send STATUS after CMD received (200ms guard)
  └── TaskSerial       — priority 1 — serial console: 'b'=BENCH_TEST 'p'=PRODUCTION '?'=status
```

**FreeRTOS mutex layout** (created in `setup()` before any task starts):
| Mutex | Guards |
|---|---|
| `guard_temps` | `temps[3]` — written by TaskTemp, read by TaskPLC + TaskSafety |
| `guard_flow` | `current_flow` — written by TaskFlow, read by TaskPLC + TaskSafety + TaskPWM_Boost |
| `guard_current` | `current_rms`, `power_watts` — written by TaskCurrent, read by TaskPLC + TaskSafety |
| `guard_cmd` | `cmd_pwm_internal/boost/flags` — written by TaskPLC, read by TaskPWM + TaskSafety |

**TaskPLC communication pattern** (half-duplex KQ-330):
```
Master sends CMD every 1s
  └─ Slave receives CMD
       └─ 200ms guard (KQ-330 echo clears)
            └─ Slave sends STATUS
                 └─ Master receives STATUS within 3s window

Heartbeat: if no CMD received for 5s, slave pushes STATUS anyway
```
> Key: slave must NOT transmit independently at 1s intervals — that causes
> half-duplex collision with the master’s CMD and both packets are lost.

**PWM (time-proportional burst) logic** (both SSR tasks):
```
window = 2000ms
on_ms  = (2000 × pwm_val%) / 100
off_ms = 2000 - on_ms

→ ledcWrite(pin, 127)   ← SSR ON for on_ms
→ ledcWrite(pin, 0)     ← SSR OFF for off_ms
```

**Safety checks** (every 50ms):
1. Any sensor > 80°C → fault
2. Boost commanded with flow < 1.0 L/min → fault
3. `current_rms > 0.5A` with no command → fault (SSR short detection)

On fault: `ledcWrite(both SSRs, 0)` + `system_fault = true` (latches until reboot).

---

## 4. Communication Protocol

Binary packet over KQ-330 PLC modem, max 1 packet/second.

**Frame:** `[0xAA | LEN | TYPE | SEQ | ...payload... | CRC8 | 0x55]`  
CRC-8/SMBUS (poly 0x07) covers TYPE through last payload byte.

### CMD packet (Master → Slave, 9 bytes total)
| Field       | Size | Description                      |
|-------------|------|----------------------------------|
| startByte   | 1    | 0xAA                             |
| length      | 1    | 5 (payload length)               |
| packetType  | 1    | 0x02                             |
| sequence    | 1    | wrapping counter                 |
| pwmInternal | 1    | 0–100%                           |
| pwmBoost    | 1    | 0–100%                           |
| cmdFlags    | 1    | bit0=HEATER_ENABLE, bit1=BOOST_ENABLE, bit7=EMERGENCY_STOP |
| crc8        | 1    | CRC                              |
| endByte     | 1    | 0x55                             |

### STATUS packet (Slave → Master, 17 bytes total)
| Field         | Size | Description                         |
|---------------|------|-------------------------------------|
| startByte     | 1    | 0xAA                                |
| length        | 1    | 13 (payload length)                 |
| packetType    | 1    | 0x01                                |
| sequence      | 1    | wrapping counter                    |
| tempInternal  | 2    | tank temp × 10 (fixed-point)        |
| tempBoilerOut | 2    | boiler outlet temp × 10             |
| tempBoostOut  | 2    | boost outlet temp × 10              |
| flowRate      | 2    | L/min × 10                          |
| powerWatts    | 2    | watts (uint16)                      |
| statusByte    | 1    | bit0=FLOW_ACTIVE, bit1=INTERNAL_ON, bit2=BOOST_ON, bit3=FAULT |
| crc8          | 1    | CRC                                 |
| endByte       | 1    | 0x55                                |

### Log format (both sides)
```
[M->S] seq= 42 | pwmInt=100%  pwmBst=  0% | flags=0x01 | [STATE_HEATING_TANK]
[M<-S] seq= 42 | t1= 25.2  t2= 25.5  t3= 26.0 | flow= 0.0  pwr=   0W | sts=0x02
[S<-M] seq= 42 | pwmInt=100%  pwmBst=  0% | flags=0x01
[S->M] seq= 15 | t1= 25.2  t2= 25.5  t3= 26.0 | flow= 0.0  pwr=   0W | sts=0x02
```
- `sts=0x02` → SSR Internal ON (normal heating)
- `sts=0x08` → FAULT latched
- `pwr=0W` with 9W LED test load is expected (40mA < 0.2A noise filter threshold)

---

## 5. PlatformIO Configuration

### Master (`master/platformio.ini`)
```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.11/platform-espressif32.zip
board = BOARD_VIEWE_UEDX80480050E_WB_A
framework = arduino
upload_port = COM7       ← change to your port
monitor_port = COM7      ← change to your port
monitor_speed = 115200
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DBOARD_HAS_PSRAM
    ...
```

### Slave (`slave/platformio.ini`)
```ini
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
```

---

## 6. Demo / Realtime Mode Framework

### 6.1 Master — Operation Mode

Toggled at runtime from the **Settings** screen ("Switch" button).

| Mode | Default | Behaviour |
|---|---|---|
| `MODE_DEMO` | ✅ boot default | Slave uses mock sensor data driven by master CMD flags. All interlocks active. Faults auto-clear (2s). |
| `MODE_REALTIME` | — | Slave reads real DS18B20 / YF-B6 / ACS758 sensors. All safety interlocks permanently active. |

Auto-switch: `CMD_DEMO_ACTIVE` flag in every CMD propagates the master mode to the slave automatically.  
Serial override: `d` = DEMO, `r` = REALTIME, `?` = status

### 6.2 Slave — SystemMode

Slave switches automatically when CMD contains `CMD_DEMO_ACTIVE` flag.

| Mode | Serial command | Behaviour |
|---|---|---|
| `MODE_BENCH_TEST` | `b` | Legacy: sensors + interlocks bypassed. Boot default. |
| `MODE_DEMO` | `d` (or auto via CMD) | Mock sensors from master flags. All interlocks active. Faults auto-clear after 2s. |
| `MODE_PRODUCTION` | `p` | Real sensors, all interlocks permanent. |

### 6.3 Demo Scenario Flags in cmdFlags

| Bit | Constant | Effect on slave |
|---|---|---|
| 3 | `CMD_DEMO_ACTIVE` | Slave enters MODE_DEMO, uses mock sensor logic |
| 4 | `CMD_DEMO_FAULT_SIM` | Slave reports 13.6A even when SSRs off (S8) |
| 2 | `CMD_DEMO_FLOW` | Slave injects flow = 6.5 L/min |
| 5 | `CMD_DEMO_OVERTEMP` | Slave injects temp = 87°C (triggers safety in S7) |

Slave infers mock temp from SSR command: HEATER→ON = 25°C, BOOST→ON = 35°C, idle = 42°C.

### 6.4 Automated 8-Scenario Test Bench

`TaskAutomatedTestBench` runs on Core 1 and cycles through 8 scenarios automatically (~60s each). Starts 6s after boot, only executes when `APP_MODE_DEMO`. Values and intra-scenario timings are aligned to the authoritative MATLAB models in `Models/Models/` (`Scenario_1..8`, `boiler_params.m`).

| # | Category | Scenario | Temp | Flow | Expected State | SSR Int | SSR Boost |
|---|---|---|---|---|---|:---:|:---:|
| S1 | A | Pre-Heating | 20°C | 0 | STATE_HEATING_TANK | **ON** | OFF |
| S2 | A | Preheated Shower | 40°C | 8 @ t=30s | STATE_SHOWER_BOOST | OFF | **ON** |
| S3 | A | Warm Shower Cutoff | 45°C | 8 @ t=30s | STATE_SHOWER_BOOST | OFF | OFF |
| S4 | A | Standby | 42°C | 0 | STATE_STANDBY | OFF | OFF |
| S5 | B | Solar Bypass | 28→42°C | 0 | STANDBY (forced) | OFF | OFF |
| S6 | C | PLC Loss | 20°C | 0 | SAFETY_OVERRIDE | OFF | OFF |
| S7 | C | Overtemp | 70→86°C | 0 | SAFETY_OVERRIDE | OFF | OFF |
| S8 | C | Stuck SSR | 70°C | 0 | FAULT (0x08) | OFF | OFF |

> S6 timeline: idle 0–15s → heating 15–30s → PLC link cut at t=30s. S7 ramps from 70°C at 0.4°C/s (software cutoff 80°C ≈ t+25s, HW interlock 85°C ≈ t+37.5s).

### 6.5 Demo Serial Log Format

**Master (every ~2s):**
```
[MASTER] [seq=003] STATE: HEATING_TANK   | TEMP: 25.0°C | FLOW:  0.0LPM | PWR: 2499W | SEND -> INT: 100% | BST:   0%
[MASTER] ⚠️ PLC TIMEOUT > 5s! ENTERING SAFETY_OVERRIDE         ← event, once
[MASTER] ❌ RECEIVED STATUS_FAULT (0x08) FROM SLAVE! SYSTEM LOCKED.  ← event, once
```

**Slave (every ~2s):**
```
[SLAVE]  [seq=003] SSR_INT: ON  | SSR_BST: OFF | TEMP: 25.0°C | FLOW:  0.0LPM | PWR: 2499W | CURR: 11.4A
[SLAVE] ⚠️ SOFTWARE CUTOFF: Temp >= 80°C. Dropping PWM to 0%.     ← event, once
[SLAVE] ❌ HARDWARE INTERLOCK TRIP! (LM393N Simulation) -> CURRENT FORCED TO 0.0A!  ← event, once
[SLAVE] !!! CRITICAL FAULT: UNCOMMANDED CURRENT DETECTED IN 50ms LOOP!  ← event, once
```

---

## 7. Current Verified Status (as of branch v7, July 2026)

| Item | Status | Notes |
|---|---|---|
| Master ↔ Slave PLC comms | ✅ WORKS | Stable with KQ-330 timing constants locked |
| Master LVGL UI (v6 multi-screen) | ✅ WORKS | Home, Settings, Stats, Schedule, Diagnostics, Solar screens |
| Demo / Realtime mode toggle | ✅ WORKS | Settings page "Switch" button |
| 8-scenario automated test bench | ✅ WORKS | Runs on Core 1, 60s per scenario |
| S1 Pre-Heating (SSR INT ON) | ✅ WORKS | 2500W confirmed |
| S2 Preheated Shower (SSR EXT ON) | ✅ WORKS | 3000W confirmed |
| S3 Warm Shower Boost Cutoff | ✅ WORKS | Both SSRs off at 45°C |
| S4 Standby | ✅ WORKS | 0W confirmed |
| S5 Solar Bypass (forced 0W) | ✅ WORKS | 28→42°C sweep, SSRs stay off via override |
| S6 PLC Loss watchdog | ✅ WORKS | Slave safety trips after 5s, auto-clears in demo |
| S7 Overtemp cutoff | ✅ WORKS | Software @80°C, HW interlock sim @85°C |
| S8 Stuck SSR detection | ✅ WORKS | Uncommanded current fault within 50ms |
| UI power button locked in demo | ✅ WORKS | Cannot toggle boiler during demo |
| Mock power values | ✅ WORKS | SSR_INT=2500W (11.36A), SSR_BOOST=3000W (13.64A) |
| SystemMode runtime (slave) | ✅ WORKS | d=DEMO, r=REALTIME serial commands + auto via CMD_DEMO_ACTIVE |
| FreeRTOS mutexes (4 guards) | ✅ WORKS | guard_temps/flow/current/cmd |
| TaskMasterComms on Core 0 | ✅ WORKS | Isolated from LVGL (Core 1), no preemption |
| KQ-330 timing locked | ✅ WORKS | 2ms/byte TX + 200ms guard — DO NOT CHANGE |
| WiFi + NTP sync | ✅ WORKS | Auto-reconnect, time shown on dashboard |
| Weather + Solar Forecast | ✅ WORKS | Fetched on WiFi connect, updated hourly/daily |
| Temp display (REALTIME, no sensor) | ✅ FIXED | Shows `---` instead of stale value when DS18B20 disconnected |
| Uncommanded-current check | ✅ WORKS | Auto-disabled when VREF out of spec (ADC calibration issue); S8 always active in DEMO |
| 220V actual load test | ❌ UNTESTED | Needs resistive load (≥40W), not 9W LED |

---

## 8. Known Bugs and Limitations

### BUG-1: Current sensor false fault on boot (safety_task.cpp)
**Symptom:** `[SAFETY] FAULT: Current detected without command` fires continuously, `sts=0x08`, SSR never turns on.  
**Root cause:** If the SSR is conducting when the slave boots (stuck triac from previous session, or any other reason), the 1-second zero-current calibration in `TaskCurrent` captures the wrong VREF. The offset bakes in ~4A phantom current that persists even after the SSR is commanded OFF.  
**Workaround:** Disconnect 220V from the SSR output, then reboot the slave.  
**Proper fix needed:** Add VREF sanity clamp in `current_task.cpp` (reject calibration if VREF deviates >10% from 2.5V nominal) AND ensure SSR pins are explicitly LOW before calibration runs.

### BUG-2: `plcConnected` hardcoded to `true` (comms_master.cpp)
**Symptom:** SAFETY_OVERRIDE state in SystemManager never fires, even if PLC communication is actually lost.  
**Root cause:** `inputs.plcConnected = true` is hardcoded in `sendCommand()`.  
**Proper fix needed:** Track last received STATUS timestamp; set `plcConnected = false` if no STATUS received in >5 seconds.

### NOTE (was BUG-3): SSR LEDC 1 kHz carrier is REQUIRED by the hardware watchdog (pwm_task_internal.cpp, pwm_task_boost.cpp)
The internal/boost SSR gate is a **DC-blocking capacitive watchdog** (project book §9.1.9, "Internal/External Watchdog Gate"): it latches only while it receives a continuous high-frequency pulse train, and treats a constant DC level as a controller-freeze fault, cutting the heater within ~1s (RC τ≈1s).  
**Therefore `ledcWrite(pin, 127)` @ 1 kHz is mandatory, not a defect** — it supplies that carrier.  
**⚠️ DO NOT** replace it with `digitalWrite(HIGH)`: constant DC would trip the hardware watchdog and the heater could never sustain ON. The earlier "replace LEDC with digitalWrite" (SW-3) suggestion is **retracted**.  
**Side effect (accepted):** a zero-crossing SSR driven at 1 kHz fires on ~50% of AC half-cycles, so tiny LED test loads (below triac holding current) may not light. The real resistive boiler element works correctly. For bench LED tests use a 40W+ incandescent/resistive load.

### LIMITATION: 9W LED is not a valid SSR test load
Zero-crossing SSRs require a minimum holding current (typically 50–200mA). A 9W 220V LED draws only ~40mA. The triac drops out every zero crossing. Use a 40W+ incandescent bulb or the real boiler element for testing.

---

## 9. TODO — Next Development Steps (Priority Order)

### Phase 1: Hardware Validation Checklist
Before writing more code, confirm these hardware items work:

- [ ] **HW-1** Flash both devices on v3, confirm `[S<-M]` and `[S->M]` appear in both monitors with no `DROP` lines
- [ ] **HW-2** Connect a resistive test load (≥40W incandescent bulb) to SSR_INT output. Command boiler ON. Confirm load powers on.
- [ ] **HW-3** Measure current with a clamp meter when load is ON. Confirm `pwr` reading in slave monitor is reasonable (use ≥40W resistive load; 9W LED is too low for SSR triac holding current)
- [ ] **HW-4** Test temperature reading by warming a sensor. Confirm `t1` rises in monitor.
- [ ] **HW-5** Test flow sensor by running water. Confirm `flow > 0` appears.
- [ ] **HW-6** Test overtemp safety: heat a sensor to >80°C. Confirm `[SAFETY] FAULT: Overheat` fires and SSR turns off.
- [ ] **HW-7** Test uncommanded current detection: command OFF, then manually force SSR ON with a wire. Confirm fault fires within 50ms.
- [ ] **HW-8** Add 5V→3.3V level shifter on master RX (GPIO13) from KQ-330 DOUT before final install.

### Phase 2: Software Bug Fixes
- [ ] **SW-1** Fix BUG-2: implement PLC watchdog in comms_master.cpp — set `plcConnected = false` after 5 seconds with no STATUS received
- [ ] **SW-2** Fix BUG-1: add VREF sanity clamp in `current_task.cpp` AND drive SSR pins LOW explicitly before calibration starts
- [x] **SW-3** ❌ REJECTED — do NOT replace `ledcWrite(pin, 127)` with `digitalWrite(HIGH)`. The 1 kHz carrier is required by the DC-blocking hardware watchdog gate (see NOTE "was BUG-3"). Constant DC would trip the interlock and cut the heater. Closed, no action.
- [ ] **SW-4** Add `system_fault` reset mechanism (e.g. CMD_EMERGENCY_STOP cleared = reset fault) so recovery doesn't require hardware reboot
- [ ] **SW-5** Increase current noise floor for real boiler: change `0.2f` threshold to match actual noise profile after real load testing

### Phase 3: UI Improvements (Master)
- [ ] **UI-1** Add PLC connection status indicator (green/red) on dashboard — feed from `plcConnected` watchdog
- [ ] **UI-2** Show current SystemManager state name on screen (HEATING_TANK, STANDBY, etc.)
- [ ] **UI-3** Add FAULT indicator on screen — reads `STATUS_FAULT` bit from slave STATUS packet
- [ ] **UI-4** Show all three temperature sensors separately (currently only t1 and t3 shown)
- [ ] **UI-5** Add historical temperature graph (last 30 minutes)
- [ ] **UI-6** Test menu: expose serial test commands ('1'/'0'/'r') via touchscreen buttons

### Phase 4: FreeRTOS and Code Quality
- [x] **RTOS-1** ✅ TaskMasterComms pinned to Core 0 — LVGL on Core 1 cannot preempt it
- [ ] **RTOS-2** Replace shared global variables in slave (`shared_data.cpp`) with FreeRTOS queues or mutexes to prevent data races
- [ ] **RTOS-3** Add task watchdog (WDT) to slave — any task stuck > 5s should trigger a reboot
- [ ] **RTOS-4** Review task stack sizes — currently all 4096. Monitor with `uxTaskGetStackHighWaterMark()` under real load
- [ ] **RTOS-5** Prioritize safety more explicitly: safety task priority 4 is correct, but verify it always preempts PWM (priority 3) within the 50ms window

### Phase 5: System Integration Test Plan
Run this full test sequence before declaring the system production-ready:

```
TEST SEQUENCE
─────────────────────────────────────────────────────────────────────
T1  Cold boot, both devices off 220V
    Expected: [CURRENT] Calibrated VREF ≈ 2.500V, no SAFETY faults

T2  Master UI: boiler OFF, temp 25°C (below 40°C target)
    Expected: SystemManager STATE_OFF, master sends flags=0x00 pwmInt=0

T3  Master UI: boiler ON, temp 25°C
    Expected: SystemManager STATE_HEATING_TANK
             Master sends flags=0x01 pwmInt=100
             Slave reports sts=0x02 (SSR ON)
             Resistive test load powers on

T4  Simulate temp > 40°C (warm sensor by hand)
    Expected: SystemManager STATE_STANDBY, pwmInt=0, SSR turns off

T5  Simulate flow > 0.5 L/min (run water)
    Expected: SystemManager STATE_SHOWER_BOOST

T6  Simulate overheat: heat sensor to >80°C
    Expected: [SAFETY] FAULT: Overheat, sts=0x08, all SSRs cut

T7  PLC disconnect: unplug one modem
    Expected (after SW-1 fix): plcConnected=false, STATE→SAFETY_OVERRIDE, SSRs off

T8  SSR short simulation (SW-2 fix required): force current with SSR OFF
    Expected: [SAFETY] FAULT: Current detected without command within 50ms
─────────────────────────────────────────────────────────────────────
```

---

## 10. Flash Instructions

### Master (this PC, COM7)
```bash
cd c:\Users\eladmual\smart-boiler\master
git pull origin v7
pio run --target upload
pio device monitor --port COM7 --baud 115200
```

### Slave (personal PC)
```bash
cd "C:\Users\User\Desktop\smart boiler\smart-boiler"
git pull origin v7
cd slave
pio run --target upload
pio device monitor --baud 115200
```

---

## 11. Git Branch Notes

| Branch | Status | Description |
|---|---|---|
| `v7` | ✅ **ACTIVE** | Full demo framework, multi-screen UI, 8-scenario test bench |
| `v6-ui-upgrade` | ✅ STABLE | Multi-screen LVGL UI, WiFi, weather, solar forecast |
| `v5` | ✅ STABLE | FreeRTOS refactor baseline |
| `v4-freertos-refactor` | ✅ STABLE | Mutexes, SystemMode, Core 0 comms |
| `v3` | archived | Original working PLC comms |
| `main` | old | Pre-PLC version |

To start working: `git checkout v7` on both machines.
