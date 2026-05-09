# Smart Boiler — Complete System Documentation

**Branch:** `feature/smart-brain`  
**Hardware:** ESP32-S3 Master (5" touchscreen) + ESP32-C6 Slave (sensors & SSRs)  
**Location:** Israel (Tel Aviv, UTC+3)

---

## 1. System Architecture

### 1.1 Physical Units

| Unit | MCU | Role |
|---|---|---|
| Master | ESP32-S3 (dual-core, 240 MHz) | Touchscreen UI, decision brain, PLC master |
| Slave | ESP32-C6 (RISC-V, single-core) | Sensors, SSR control, hardware safety |

### 1.2 Communication — KQ-330 PLC Protocol
- **Physical:** Half-duplex UART 9600 baud over powerline carrier
- **Master TX → Slave RX:** `CMD` packet every 1 second (9 bytes)
- **Slave TX → Master RX:** `STATUS` packet 200 ms after receiving CMD (17 bytes)
- **Frame format:** `[0xAA][len][type][payload...][CRC-8/SMBUS][0x55]`
- **CRC:** Poly 0x07, init 0x00 (SMBUS standard)
- **Effective throughput:** ~100 bps usable data

```
Master                        Slave
  |--- CMD (9 bytes) -------->|
  |                           | (200 ms delay)
  |<-- STATUS (17 bytes) -----|
  |         (every 1 second)  |
```

---

## 2. FreeRTOS Task Architecture

### 2.1 Master Tasks (ESP32-S3, all Core 1 except Network)

| Task | Core | Priority | Period | Purpose |
|---|---|---|---|---|
| TaskBrain | 1 | 3 (highest) | 250 ms | SystemManager state machine → generates PWM commands |
| TaskMasterComms | 1 | 2 | 10 ms poll / 1 s TX | PLC send/receive, publishes SensorSnapshot |
| TaskUi | 1 | 2 | 10 ms poll / 250 ms render | LVGL touchscreen, WiFi, NTP, weather |
| TaskNetwork | 0 | 1 | 5 s | WiFi status, placeholder for future network work |
| **TaskSmartBrain** | 1 | 1 (lowest) | **60 s** | Predictive heating decision tree |

### 2.2 Slave Tasks (ESP32-C6, all Core 0)

| Task | Priority | Period | Purpose |
|---|---|---|---|
| TaskSafety | 4 (highest) | 50 ms | Software thermal cutoff, watchdog supervision |
| TaskPwmInternal | 3 | 50 ms slice | PWM window for internal tank heater |
| TaskPwmBoost | 3 | 50 ms slice | PWM window for inline boost heater |
| TaskFlow | 2 | 1 s | YF-B6 pulse counting → L/min |
| TaskTemp | 2 | 1 s | DS18B20 one-wire (3 sensors) |
| TaskCurrent | 2 | 500 ms | ACS758 RMS current → watts |
| TaskPlc | 2 | 10 ms poll | PLC receive CMD → send STATUS |

### 2.3 Shared State (Queue-based, length=1 "latest value wins")

```
TaskMasterComms → [SensorSnapshot queue] → TaskBrain
TaskUi          → [UiSnapshot queue]     → TaskBrain
TaskSmartBrain  → [UiSnapshot queue]     → TaskBrain  (auto preheat)
TaskBrain       → [CommandSnapshot queue]→ TaskMasterComms
```

---

## 3. Energy Model

### 3.1 Base + Boost Architecture

Instead of maintaining 150 L at 60°C (high standing heat loss):

- **Internal Heater (SSR Internal, GPIO4):** Maintains tank at base temperature ~40°C
- **Boost Heater (SSR Boost, GPIO5):** Fires only when flow > 0.5 L/min, bringing water to shower temperature inline

**Energy saving:** Heat only what is consumed. A 40°C tank loses far less heat to the environment than a 60°C tank.

### 3.2 SystemManager State Machine

```
Priority order (highest first):

SAFETY_OVERRIDE   ← PLC disconnected OR temp ≥ 85°C
      ↓
STATE_OFF         ← boilerOn = false (user or brain)
      ↓
STATE_SHOWER_BOOST ← flow > 0.5 L/min → boost heater ON at full PWM
      ↓
STATE_HEATING_TANK ← temp < target → internal heater ON
      ↓
STATE_STANDBY     ← temp ≥ target, no flow → all heaters OFF
```

---

## 4. Hardware Safety Architecture (Three Independent Layers)

### Layer 1 — Software (Slave FreeRTOS, priority 4)
`TaskSafety` runs every 50 ms. If water temperature exceeds 85°C it sets an emergency flag that forces both SSR PWM outputs to 0 regardless of master commands. Also enforces a 3-second command watchdog — if no fresh CMD is received from the master, both heaters shut down.

### Layer 2 — Analog Hardware (Independent of MCU)
An **LM393N comparator** circuit monitors an NTC thermistor on the tank. When water exceeds 85°C, the comparator output goes LOW and physically cuts the GND return path for all SSR drive transistors. This operates with **zero software involvement** — even a frozen or crashed MCU cannot keep the heaters on.

### Layer 3 — Flow Interlock + Watchdog Capacitors
- **BS170 MOSFET:** The boost heater circuit is physically blocked unless a real pulse train from the YF-B6 flow sensor is present. A static HIGH from a stuck MCU pin cannot open the MOSFET.
- **AC-coupled watchdog (1 µF + 22 µF capacitors):** The SSR drive signal must alternate (PWM). A continuous DC HIGH — as would occur if the MCU firmware froze with an output stuck HIGH — is blocked by these capacitors, disconnecting the SSRs automatically.

---

## 5. Sensors

| Sensor | GPIO (Slave) | Measurement | Notes |
|---|---|---|---|
| DS18B20 (×3) | GPIO7 (1-Wire) | Water temperature °C | Tank internal, boiler out, boost out |
| YF-B6 | GPIO6 | Flow rate L/min | Pulse counting, 1 s window |
| ACS758 | GPIO3 (ADC) | AC current → Watts | Auto-calibrates at boot from VREF |

### ACS758 Auto-Calibration
At boot the slave measures VREF (expected 2.5 V at 5 V supply). The measured rail was 4.03 V, giving VREF = 2.015 V and sensitivity 32.2 mV/A instead of the nominal 40 mV/A. Boot log:
```
[CURRENT] Calibrated VREF = 2.015V (expected 2.500V)
[CURRENT] VCC = 4.030V  sensitivity = 32.2 mV/A (nominal 40.0)
```

---

## 6. Smart Brain — Predictive Heating Engine

All brain logic runs locally on the ESP32-S3. No cloud. Privacy preserved.

### 6.1 SPIFFS File Map

| File | Size | Purpose |
|---|---|---|
| `/brain/events.bin` | 3588 bytes | Circular event log (512 × 7-byte records) |
| `/brain/histogram.bin` | 196 bytes | 96-slot 24-hour usage histogram |
| `/brain/heatup.bin` | 12 bytes | Last 5 heat-up durations (adaptive lead time) |
| `/brain/settings.bin` | 6 bytes | Ready-by times (weekday + weekend) |

### 6.2 Phase 1 — Event Logger (`brain/event_log.h`)

A circular ring buffer on SPIFFS. Every significant system event is timestamped (Unix time from NTP) and stored.

| Event | Code | Value |
|---|---|---|
| USER_ON | 0x01 | — |
| USER_OFF | 0x02 | — |
| FLOW_START | 0x03 | Flow rate L/min × 10 |
| FLOW_STOP | 0x04 | — |
| TEMP_SET | 0x05 | Target °C × 10 |
| AUTO_PREHEAT | 0x06 | Predicted peak minute-of-day |
| TANK_READY | 0x07 | Actual tank temp °C × 10 |
| TARGET_TIME_SET | 0x08 | Ready-by minute-of-day |
| WEATHER_ADJUST | 0x09 | Outdoor temp °C × 10 |

Record layout (7 bytes, packed):
```
[uint32_t unixTimestamp][uint8_t type][int16_t valueX10]
```

File layout:
```
[uint16_t head][uint16_t count][record 0][record 1]...[record 511]
```

### 6.3 Phase 2 — Shower Histogram (`brain/shower_histogram.h`)

96 slots covering 24 hours (one slot = 15 minutes). Every `FLOW_START` event increments the matching slot.

```
Slot index = (hour × 60 + minute) / 15    (0..95)
Slot 28 = 07:00–07:15, centre = 07:07
```

Reliability threshold: **peak slot ≥ 3 hits AND ≥ 7 different calendar days observed.**  
Until reliable, the brain defers to the user-set Ready-By time (if any) or stays idle.

### 6.4 Phase 3 — Decision Tree (`tasks/TaskSmartBrain.cpp`)

Runs every **60 seconds** on Core 1, priority 1 (never blocks control tasks).

```
Every 60 s:
  1. FAULT CHECK   — PLC disconnected or no sensor data? → skip
  2. MANUAL CHECK  — user turned ON manually? → skip (respect intent)
  3. DATA CHECK    — Ready-By set? (bypass histogram)
                     else histogram reliable? → skip if not
  4. WINDOW CHECK  — now within lead_time minutes before target?
       YES → publish boilerOn=true to UiSnapshot queue
             log AUTO_PREHEAT event
             start HeatupTracker session
  5. COOLDOWN      — auto-ON for > 2 hours? → publish boilerOn=false
```

The UiSnapshot queue (length 1) is shared with the touchscreen task. TaskBrain reads it every 250 ms and generates the actual PWM commands. The user can always press ON/OFF to override — the brain detects `boilerOn && !s_autoActive` and backs off immediately.

### 6.5 Phase 4 — Adaptive Lead Time (`brain/heatup_tracker.h`)

Tracks real heat-up duration per session:

```
AUTO_PREHEAT fires  →  startSession()  [tick saved in RAM]
Water heats up...
TANK_READY detected →  endSession()
  duration = (now - start) in minutes
  stored in ring buffer of 5 entries on SPIFFS
```

Lead time calculation:
```
lead_time = average(last 5 durations) + 5 min safety margin
```

Default before any data: **30 minutes.**

Example after 5 sessions [28, 31, 26, 29, 30]:
```
avg = 28.8 min → lead = 34 min
```

Boot log:
```
[BOOT] HeatupTracker ready: 5 session(s), lead=34 min
```

### 6.6 Phase 5 — Weather-Adjusted Lead Time

Cold inlet water requires more energy and more time to heat. The outdoor temperature (fetched from OpenWeatherMap every 10 minutes) is used to estimate inlet water temperature:

$$\text{inlet\_temp} = 15 + (\text{outdoor} - 20) \times 0.3 \text{ °C}$$

$$\text{weather\_adjustment} = (15 - \text{inlet\_temp}) \times 2 \text{ min/°C}$$

| Outdoor Temp | Inlet Estimate | Adjustment | Effect on 30 min lead |
|---|---|---|---|
| 5°C (winter) | 10.5°C | +9 min | 39 min |
| 20°C (baseline) | 15°C | 0 min | 30 min |
| 35°C (summer) | 19.5°C | -9 min | 21 min |

Solar heating benefit is captured automatically: if the sun preheated the tank, `TANK_READY` fires sooner → HeatupTracker records a shorter session → next cycle's lead time shrinks.

A `WEATHER_ADJUST` event is logged whenever a non-zero adjustment is applied.

### 6.7 Phase 6 — User "Ready By" Time (`brain/brain_settings.h`)

The user can set a fixed target time via the touchscreen. The brain then guarantees the water is ready by that time regardless of learned habits.

**UI flow:**
1. Tap **"Ready By"** button in the Shower Status card
2. A modal overlay appears with hour and minute spinners
3. Tap **Save** → persisted to `/brain/settings.bin`, logged as `TARGET_TIME_SET`
4. Tap **Cancel** → no change

**Brain logic when Ready-By is set:**
```
start_time = ready_by_time - lead_time (adaptive + weather)
```

Two independent schedules are stored:
- **Weekday** (Monday–Friday)
- **Weekend** (Saturday–Sunday)

Currently the UI saves the same time to both. Independent day control is available via `BrainSettings::setWeekdayReadyBy()` and `BrainSettings::setWeekendReadyBy()` for future UI expansion.

When Ready-By is set, the histogram reliability check (Phase 2 gate) is **bypassed** — the system works from day 1 without any learning period.

---

## 7. UI Layout (800×480, LVGL v8)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  [Clock]      [Date]      Smart Boiler      [Weather 28°]  [WiFi icon]  │  ← Top bar (48px)
├───────────────────────────┬─────────────────────────────────────────────┤
│                           │                                             │
│   ┌─── 72°C (circle) ───┐ │   ┌───────────── ON/OFF ──────────────┐   │
│   │   Water Temperature  │ │   │         [Giant button]            │   │
│   └──────────────────────┘ │   └───────────────────────────────────┘   │
│                           │                                             │
│   ┌─── Target Temp ──────┐ │   ┌───────── Shower Status ───────────┐   │
│   │  ▼ 60°  ▲           │ │   │  ✓ 2 Showers Ready!  (green)     │   │
│   └──────────────────────┘ │   │  ready in ~0 min                  │   │
│                           │   │  ─────────────────────────────     │   │
│   [Heating LED]           │   │  [🔔 Ready By]  button            │   │
│                           │   └───────────────────────────────────┘   │
├───────────────────────────┴─────────────────────────────────────────────┤
│  ⚡ Power: 2000 W    ▶ Flow: 0.0 L/min    ✓ Connected                   │  ← Info bar (42px)
└─────────────────────────────────────────────────────────────────────────┘
```

**Shower card colour states:**
- White border, orange "Heating Up..." → water not ready
- Green background + border, "✓ N Showers Ready!" → water at temperature

---

## 8. Boot Sequence & Serial Log

```
--- MASTER UNIT STARTED: FreeRTOS architecture shell ---
[BOOT] EventLog ready: 47 events in log
[BOOT] Histogram ready: 14 sessions over 9 days
[BOOT] HeatupTracker ready: 5 session(s), lead=34 min
[BOOT] Ready-by today: 07:30
[BOOT] Created task: Brain
[BOOT] Created task: MasterComms
[BOOT] Created task: UI
[BOOT] Created task: Network
[BOOT] Created task: SmartBrain
[BRAIN] Task started
[COMMS] Task started
[SMART] Task started
```

---

## 9. Testing Strategy

### 9.1 Master — SystemManager Test Bench
Enable with `#define TEST_MODE 1` in `main.cpp`.  
11 pre-defined scenarios inject fake sensor data and verify the correct PWM commands and state.  
**Status: 11/11 PASS** ✅

### 9.2 Slave — PLC Test Sender
`slave/src/plc_test_sender.cpp` cycles through 9 scenarios (10 s each) simulating all operating conditions. Verified end-to-end: master correctly transitions between all SystemManager states.  
**Status: 9/9 scenarios verified** ✅

### 9.3 Hardware Bench Tests (Pending)
| Test | Method | Status |
|---|---|---|
| SSR Internal fires | Connect LED to SSR output, trigger STATE_HEATING_TANK | ⏳ |
| SSR Boost fires | Connect LED to boost SSR, trigger flow scenario | ⏳ |
| Thermal cutoff 85°C | Disconnect one NTC wire → LM393N should cut power | ⏳ |
| Flow interlock | Remove flow sensor pulse → boost SSR must not fire | ⏳ |
| Watchdog cap | Hold GPIO HIGH with no PWM → SSR must disconnect | ⏳ |

---

## 10. Git Branch Strategy

| Branch | Purpose |
|---|---|
| `main_master_slave` | Production — stable, hardware-verified code |
| `feature/smart-brain` | Smart brain Phases 1–6 (this branch) |
| `feature/system-manager-v2` | Future SystemManager improvements |
| `freertos-refactor` | Preserved — FreeRTOS architecture (merged into main) |

**Merge plan:** After hardware bench tests pass on `main_master_slave`, merge `feature/smart-brain` → `main_master_slave` via pull request.

---

## 11. SPIFFS Storage Summary

| File | Size | Survives reboot | Purpose |
|---|---|---|---|
| `/brain/events.bin` | 3588 B | ✅ | Full event history |
| `/brain/histogram.bin` | 196 B | ✅ | Shower time pattern |
| `/brain/heatup.bin` | 12 B | ✅ | Heat-up duration history |
| `/brain/settings.bin` | 6 B | ✅ | Ready-by times |
| `/wifi.json` | ~200 B | ✅ | WiFi credentials |
| `/settings.json` | ~500 B | ✅ | General settings |
| **Total brain** | **~4 KB** | | Well within 4 MB SPIFFS |

---

## 12. Component List

| Component | Part | Role |
|---|---|---|
| Master MCU | ESP32-S3 (Viewe UEDX80480050E-WB-A) | Brain, UI, PLC master |
| Slave MCU | ESP32-C6 DevKit | Sensors, SSR control |
| Internal heater SSR | Solid State Relay | Tank base heating |
| Boost heater SSR | Solid State Relay | Inline shower boost |
| Temperature sensors | DS18B20 (×3) | Tank + pipe temps |
| Flow sensor | YF-B6 | Water flow rate |
| Current sensor | ACS758 | Power measurement |
| Thermal comparator | LM393N | Hardware 85°C cutoff |
| Flow interlock | BS170 MOSFET | Dry-fire protection |
| Watchdog caps | 1 µF + 22 µF | Stuck-HIGH protection |
| PLC modem | KQ-330 (×2) | Powerline communication |
