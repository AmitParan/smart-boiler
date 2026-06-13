# Smart Boiler — Project README (Branch: v3)

Last updated: June 2026  
Git branch: `v3`  
Last commit: `a7a9f6d` — revert(slave): restore PWM/safety/current tasks to last working state

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
| Priority | State               | Condition                           | Output              |
|----------|---------------------|-------------------------------------|---------------------|
| 1 (high) | SAFETY_OVERRIDE     | plcConnected = false                | pwmInt=0, flags=0   |
| 2        | STATE_OFF           | uiStateOn = false                   | pwmInt=0, flags=0   |
| 3        | STATE_SHOWER_BOOST  | flow > 0.5 L/min                    | pwmInt=100, boost=100 |
| 4        | STATE_HEATING_TANK  | currentTemp < targetTemp (40°C)     | pwmInt=100, flags=enable |
| 5 (low)  | STATE_STANDBY       | temp OK, no flow                    | pwmInt=0, flags=0   |

> Note: `plcConnected` is hardcoded to `true` in `sendCommand()`. SAFETY_OVERRIDE never fires. This needs fixing in a future version.

### 3.2 Slave Tasks (ESP32-C6, FreeRTOS, all pinned to Core 0)

```
main.cpp
  ├── TaskSafety    — priority 4 — runs every 50ms, hard-cuts SSRs on fault
  ├── TaskPWM_Internal — priority 3 — drives PIN_SSR_INT (GPIO4)
  ├── TaskPWM_Boost    — priority 3 — drives PIN_SSR_EXT (GPIO5)
  ├── TaskFlow      — priority 2 — YF-B6 pulse counter
  ├── TaskTemp      — priority 2 — DS18B20 readings (750ms/conversion)
  ├── TaskCurrent   — priority 2 — ACS758 RMS current sampling
  └── TaskPLC       — priority 2 — KQ-330 receive CMD / send STATUS
```

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

## 6. Current Verified Status (as of branch v3)

| Item                               | Status  | Notes                                      |
|------------------------------------|---------|---------------------------------------------|
| Master ↔ Slave PLC comms           | ✅ WORKS | Every second, no drops                     |
| Master LVGL UI touchscreen         | ✅ WORKS | Boiler on/off, temp display, flow, power   |
| SystemManager state machine        | ✅ WORKS | Correctly enters STATE_HEATING_TANK         |
| Slave receives CMD, sends STATUS   | ✅ WORKS | `[S<-M]` and `[S->M]` log lines confirmed  |
| SSR Internal indicator LED         | ✅ WORKS | Red LED on SSR lights when commanded ON    |
| DS18B20 temperature (3 sensors)    | ✅ WORKS | t1/t2/t3 reporting correctly               |
| Flow sensor (YF-B6)                | ✅ WORKS | Reports 0.0 L/min at rest                  |
| Current sensor (ACS758)            | ⚠️ PARTIAL | Reads 0W for 9W LED (expected — see §4); real boiler load untested |
| Safety: overheat protection        | ✅ CODE OK | Not hardware-tested at high temp yet       |
| Safety: flow interlock             | ✅ CODE OK | Not hardware-tested with boost SSR yet     |
| Safety: uncommanded current        | ⚠️ KNOWN BUG | Can false-trigger if SSR is ON at boot calibration — see §7 |
| 220V load actually powered         | ❌ UNTESTED | 9W LED incompatible (too low current for SSR triac). Use resistive load. |
| Boost SSR                          | ❌ UNTESTED | No test with flow active                   |

---

## 7. Known Bugs and Limitations

### BUG-1: Current sensor false fault on boot (safety_task.cpp)
**Symptom:** `[SAFETY] FAULT: Current detected without command` fires continuously, `sts=0x08`, SSR never turns on.  
**Root cause:** If the SSR is conducting when the slave boots (stuck triac from previous session, or any other reason), the 1-second zero-current calibration in `TaskCurrent` captures the wrong VREF. The offset bakes in ~4A phantom current that persists even after the SSR is commanded OFF.  
**Workaround:** Disconnect 220V from the SSR output, then reboot the slave.  
**Proper fix needed:** Add VREF sanity clamp in `current_task.cpp` (reject calibration if VREF deviates >10% from 2.5V nominal) AND ensure SSR pins are explicitly LOW before calibration runs.

