# Guided Code Tour — for reviewers

This document is a short, guided path through the parts of the codebase that
carry the real engineering: **safety**, **concurrency**, **the wire protocol**,
and **the transport abstraction**.

It is deliberately **not** a code dump. Each section states a claim the project
makes, then shows the few lines that prove it, then links to the full file.
Every snippet is under 25 lines and can be read in one screen.

**How to read this:** sections 1–4 are the evidence. Section 5 tells you how to
**reproduce every claim yourself** on the hardware in a few minutes.

> Line-number links point at commit `d2c5f93`, so they stay accurate permanently.

---

## 1. Safety — the part that works when the software doesn't

The system drives two mains-voltage heating elements. The design assumption is
that **the microcontroller will eventually fail**, so safety cannot depend on it.

### 1.1 The 1 kHz carrier is a hardware watchdog, not a power setting

This is the single most important — and most misreadable — line in the codebase.

[`slave/src/tasks/pwm_task_internal.cpp:8-18`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/pwm_task_internal.cpp#L8-L18)

```cpp
//  HARDWARE-INTERLOCK REQUIREMENT  (Project Book: "Internal Watchdog Gate")
//  The SSR gate is DC-blocking (series input capacitors). It latches ONLY
//  while it receives a continuous high-frequency pulse train ("AC" proof of
//  a live controller). A constant DC level is read as a controller-freeze
//  fault and the gate physically cuts the heater within ~1 s (RC, tau ~= 1 s).
//    => ledcWrite(pin, 127) @ 1 kHz supplies that MANDATORY carrier.
//    => DO NOT replace with digitalWrite(HIGH) (README "SW-3"): constant DC
//       trips the hardware watchdog and the heater can never sustain ON.
ledcAttach(PIN_SSR_INT, 1000, 8);
```

`ledcWrite(pin, 127)` looks like "50 % power". It is not. It is the
**proof-of-life carrier** that keeps the analog watchdog gate (2N2222A + series
capacitor) energised. If the firmware crashes and leaves the pin at a constant
level, the capacitor blocks it, the base charge bleeds away through R3, and the
heater is physically disconnected in about one second — **with no code running**.

Actual power control is done separately, by the 2000 ms burst window below the
carrier. See project book §9.1.4(a) and Figure 32.

### 1.2 Fault state is checked before anything else

[`slave/src/tasks/pwm_task_internal.cpp:23-28`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/pwm_task_internal.cpp#L23-L28)

```cpp
if (system_fault) {
    ledcWrite(PIN_SSR_INT, 0);
    internal_ssr_on = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    continue;
}
```

The fault check is the **first** statement in the control loop, before any
command is read or any mutex is taken. A latched fault cannot be starved,
delayed or out-voted by the control path. The identical block opens
`TaskPWM_Boost`.

### 1.3 Detecting a *welded* SSR — current with no command

The least obvious of the four safety checks, and the one that shows failure-mode
thinking rather than limit-checking.

[`slave/src/tasks/safety_task.cpp:81-84`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/safety_task.cpp#L81-L84)

```cpp
bool any_commanded    = (local_pwm_int > 0u) || (local_pwm_bst > 0u);
bool do_current_check = (currentMode == MODE_DEMO) || current_sensor_valid;
if (do_current_check && !any_commanded && local_current > 0.5f) {
    fault = true;
```

A solid-state relay's characteristic failure is to fail **closed** — welded on,
with the element live and the controller convinced it is off. Comparing measured
current against commanded state catches exactly that, within one 50 ms cycle.

Note `current_sensor_valid`: the check is **skipped in REALTIME if the ACS758
failed its boot calibration**, because an uncalibrated ADC would produce phantom
faults. Refusing to act on data known to be untrustworthy is deliberate.

### 1.4 The link watchdog, and why `cmd_ever_received` exists

[`slave/src/tasks/safety_task.cpp:96`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/safety_task.cpp#L96)

```cpp
if (cmd_ever_received && (millis() - last_cmd_received_ms > PLC_TIMEOUT_MS)) {
```

If the master goes silent for 5 s, the slave assumes the link is lost and cuts
both elements. The `cmd_ever_received` guard prevents a **false trip at boot**,
when `last_cmd_received_ms` is still zero and no CMD could possibly have arrived
yet — the slave may take up to 20 s to associate with WiFi.

### 1.5 Design → implementation traceability

The four software checks in `TaskSafety` implement the **Logic Truth Table
(book Table 10)**; the analog gates implement the **Hardware Truth Table
(book Table 11)**. The mapping is one-to-one:

| Book Table 11 row | Enforced by |
|---|---|
| NTC overheat → everything off | LM393N comparator (hardware only, no firmware) |
| Boost requires flow | `safety_task.cpp:66-74` **and** the BS170 flow gate |
| Controller freeze → heater off | 1 kHz carrier + DC-blocking capacitor (§1.1) |
| Uncommanded current → fault | `safety_task.cpp:81-93` |

Two of these rows are enforced **twice** — once in software, once in analog
hardware. That redundancy is the point.

**Full file:** [`slave/src/tasks/safety_task.cpp`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/safety_task.cpp) — priority 4, runs every 50 ms.

---

## 2. Concurrency — four mutexes, and who may touch what

The slave runs seven FreeRTOS tasks that share sensor and command state. Every
shared variable is protected by exactly one mutex, and the ownership rules are
documented in the header rather than left implicit.

### 2.1 The guards are declared with their scope stated

[`slave/src/core/shared_data.h:61-65`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/core/shared_data.h#L61-L65)

```cpp
extern SemaphoreHandle_t guard_temps;    ///< guards temps[3]
extern SemaphoreHandle_t guard_flow;     ///< guards current_flow
extern SemaphoreHandle_t guard_current;  ///< guards current_rms, power_watts
extern SemaphoreHandle_t guard_cmd;      ///< guards cmd_pwm_internal/boost/flags
extern portMUX_TYPE      timerMux;       ///< ISR-safe spinlock for flow pulse counter
```

`timerMux` is a **spinlock, not a mutex** — it protects the flow pulse counter
inside an interrupt service routine, where blocking on a semaphore is forbidden.
Using the right primitive for ISR context is a deliberate distinction.

### 2.2 Ownership map — implements book Table 20

| Mutex | Written by | Read by |
|---|---|---|
| `guard_temps` | `TaskTemp` | `TaskPLC`, `TaskSafety`, `TaskPWM_Boost` |
| `guard_flow` | `TaskFlow` | `TaskPLC`, `TaskSafety`, `TaskPWM_Boost` |
| `guard_current` | `TaskCurrent` | `TaskPLC`, `TaskSafety` |
| `guard_cmd` | `TaskPLC` | `TaskPWM_Internal`, `TaskPWM_Boost`, `TaskSafety` |

Each guard has exactly **one writer** and several readers. Single-writer
ownership means no write-write race is structurally possible.

### 2.3 Created before any task can run

[`slave/src/main.cpp:24-33`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/main.cpp#L24-L33)

```cpp
guard_temps   = xSemaphoreCreateMutex();
guard_flow    = xSemaphoreCreateMutex();
guard_current = xSemaphoreCreateMutex();
guard_cmd     = xSemaphoreCreateMutex();

if (!guard_temps || !guard_flow || !guard_current || !guard_cmd) {
    Serial.println("[FATAL] Failed to create mutexes - halting.");
    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

All four are created **before `xTaskCreatePinnedToCore` is called even once**, so
no task can ever observe a null handle. Allocation failure **halts the system**
rather than continuing unprotected — a boiler controller with broken mutual
exclusion must not run.

### 2.4 The access pattern: snapshot, release, then decide

[`slave/src/tasks/safety_task.cpp:28-37`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/tasks/safety_task.cpp#L28-L37)

```cpp
if (xSemaphoreTake(guard_temps,   pdMS_TO_TICKS(5)) == pdTRUE) {
    local_temps[0] = temps[0]; local_temps[1] = temps[1]; local_temps[2] = temps[2];
    xSemaphoreGive(guard_temps);
}
if (xSemaphoreTake(guard_flow,    pdMS_TO_TICKS(5)) == pdTRUE) {
    local_flow = current_flow; xSemaphoreGive(guard_flow);
}
```

Three properties worth noticing:

1. **Bounded wait.** Every take uses a 5 ms timeout, never `portMAX_DELAY`. The
   highest-priority safety task can never be blocked indefinitely by a lower one.
2. **Copy, then release.** Each mutex is held only for the duration of the copy —
   the safety logic runs afterwards on local copies, so it holds no lock while
   deciding.
3. **Never more than one guard is held at a time.** Verified across every task
   that touches a mutex (`plc_comms`, `safety_task`, `temp_task`, `flow_task`,
   `current_task`, both PWM tasks): each `xSemaphoreTake` is matched by its
   `xSemaphoreGive` before any other take — there is no nesting anywhere in the
   codebase. Without hold-and-wait, a deadlock cycle cannot form.

---

## 3. The wire protocol

### 3.1 A self-documenting frame

[`slave/src/config/boiler_protocol.h:15-21`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/config/boiler_protocol.h#L15-L21)

```
//  Wire frame layout (little-endian multi-byte fields):
//  [ 0xAA | LEN | TYPE | SEQ | ...payload... | CRC8 | 0x55 ]
//    ^                   |<--- LEN bytes --->|
//    start                                         end
//
//  CRC-8/SMBUS (poly 0x07) covers bytes [TYPE .. last payload byte].
```

This header is **byte-identical on master and slave** — a single shared
definition of truth for the link.

### 3.2 `#pragma pack(1)` — struct layout *is* the wire format

[`slave/src/config/boiler_protocol.h:58-66`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/config/boiler_protocol.h#L58-L66)

```cpp
//  #pragma pack(1) eliminates compiler padding → sizeof == wire size
#pragma pack(1)

struct BoilerStatusPacket_t {
    uint8_t  startByte;       // PROTO_START         = 0xAA
    uint8_t  length;          // STATUS_PAYLOAD_LEN  = 13
    uint8_t  packetType;      // PROTO_TYPE_STATUS   = 0x01
    uint8_t  sequence;        // rolling 0..255, receiver detects lost packets
    int16_t  tempInternal;    // tank temp [°C × 10]  e.g. 652 = 65.2 °C
```

Without `#pragma pack(1)` the compiler would insert padding to align `int16_t`
on a 2-byte boundary, and `sizeof(struct)` would no longer equal the number of
bytes on the wire — so a straight `memcpy` to the transport would send garbage.
Packing lets the struct be cast directly to a byte buffer and back.

**Temperatures are sent as `int16 × 10`, not `float`.** At 0.1 °C resolution that
is 2 bytes instead of 4, halving the payload. On the original power-line link —
roughly **100 bps effective** — that is not a micro-optimisation, it is the
difference between meeting the 1 packet/second budget and missing it.

### 3.3 A real packet, decoded

This is an actual 17-byte STATUS frame captured from the running system
(`[M<-S] seq=222 | t1=25.0 t2=23.0 t3=26.0 | flow=0.0 pwr=2499W | sts=0x02`):

```
AA 0D 01 DE FA 00 E6 00 04 01 00 00 C3 09 02 52 55
```

| Bytes | Field | Value | Meaning |
|---|---|---|---|
| `AA` | startByte | 0xAA | frame start |
| `0D` | length | 13 | payload length |
| `01` | packetType | STATUS | slave → master |
| `DE` | sequence | 222 | rolling counter |
| `FA 00` | tempInternal | 250 | **25.0 °C** (little-endian, ÷10) |
| `E6 00` | tempBoilerOut | 230 | 23.0 °C |
| `04 01` | tempBoostOut | 260 | 26.0 °C |
| `00 00` | flowRate | 0 | 0.0 L/min |
| `C3 09` | powerWatts | 2499 | 2499 W |
| `02` | statusByte | `0b00000010` | `STATUS_INTERNAL_ON` |
| `52` | crc8 | 0x52 | CRC-8/SMBUS over bytes 2–14 |
| `55` | endByte | 0x55 | frame end |

You can verify the CRC yourself against
[`proto_crc8()`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/config/boiler_protocol.h#L102-L113),
and you can capture your own frames live — see §5.3.

---

## 4. The transport abstraction

The strongest argument for the abstraction is what the two implementations look
like next to each other. **Same signature, same bytes in, same bytes out.**

### 4.1 Sending a frame — two worlds

<table>
<tr><th>PLC — KQ-330 over UART</th><th>WiFi — UDP</th></tr>
<tr><td>

```cpp
void link_send(const uint8_t* data,
               uint8_t len) {
    for (uint8_t i = 0u; i < len; i++) {
        Serial1.write(data[i]);
        delay(KQ330_INTER_BYTE_DELAY_MS);
    }
}
```

</td><td>

```cpp
void link_send(const uint8_t* data,
               uint8_t len) {
    if (!link_ready()) return;
    IPAddress dst = /* peer or broadcast */;
    udp.beginPacket(dst, PORT);
    udp.write(data, len);
    udp.endPacket();
}
```

</td></tr>
</table>

[`link_plc.cpp:60-65`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/comms/link_plc.cpp#L60-L65) · [`link_wifi.cpp:96-110`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/comms/link_wifi.cpp#L96-L110)

The PLC version must pace **every single byte** with a 2 ms gap — established by
measurement, because a bulk write made the modem silently drop the last 3–4
bytes. UDP needs none of that: the network stack already preserves message
boundaries.

### 4.2 Receiving — the asymmetry is even sharper

| | PLC | WiFi |
|---|---|---|
| Frame reassembly | **5-state machine**, byte by byte | none — a datagram *is* a frame |
| Byte-gap timeout | 2000 ms, discards partial frames | not applicable |
| Half-duplex guard | 200 ms after RX before TX | not applicable — UDP is full duplex |
| Lines of code | ~75 | ~20 |

Roughly 55 lines of `link_plc.cpp` exist **solely** to cope with the physical
behaviour of a power-line modem. Isolating them behind
[`link.h`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/comms/link.h)
is what allowed WiFi to be added **without editing a single line** of
`SystemManager`, the demo scenarios, the UI, or any slave task.

### 4.3 The whole interface

[`slave/src/comms/link.h:22-42`](https://github.com/AmitParan/smart-boiler/blob/d2c5f93/slave/src/comms/link.h#L22-L42) *(doc comments elided for brevity — see the file)*

```cpp
void        link_begin();                                   // bring transport up
bool        link_ready();                                   // usable?
void        link_service();                                 // housekeeping
void        link_send(const uint8_t* data, uint8_t len);    // send one frame
uint8_t     link_poll(uint8_t* buf, uint8_t maxlen);        // receive one frame
const char* link_name();                                    // "PLC" / "WiFi"
```

Six functions. Selected at build time by a single flag, identical on both sides:

```ini
build_flags = -DLINK_WIFI     ; WiFi (UDP)
;             (flag absent)   ; KQ-330 power-line modem
```

---

## 5. Verify every claim yourself

Nothing above has to be taken on trust.

### 5.1 Watch the safety watchdog cut the heaters (2 minutes)

Flash both boards, open both serial monitors, and let the demo reach
**Scenario 6 — Communication Loss**. The master deliberately stops transmitting.
Within 5 seconds the slave prints:

```
[SLAVE] ⚠️ NO CMD RECEIVED FOR 5s! TaskSafety TRIGGERED HARD-CUTOFF!
```

and both SSRs go off. That is §1.4 executing. **Scenario 8** does the same for
the stuck-SSR detector of §1.3, and **Scenario 7** for the 80 °C cutoff.

### 5.2 Prove the transport abstraction is real (2 minutes)

Delete `-DLINK_WIFI` from **both** `platformio.ini` files and rebuild. Both
projects compile onto the power-line transport with no source changes:

| | PLC | WiFi |
|---|---|---|
| Slave flash | 297,125 B (22.7 %) | 988,089 B (75.4 %) |
| Master flash | 2,034,762 B (31.0 %) | 2,043,126 B (31.2 %) |

The ~690 KB difference on the slave is the WiFi stack — the application code is
unchanged.

### 5.3 Capture real packets on the wire (1 minute)

With the system running over WiFi, from the repository root:

```bash
python tools/udp_link_probe.py listen
```

Every STATUS frame the slave broadcasts is printed as hex and ASCII. Decode one
against the table in §3.3 — the temperatures, the power reading and the status
bits will match what the two serial monitors are printing at that moment.

### 5.4 Prove the firmware reorganisation changed nothing

The folder restructuring was verified by rebuilding and comparing binaries: the
slave firmware is **byte-identical** before and after the move (988,089 bytes).
The master differs by exactly 4 bytes, because `CORE_DEBUG_LEVEL=1` embeds
`__FILE__` paths in log strings and those paths changed.

---

## Where to go next

| Question | File |
|---|---|
| What decides when the heater turns on? | [`master/src/control/SystemManager.cpp`](master/src/control/SystemManager.cpp) |
| How does the system learn shower times? | [`master/src/control/smart_preheat.cpp`](master/src/control/smart_preheat.cpp) |
| How are the 8 scenarios scripted? | [`master/src/demo/demo_scenarios.cpp`](master/src/demo/demo_scenarios.cpp) |
| Which GPIO is what? | [`slave/src/config/config.h`](slave/src/config/config.h) · [`master/src/config/config.h`](master/src/config/config.h) |
| Full system overview | [`README.md`](README.md) |
