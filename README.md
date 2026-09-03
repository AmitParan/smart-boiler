# Smart Electric Boiler — דוד חשמלי חכם

**An autonomous retrofit energy-management system for domestic electric water heaters.**

Final engineering project · Afeka College of Engineering, Tel Aviv
Students: **Elad Moalem** (אלעד מועלם) · **Amit Paran** (עמית פארן)
Supervisor: **Dmitry Teif** (דמיטרי טייף)

---

## Table of contents

1. [The problem](#1-the-problem)
2. [The solution — two-tier hybrid heating](#2-the-solution--two-tier-hybrid-heating)
3. [System architecture](#3-system-architecture)
4. [Hardware](#4-hardware)
5. [Safety architecture](#5-safety-architecture)
6. [Communication](#6-communication)
7. [Firmware architecture (FreeRTOS)](#7-firmware-architecture-freertos)
8. [The control brain](#8-the-control-brain)
9. [Repository layout](#9-repository-layout)
10. [Build and flash](#10-build-and-flash)
11. [Demo mode — 8 automated scenarios](#11-demo-mode--8-automated-scenarios)
12. [Results](#12-results)
13. [Documentation index](#13-documentation-index)

---

## 1. The problem

A conventional Israeli electric boiler ("דוד חשמלי") is a **blind, open-loop
machine**. The user flips a switch, waits, and gets no feedback about water
temperature or how much hot water is actually available. Three physical
inefficiencies follow from that:

| Problem | Consequence |
|---|---|
| **Open-loop operation** | The user over-heats "just in case", or under-heats and gets a cold shower |
| **Standing thermal losses** | A 80–150 L tank held at high temperature bleeds heat to the room continuously, 24 h a day |
| **Whole-tank heating** | Washing your hands heats 150 L of water to serve 2 L of demand |

The obvious fix — a **tankless instant heater** — is not available to most Israeli
homes. Those units demand **14 kW–24 kW** and three-phase service. A standard
apartment is limited to **single-phase, 16 A ≈ 3 kW**.

## 2. The solution — two-tier hybrid heating

This project fits **within the 3 kW household ceiling** by splitting the job
across two coordinated heating tiers instead of one big element.

```
   ┌──────────────────────────────────────────────────────────────┐
   │  TIER 1 — Moderate Storage                                   │
   │  Hold the main tank at only 35–40 °C, not 60–70 °C.          │
   │  A cooler tank loses far less heat to the room, so standing   │
   │  losses collapse. The tank is a warm buffer, not a kettle.    │
   └──────────────────────────────────────────────────────────────┘
                                │  water leaves the tank at ~35-40 °C
                                ▼
   ┌──────────────────────────────────────────────────────────────┐
   │  TIER 2 — Flow-Triggered Top-Up                              │
   │  A 3000 W inline heater in series with the outlet pipe adds   │
   │  a focused +3-7 °C ONLY while water is actually flowing,      │
   │  and only under hardware interlock. Energy is spent on the    │
   │  water you are using, at the moment you use it.               │
   └──────────────────────────────────────────────────────────────┘
```

The tank never has to be hot — it only has to be *warm enough* that a 3 kW
inline booster can close the remaining gap in real time. That is what keeps the
system inside a single-phase 16 A supply while still delivering a hot shower.

On top of the thermodynamics sits an **adaptive learning layer** that records
when the household actually showers and pre-heats ahead of predicted demand,
so the tank is ready without being held hot all day.

## 3. System architecture

Two microcontrollers, deliberately separated by role:

```
┌───────────────────────────────┐              ┌───────────────────────────────┐
│  MASTER — indoor command unit │              │  SLAVE — boiler-side unit     │
│  ESP32-S3                     │              │  ESP32-C6                     │
│  Viewe UEDX80480050E-WB-A     │◄────────────►│  custom PCB                   │
│  800x480 capacitive touch     │   1 packet   │                               │
│                               │   per second │                               │
│  · LVGL v8 touchscreen HMI    │              │  · 3x DS18B20 temperature     │
│  · SystemManager state machine│              │  · YF-B6 flow sensor          │
│  · Smart preheat learning     │              │  · ACS758 current sensing     │
│  · WiFi / NTP / weather       │              │  · 2x SSR drive (burst PWM)   │
│  · 8-scenario demo bench      │              │  · analog hardware interlock  │
└───────────────────────────────┘              └───────────────────────────────┘
        the user's world                              the plumbing's world
```

**Why two units?** The boiler is usually in a service balcony or on the roof —
behind reinforced concrete, often inside or near a MAMAD (safe room). Putting
the touchscreen there would be useless, and relying on WiFi reaching there was
judged unsafe for a control link. The original design therefore carried the
control link over **the building's own 220 VAC wiring** using Power Line
Communication, which passes through concrete that WiFi cannot.

> **Engineering note — the transport changed.** The KQ-330 power-line modems
> ultimately failed in hardware (see [section 6](#6-communication)). The firmware
> was restructured so the link runs over **either** PLC **or** WiFi, chosen by a
> single build flag, carrying byte-identical packets. Everything above the
> transport was unaffected.

## 4. Hardware

### 4.1 Slave GPIO map — ESP32-C6 *(book Table 7)*

| GPIO | Function | Direction | Component |
|:---:|---|---|---|
| **4** | `SSR_Internal` | Output (PWM) | Tank heater SSR |
| **5** | `SSR_Boost` | Output (PWM) | Inline booster SSR |
| **7** | `One_Wire_Bus` | In/Out | 3 × DS18B20 on one bus |
| **6** | `Flow_Sensor` | Input (interrupt) | YF-B6 Hall-effect flow meter |
| **10** | `PLC_RX` | Input (UART) | KQ-330 DOUT, via level shifter U3 |
| **11** | `PLC_TX` | Output (UART) | KQ-330 DIN |
| **3** | `Current_ADC` | Input (analog) | ACS758LCB-050B |

Defined in [`slave/src/config/config.h`](slave/src/config/config.h).

### 4.2 Master GPIO map — ESP32-S3 *(book Table 9)*

| GPIO | Function | Direction | Component |
|:---:|---|---|---|
| **17** | `MASTER_TX_PIN` | Output | KQ-330 DIN |
| **13** | `MASTER_RX_PIN` | Input | KQ-330 DOUT |
| **43 / 44** | `UART0_TX/RX` | — | USB CDC debug console |
| — | Touch + RGB | — | Managed by the board package (800×480) |

Defined in [`master/src/config/config.h`](master/src/config/config.h).

### 4.3 Key components

| Component | Part | Role |
|---|---|---|
| Master MCU | ESP32-S3 (Viewe UEDX80480050E-WB-A) | HMI, decisions, connectivity |
| Slave MCU | ESP32-C6-DevKitC-1 | Sensing, switching, safety |
| Power-line modem | KQ-330 | 9600 bps UART over 220 VAC |
| Solid-state relays | SSR-40DA (zero-crossing) | Switch both heating elements |
| Temperature | 3 × DS18B20 (1-Wire) | Tank, boiler outlet, boost outlet |
| Flow | YF-B6 Hall-effect | `F = 6.6 × Q` (Hz per L/min) |
| Current | ACS758LCB-050B | 40 mV/A, power calculation + fault detection |
| Comparator | LM393N + NTC | Autonomous 85 °C thermal cutoff |
| Interlock switching | 2N2222A BJT, BS170 MOSFET | Watchdog and flow gates |
| Level shifter | SN74LV1T34 | 5 V ↔ 3.3 V on the modem link |
| PSU | HLK-10M05 | 220 VAC → 5 V, isolated |

Full schematic, Gerbers, drill files and BOM live in [`pcb/`](pcb/).
Datasheets for every part are in [`datasheets/`](datasheets/).

## 5. Safety architecture

Safety is **dual-layer**: fast software checks, plus an **analog hardware
envelope that works even if the microcontroller is dead**. This is the heart of
the project and the reason the design is defensible.

### 5.1 The four hardware gates *(book §9.1.4)*

| Gate | Built from | What it physically prevents |
|---|---|---|
| **a. Internal Watchdog Gate** | 2N2222A BJT + DC-blocking capacitor | Controller freeze. The SSR latches **only** while it receives a continuous 1 kHz pulse train. A frozen MCU leaves a constant DC level, the capacitor blocks it, the base charge bleeds away and the heater is cut within ≈1 s. |
| **b. External Boost Gate** | Watchdog + flow interlock + PWM | Triple-gated. The booster cannot energise unless the controller is alive **and** water is genuinely flowing. |
| **c. Flow Interface** | BS170 MOSFET, 15–60 Hz | Dry-fire protection. When flow pulses stop, the RC network discharges and cuts the booster within 1.0 s. |
| **d. Analog Comparator + Thermal Cutoff** | LM393N + NTC, V<sub>ref</sub> ≈ 0.63 V | Boiling. At ≈85 °C the comparator output flips and disconnects SSR power directly — **no firmware involved at all**. |

> **This is why `ledcWrite(pin, 127)` at 1 kHz is not optional.**
> It looks like a PWM power setting; it is actually the **carrier that proves the
> controller is alive** to gate (a). Replacing it with `digitalWrite(HIGH)` would
> be read by the hardware as a controller-freeze fault and the heater could never
> stay on. See [`slave/src/tasks/pwm_task_internal.cpp`](slave/src/tasks/pwm_task_internal.cpp).

### 5.2 Hardware truth table *(book Table 11)*

`1` = live PWM pulse train · `DC/0` = logic low / fault / blocked · `X` = don't care

| NTC safety | CMD internal | CMD boost | Flow | SSR internal | SSR boost | Meaning |
|:---:|:---:|:---:|:---:|:---:|:---:|---|
| 0 (overheat) | X | X | X | **0** | **0** | NTC disconnects everything — system-wide safe state |
| 1 (OK) | DC / 0 | DC / 0 | X | 0 | 0 | Idle — no commands from the controller |
| 1 (OK) | 1 | DC / 0 | X | **1** | 0 | Base tank heating, independent of flow |
| 1 (OK) | DC / 0 | 1 | 1 | 0 | **1** | Booster only — requires flow |
| 1 (OK) | X | X | **0** | X | **0** | Flow interlock forces the booster off |
| 1 (OK) | 1 | 1 | 1 | **1** | **1** | Both tiers active under hardware supervision |

### 5.3 Software safety — `TaskSafety`, every 50 ms

[`slave/src/tasks/safety_task.cpp`](slave/src/tasks/safety_task.cpp) runs at the
**highest FreeRTOS priority (4)** and independently verifies:

1. Every temperature sensor is below **80 °C** (software cutoff, below the 85 °C hardware trip)
2. The booster is never commanded while flow **< 1.0 L/min** (dry-fire)
3. No **uncommanded current** is flowing — catches a welded/shorted SSR
4. A **CMD packet arrived within the last 5 s** — otherwise the link is presumed lost

Any failure latches `system_fault`, and both SSRs are cut immediately.

## 6. Communication

### 6.1 Binary packet protocol

Frame layout, identical on both sides
([`*/src/config/boiler_protocol.h`](slave/src/config/boiler_protocol.h)):

```
[ 0xAA | LEN | TYPE | SEQ | ...payload... | CRC8 | 0x55 ]
  start                                            end
         └────── CRC-8/SMBUS (poly 0x07) ──────┘
```

| Packet | Direction | Size | Contents |
|---|---|:---:|---|
| **CMD** | Master → Slave | 9 B | PWM internal %, PWM boost %, flag bits |
| **STATUS** | Slave → Master | 17 B | 3 × temperature, flow, power, status bits |

Temperatures are sent as `int16 × 10` (0.1 °C resolution), flow as `uint16 × 10`.
Cadence is **1 packet per second**, with a rolling sequence number so either side
detects a dropped packet.

### 6.2 The transport layer — PLC *or* WiFi

The link medium is abstracted behind [`link.h`](slave/src/comms/link.h), so the
**exact same bytes** travel over either medium:

```
       sendCommand() / PLC_SendStatus()          ← protocol, unchanged
                     │
          link_send() / link_poll()              ← the abstraction
                     │
           ┌─────────┴─────────┐
     link_plc.cpp        link_wifi.cpp
   KQ-330 over UART        UDP over WiFi
```

| | `link_plc.cpp` | `link_wifi.cpp` |
|---|---|---|
| Framing | Byte-by-byte receive state machine | A datagram **is** a frame |
| TX timing | 2 ms inter-byte gap (modem requirement) | none |
| Half-duplex guard | 200 ms after RX before TX | none — UDP is full duplex |
| Discovery | n/a | Automatic; no IP is configured anywhere |

Selected by one build flag in `platformio.ini` — **the same on both sides**:

```ini
build_flags = -DLINK_WIFI     ; WiFi (UDP)
;             (flag absent)   ; KQ-330 power-line modem
```

### 6.3 Honest status of the PLC link

The PLC link worked during earlier development but **failed in hardware** during
final integration. Diagnosis, in order:

1. Raw-UART PING/PONG sketches on both boards (no project code at all) showed the
   master transmitting cleanly and the slave receiving **zero bytes**.
2. An earlier session had seen clean data in and `0xFF` garbage out — the modem
   link corrupting data.
3. Root cause was never confirmed at component level. Prime suspects: the KQ-330
   `MODE`/`RST` pins appearing to float, the J2 signal ribbon seating, and level
   shifter U3's supply rail.

Rather than block the project on a modem fault, the transport was abstracted and
a WiFi backend added. **The PLC code remains complete and compiles** — it is a
documented design that failed in hardware, not abandoned work. Full diagnostic
procedure: [`TROUBLESHOOTING_PLC.md`](TROUBLESHOOTING_PLC.md).

## 7. Firmware architecture (FreeRTOS)

### 7.1 Slave task hierarchy *(book Table 19)*

| Priority | Task | Period | Responsibility |
|:---:|---|---|---|
| **4** (highest) | `TaskSafety` | 50 ms | Closed-loop safety, hard SSR cutoff |
| **3** | `TaskPWM_Internal`, `TaskPWM_Boost` | 2000 ms window | Burst-mode SSR power control |
| **2** | `TaskTemp`, `TaskFlow`, `TaskCurrent`, `TaskPLC` | 100–500 ms | Sensing and the comms link |
| **1** (lowest) | `TaskSerial` | loop | Debug console, `d`/`r` mode switching |

### 7.2 Memory guards — the four mutexes *(book Table 20)*

The slave is fully concurrent, so every shared variable is protected:

| Mutex | Written by | Read by |
|---|---|---|
| `guard_temps` | `TaskTemp` | `TaskPLC`, `TaskSafety`, `TaskPWM_Boost` |
| `guard_flow` | `TaskFlow` | `TaskPLC`, `TaskSafety`, `TaskPWM_Boost` |
| `guard_current` | `TaskCurrent` | `TaskPLC`, `TaskSafety` |
| `guard_cmd` | `TaskPLC` | `TaskPWM_Internal`, `TaskPWM_Boost`, `TaskSafety` |

Created in [`slave/src/main.cpp`](slave/src/main.cpp) **before any task starts**.

### 7.3 Master — dual-core split *(book Table 17)*

The ESP32-S3's two cores are separated deliberately so that a heavy LVGL redraw
can never delay a control packet:

| Core | Work |
|---|---|
| **Core 0** | `TaskMasterComms` — 1 s CMD cadence, STATUS reception, CRC-8 |
| **Core 1** | LVGL GUI, touch, WiFi/NTP, weather, automated test bench |

### 7.4 Burst-mode power control

The booster must deliver a *specific* power, not simply on/off. Required power
comes from the thermodynamic relation:

```
P = ṁ · Cp · ΔT
```

For 5 L/min (ṁ = 0.0833 kg/s) and ΔT = 6 °C:
`P = 0.0833 × 4184 × 6 ≈ 2090 W` — about **70 %** of the 3000 W element.

The controller therefore runs a **2000 ms time-proportional window**: 1400 ms ON,
600 ms OFF. Because the SSR is zero-crossing, a 2 s window contains 200 mains
half-cycles at 50 Hz, so 70 % resolves to 140 half-cycles — fine-grained control
with no switching noise injected onto the power line.

```
Power envelope   ████████████████████░░░░░░░░    1400 ms ON / 600 ms OFF
1 kHz carrier    ▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌            watchdog proof-of-life
Mains output     ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿            switched at zero crossings
```

## 8. The control brain

### 8.1 SystemManager — finite state machine

[`master/src/control/SystemManager.cpp`](master/src/control/SystemManager.cpp)
evaluates in strict priority order:

| Priority | State | Trigger | Output |
|:---:|---|---|---|
| 1 | `SAFETY_OVERRIDE` | Link lost **or** T ≥ 85 °C | Both PWM = 0 |
| 2 | `STATE_OFF` | User switched off | Both PWM = 0 |
| 3 | `STATE_SHOWER_BOOST` | Flow detected **and** tank < 45 °C | Booster on |
| 4 | `STATE_HEATING_TANK` | No flow **and** tank < 40 °C | Tank heater on |
| 5 | `STATE_STANDBY` | Temperature satisfied | Idle |

### 8.2 Smart preheat — the learning layer

[`master/src/control/smart_preheat.cpp`](master/src/control/smart_preheat.cpp)
records the time of each real shower (detected as flow crossing the threshold
from zero) and predicts when the household will next want hot water, starting
the tank early enough to be ready. It **never** overrides a manual ON, **never**
touches safety, and stands down entirely if the link to the slave is lost.

Three user-selectable operating modes:

| Mode | Behaviour |
|---|---|
| **Manual** | Reactive only, no pre-heating |
| **Ready-by** | User sets target times; the system computes when to start |
| **Smart** | Learned usage patterns drive pre-heating automatically |

## 9. Repository layout

Both firmwares use the same scheme, so understanding one gets you the other.

```
master/src/                              slave/src/
├── main.cpp      boot, WiFi/NTP,        ├── main.cpp      boot, mutexes,
│                 LVGL render loop       │                 task creation
├── config/       pins, wire protocol,   ├── config/       pins, wire protocol,
│                 LVGL + display config  │                 ports, WiFi secrets
├── comms/        transport + task       ├── comms/        transport + protocol
├── control/      SystemManager, smart   ├── core/         shared state, mutexes,
│                 preheat, app mode      │                 DEMO/REALTIME mode
├── data/         statistics, network    ├── tasks/        6 FreeRTOS tasks
├── ui/           LVGL screens           └── diagnostics/  test sketches
├── demo/         8-scenario bench
└── diagnostics/  test sketches
```

### Where do I look for…?

| Question | File |
|---|---|
| What decides when the heater turns on? | `master/src/control/SystemManager.cpp` |
| What do master and slave send each other? | `*/src/config/boiler_protocol.h` |
| How do the bytes travel? | `*/src/comms/link.h`, `link_plc.cpp`, `link_wifi.cpp` |
| How is an SSR actually switched? | `slave/src/tasks/pwm_task_internal.cpp` |
| What protects against overheating? | `slave/src/tasks/safety_task.cpp` |
| Where are the demo scenarios? | `master/src/demo/demo_scenarios.cpp` |
| Which GPIO is what? | `*/src/config/config.h` |
| How does the learning work? | `master/src/control/smart_preheat.cpp` |

> **Note on `#include`:** every include stays short (`#include "config.h"`)
> because each `platformio.ini` adds the `src/` subfolders to the include path.
> No source file needed editing when the folders were introduced.

## 10. Build and flash

Requires [PlatformIO](https://platformio.org/). Both projects are independent.

```bash
# Slave (ESP32-C6)
cd slave
pio run --target upload
pio device monitor
```

```bash
# Master (ESP32-S3)
cd master
pio run --target upload
pio device monitor
```

Set `upload_port` / `monitor_port` in each `platformio.ini` to your actual COM
ports (`pio device list` will show them).

### WiFi credentials (slave)

The slave has no touchscreen, so its credentials are compiled in:

```bash
cd slave/src/config
cp wifi_secrets.example.h wifi_secrets.h    # then edit with your SSID/password
```

`wifi_secrets.h` is **gitignored** and never reaches GitHub. ESP32 radios are
**2.4 GHz only** — a 5 GHz-only network will not connect.

### Windows build note

`slave/platformio.ini` sets `core_dir = C:/pio`. This is **required on Windows**:
RISC-V gcc forwards all 278 framework include paths to the assembler, and with
`<WiFi.h>` included the command line reaches 32,870 characters against Windows'
32,767 limit — reported misleadingly as `cannot execute as.exe`. Create the short
path once (no admin needed):

```bash
mklink /J C:\pio %USERPROFILE%\.platformio
```

## 11. Demo mode — 8 automated scenarios

The master runs a complete automated test bench
([`master/src/demo/demo_scenarios.cpp`](master/src/demo/demo_scenarios.cpp)) that
injects scripted sensor values, so the **entire control and safety chain can be
demonstrated without plumbing or 220 V loads**. The slave still fires its real
SSRs, so the behaviour is genuine end-to-end.

| # | Scenario | Demonstrates |
|:---:|---|---|
| 1 | Pre-Heating (tank only) | `STATE_HEATING_TANK`, internal SSR, 2500 W |
| 2 | Preheated Shower | `STATE_SHOWER_BOOST`, booster SSR, 3000 W |
| 3 | Warm Shower Boost Cutoff | Both SSRs off once 45 °C is reached |
| 4 | Standby / Redundant Request | Correct idle, 0 W |
| 5 | Predictive Solar Bypass | System yields to solar gain, stays off through a 28→42 °C sweep |
| 6 | Communication Loss | Link cut → slave 5 s watchdog → hard cutoff |
| 7 | Overtemperature | Software cutoff at 80 °C, hardware interlock simulated at 85 °C |
| 8 | Stuck SSR | Uncommanded current detected within 50 ms |

Switch modes from the Settings screen, or on the slave console: `d` = DEMO,
`r` = REALTIME, `?` = status.

## 12. Results

- **30–50 % reduction** in daily electricity consumption versus a conventional
  boiler, while fully preserving user comfort
- Validated on a laboratory test bench **and** through MATLAB/Simulink
  system-level simulation across all operating scenarios and edge cases
- Communication robustness and immediate safety response demonstrated
- Operates entirely within a standard **single-phase 16 A / 3 kW** household supply

The complete energy analysis, simulation models and scenario plots are in
[`Matlab/`](Matlab/).

## 13. Documentation index

| Path | Contents |
|---|---|
| [`README_v3.md`](README_v3.md) | Detailed development documentation, bug log, TODO |
| [`SYSTEM_DOCUMENTATION.md`](SYSTEM_DOCUMENTATION.md) | System-level reference |
| [`docs/plc-protocol.md`](docs/plc-protocol.md) | Packet protocol specification |
| [`docs/smart-brain.md`](docs/smart-brain.md) | Learning algorithm design |
| [`docs/smart-preheat.md`](docs/smart-preheat.md) | Pre-heat decision logic |
| [`TROUBLESHOOTING_PLC.md`](TROUBLESHOOTING_PLC.md) | Ordered KQ-330 fault-finding procedure |
| [`pcb/`](pcb/) | Schematic, board layout, Gerbers, drill files, BOM |
| [`datasheets/`](datasheets/) | Datasheets for every component used |
| [`Matlab/`](Matlab/) | Simulink model, 8 scenario scripts, energy analysis |
| [`tools/`](tools/) | `serial_logger.py`, `udp_link_probe.py` |

---

<sub>Afeka College of Engineering, Tel Aviv · Final Engineering Project</sub>