### BUG-2: `plcConnected` hardcoded to `true` (comms_master.cpp)
**Symptom:** SAFETY_OVERRIDE state in SystemManager never fires, even if PLC communication is actually lost.  
**Root cause:** `inputs.plcConnected = true` is hardcoded in `sendCommand()`.  
**Proper fix needed:** Track last received STATUS timestamp; set `plcConnected = false` if no STATUS received in >5 seconds.

### BUG-3: SSR LEDC duty 127 = 50% carrier (pwm_task_internal.cpp, pwm_task_boost.cpp)
**Symptom:** SSR fires on only ~50% of AC half-cycles. Load gets reduced power. Small LED loads may not light at all due to SSR triac holding current minimum.  
**Note:** This is OK for the real boiler element (resistive, high current). For a test LED, use an incandescent bulb or resistive load instead.  
**Future fix option:** Change from LEDC PWM to plain `digitalWrite(HIGH/LOW)` for clean DC control during ON window. LEDC is unnecessary — the time-proportional control is already at the task level (on_ms/off_ms).

### LIMITATION: 9W LED is not a valid SSR test load
Zero-crossing SSRs require a minimum holding current (typically 50–200mA). A 9W 220V LED draws only ~40mA. The triac drops out every zero crossing. Use a 40W+ incandescent bulb or the real boiler element for testing.

---

## 8. TODO — Next Development Steps (Priority Order)

### Phase 1: Hardware Validation Checklist
Before writing more code, confirm these hardware items work:

- [ ] **HW-1** Flash both devices on v3, confirm `[S<-M]` and `[S->M]` appear in both monitors with no `DROP` lines
- [ ] **HW-2** Connect a resistive test load (≥40W incandescent bulb) to SSR_INT output. Command boiler ON. Confirm load powers on.
- [ ] **HW-3** Measure current with a clamp meter when load is ON. Confirm `pwr` reading in slave monitor is reasonable (≥40W / 220V = ≥0.18A — may still be below noise floor; that's OK for now)
- [ ] **HW-4** Test temperature reading by warming a sensor. Confirm `t1` rises in monitor.
- [ ] **HW-5** Test flow sensor by running water. Confirm `flow > 0` appears.
- [ ] **HW-6** Test overtemp safety: heat a sensor to >80°C. Confirm `[SAFETY] FAULT: Overheat` fires and SSR turns off.
- [ ] **HW-7** Test uncommanded current detection: command OFF, then manually force SSR ON with a wire. Confirm fault fires within 50ms.
- [ ] **HW-8** Add 5V→3.3V level shifter on master RX (GPIO13) from KQ-330 DOUT before final install.

### Phase 2: Software Bug Fixes
- [ ] **SW-1** Fix BUG-2: implement PLC watchdog in comms_master.cpp — set `plcConnected = false` after 5 seconds with no STATUS received
- [ ] **SW-2** Fix BUG-1: add VREF sanity clamp in `current_task.cpp` AND drive SSR pins LOW explicitly before calibration starts
- [ ] **SW-3** Fix BUG-3: replace `ledcWrite(pin, 127)` with plain `digitalWrite(HIGH)` during ON window in both PWM tasks — LEDC is not needed
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
- [ ] **RTOS-1** Move master comms (`TaskMasterComms`) to Core 0, UI (LVGL loop) stays on Core 1 — reduces contention
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

## 9. Flash Instructions

### Master (this PC, COM7)
```bash
cd c:\Users\eladmual\smart-boiler\master
git pull origin v3
pio run --target upload
pio device monitor --port COM7 --baud 115200
```

### Slave (personal PC)
```bash
cd <repo root>/slave         # e.g. C:\Users\User\Desktop\smart boiler\smart-boiler\slave
git pull origin v3
pio run --target upload
pio device monitor --baud 115200
```

---

## 10. Git Branch Notes

| Branch                    | Status   | Description                                  |
|---------------------------|----------|----------------------------------------------|
| `v3`                      | ✅ STABLE | Current working baseline — use this          |
| `feature/system-manager-v2` | archived | All v3 commits come from here               |
| `feature/smart-brain`     | archived | FreeRTOS rework — blocked, do not use        |
| `main`                    | old      | Pre-PLC version                              |

To start working: `git checkout v3` on both machines.
