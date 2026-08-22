# Smart Boiler — Decision Tree & Learning Brain

**Version:** 1.1  
**Last updated:** May 9, 2026  
**Status:** Design phase — not yet implemented

---

## 1. Why a Smart Brain?

Today the boiler is **reactive** — the user presses ON, waits for the tank to heat up (~20–30 min from cold), then showers. This wastes time and energy.

The goal of the Smart Brain is to make the boiler **predictive**:
- Learn when the user showers every day
- Pre-heat automatically so hot water is ready when the user arrives
- Save energy by heating only to the base 40°C during idle and boosting only during actual flow
- Adapt over time as habits change

This is not AI — it is a **deterministic decision tree** built on accumulated usage data. Simple, transparent, and auditable.

---

## 2. Data the System Already Has

The slave measures and reports every second:

| Data | Source | Resolution |
|------|--------|------------|
| Tank temperature | DS18B20 | 0.1°C |
| Flow rate | YF-B6 | 0.1 L/min |
| Power consumption | ACS758 current | ~1W |
| Boiler state (ON/OFF) | UI touch | boolean |
| Target shower temperature | UI slider | 1°C steps |

The master also has:
- **Real-time clock** (NTP synced) → exact timestamp for every event
- **WiFi** → can report data or receive updates
- **SPIFFS flash storage** → can persist data across reboots
- **Weather API** (OpenWeatherMap, fetched every 10 min) → outdoor temperature, forecast, cloud cover, UV index

### What Weather Data Gives Us
The outdoor temperature directly affects how much heating work the system needs to do:

| Condition | Effect on system |
|-----------|------------------|
| Hot summer day (35°C+) | Pipes and inlet water warm naturally — boost gap is smaller, start later |
| Cold winter morning (5°C) | Inlet water is cold — need more heating, start earlier |
| Sunny forecast | Solar gain on pipes/tank — may reduce required lead time |
| Cloud cover | No passive solar help — use full lead time |

The system already fetches `temp`, `weather description`, and forecast from OpenWeatherMap. The brain will use this to adjust the pre-heat start time dynamically.

---

## 3. Event Model

Everything the brain learns comes from **events**. An event is a timestamped record of something that happened.

### Event Types

```c
enum class BoilerEvent : uint8_t {
    USER_ON        = 0x01,  // user pressed ON on touchscreen
    USER_OFF       = 0x02,  // user pressed OFF
    FLOW_START     = 0x03,  // flow sensor crossed threshold (tap opened)
    FLOW_STOP      = 0x04,  // flow dropped to zero (tap closed)
    TEMP_SET       = 0x05,  // user changed target shower temperature
    AUTO_PREHEAT   = 0x06,  // system auto-started pre-heat (brain decision)
    TANK_READY     = 0x07,  // tank reached target temperature
    TARGET_TIME_SET = 0x08, // user set a fixed ready-by time
    WEATHER_ADJUST  = 0x09, // brain adjusted lead time due to weather
};

struct BoilerEventRecord {
    uint32_t  unixTimestamp;   // seconds since epoch (from NTP)
    BoilerEvent type;
    int16_t   valueX10;        // context: temp × 10, or flow × 10, or 0
};
```

### Event Storage
Events are appended to a circular log in SPIFFS: `events.bin`

- Each record: 7 bytes
- Target log size: 7 days × ~50 events/day × 7 bytes = ~2.5 KB (well within SPIFFS)
- When full: oldest records overwritten (circular buffer)

---

## 4. What the Brain Learns

### V1 — Daily Schedule Detection

**Question:** "At what time does this user typically open the tap?"

**Method:**
1. Every time a `FLOW_START` event is logged, extract the time-of-day (hour + minute)
2. Keep a histogram: 24 hours × 4 quarter-slots = 96 buckets, each storing a count
3. After 7 days of data, find the bucket with the highest count → that is the predicted shower time
4. If the top bucket has ≥ 3 hits (out of 7 days) → pattern is considered reliable
5. Schedule a pre-heat alarm: fire `AUTO_PREHEAT` N minutes before that time

**Example histogram (simplified):**
```
06:00–06:15  ████████ 8   ← peak, user showers here
06:15–06:30  ██ 2
07:00–07:15  ███ 3
19:00–19:15  █ 1
```
→ Schedule auto pre-heat at 05:40 (20 min lead time)

### V3 — Weather-Adjusted Lead Time

**Question:** "Given today's outdoor temperature, how much of the heating work can nature do for free?"

**How it works:**

The cold water entering the boiler is not always the same temperature. In summer, pipes sitting in the sun warm up to 25–30°C. In winter, groundwater and shaded pipes may be 12–15°C. This directly changes how much energy the heater needs to add.

The brain uses the outdoor temperature from the weather API as a proxy for inlet water temperature:

```
inlet_temp_estimate = BASE_INLET_TEMP + (outdoor_temp - 20°C) × PIPE_FACTOR
```

Where `BASE_INLET_TEMP` = 18°C (calibrated from observed tank cool-down rate), `PIPE_FACTOR` = 0.3 (empirical — pipes don't track outdoor temp 1:1).

**Effect on lead time:**
```
Warm day (32°C outdoor):  inlet ≈ 21.6°C  → gap to 55°C = 33.4°C  → lead time: 14 min
Cold day (8°C outdoor):   inlet ≈ 15.6°C  → gap to 55°C = 39.4°C  → lead time: 19 min
```

The system logs a `WEATHER_ADJUST` event every time it changes the pre-heat start time based on weather, so you can audit it.

**Sunny day bonus (cloud cover < 20%):**  
If the forecast shows clear sky AND outdoor temp > 28°C, the system adds an extra 5-minute delay before starting pre-heat (passive solar may do part of the work first). It monitors whether the tank temp rises on its own — if it does, it delays further. If not, it starts normally.

---

### V4 — User-Defined Target Time (Fixed Schedule Override)

**Question:** "What if the user already knows exactly when they want hot water?"

The learning system is powerful but takes 7 days. Some users just want to set a time. This feature gives them that — and the brain still makes it energy-optimal.

**How it works:**

The user sets a "ready-by" time on the touchscreen — for example, `07:00`. The brain then:

1. Reads the current tank temperature
2. Reads the target shower temperature (from UI slider)
3. Estimates the heat-up gap: `gap = target_temp - tank_temp`
4. Looks up the rolling average lead time from history (or uses default 20 min if no history)
5. Adjusts lead time for today's weather (V3 logic above)
6. Calculates: `start_time = ready_by_time - adjusted_lead_time`
7. Waits until `start_time`, then turns boiler ON

**Example:**
```
User sets ready-by: 07:00
Current tank: 40°C  →  target: 55°C  →  gap: 15°C
Rolling average heat-up: 18 min
Weather today: 30°C outdoor → inlet warm → adjust: -3 min
Adjusted lead time: 15 min
System starts at: 06:45
```

**Per-day-of-week schedule:**  
Users can set different times for weekdays vs weekends:
```
Mon–Fri: ready at 07:00
Sat–Sun: ready at 09:00
```

**Target time + learned schedule interaction:**
- If user has set a fixed time → fixed time takes priority, learning supplements it
- If no fixed time is set → learned histogram drives the schedule
- If both predict the same window → confirm the pattern, no change
- If they conflict → fixed time wins, log the conflict for the user to see

**Energy optimization with fixed time:**  
The system does not simply turn ON at the calculated start time and wait. It monitors the tank temperature in real time:
```
start_time = 06:45

06:45: tank = 40°C → turn ON (internal heater 100%)
06:52: tank = 48°C → still heating
06:58: tank = 54°C → almost there, slow down (reduce to 60% PWM if near target)
07:00: tank = 55°C → TANK_READY event logged, heater OFF, standby
```
This prevents overshooting (wasted energy heating beyond the target).

---

### V2 — Adaptive Lead Time

**Question:** "How many minutes does THIS boiler take to heat from 40°C to shower temperature?"

**Method:**
1. Every time the user presses ON, record: `[timestamp, tank_temp_at_start, target_temp]`
2. When `TANK_READY` fires, record: `[timestamp]`
3. Compute: `heat_up_minutes = (TANK_READY.time - USER_ON.time) / 60`
4. Store the last 5 heat-up durations, compute rolling average
5. Use this as the pre-heat lead time instead of a fixed 20 minutes

**Example:**
```
Session 1: cold tank 20°C → 60°C took 28 min
Session 2: warm tank 38°C → 60°C took 12 min
Session 3: cold tank 22°C → 60°C took 26 min
Rolling average: 22 min → use 25 min lead time (with 3 min safety margin)
```

### V3 — Multi-User / Weekend Detection (future)
- Detect different patterns for weekdays vs weekends
- Detect second user (second daily flow peak)
- Adjust schedule per day-of-week

---

## 5. Decision Tree

Every 60 seconds, the brain evaluates this tree:

```
START
  │
  ├─ Is system_fault active?
  │     └─ YES → do nothing, wait for reboot
  │
  ├─ Is user currently ON (manual)?
  │     └─ YES → let SystemManager handle it normally, log events
  │
  ├─ Is schedule learning enabled? (user setting)
  │     └─ NO → do nothing
  │
  ├─ Do we have ≥ 7 days of data?
  │     └─ NO → keep logging, build histogram
  │
  ├─ Is the pattern reliable? (top bucket ≥ 3 hits)
  │     └─ NO → keep logging
  │
  ├─ Is now within [predicted_time - lead_time] window?
  │     └─ NO → do nothing
  │
  └─ YES → fire AUTO_PREHEAT
             │
             ├─ Turn boiler ON (set boiler_state = true)
             ├─ Log AUTO_PREHEAT event
             └─ Wait for FLOW_START
                   │
                   ├─ FLOW_START received → normal operation continues
                   └─ No FLOW_START after 90 min → turn OFF, log anomaly
```

---

## 6. Data Persistence — SPIFFS Layout

```
/events.bin       ← circular event log (7-byte records)
/histogram.bin    ← 96-bucket time-of-day histogram (uint16 per bucket)
/heatup.bin       ← last 5 heat-up durations in minutes (uint8 per entry)
/settings.bin     ← user preferences (schedule on/off, lead time override)
```

All files survive power cuts and reboots. The `DataManager` class already handles SPIFFS reads/writes — the brain will use the same pattern.

---

## 7. UI Integration

The master touchscreen needs to show the brain's state and allow the user to control it:

### New UI elements needed
| Element | Location | Purpose |
|---------|----------|---------|
| "Auto" indicator | Top bar | Shows when auto pre-heat is active |
| Schedule card | Right panel | Shows predicted shower time and lead time |
| **"Ready By" time picker** | Settings screen | User sets fixed target time (hour:min, per weekday/weekend) |
| **Weather influence indicator** | Schedule card | Shows "☀️ Adjusted -3 min" or "❄️ Adjusted +4 min" |
| Learning progress | Settings screen | "Learning: Day 4 of 7" |
| Override button | Schedule card | "Disable auto today" |
| History graph | Settings screen | Bar chart of shower times this week |

---

## 8. Implementation Plan

### Phase 1 — Event logging (no decisions yet)
- [ ] Define `BoilerEventRecord` struct in `master/src/brain/event_log.h`
- [ ] Write `EventLog` class: `append()`, `readAll()`, circular SPIFFS file
- [ ] Hook into `ui_manager.cpp`: log `USER_ON`, `USER_OFF`, `TEMP_SET`
- [ ] Hook into `comms_master.cpp`: log `FLOW_START`, `FLOW_STOP`, `TANK_READY`
- [ ] Verify events appear in serial monitor and survive reboot

### Phase 2 — Histogram and pattern detection
- [ ] Define `ShowerHistogram` class: 96 buckets, `increment()`, `findPeak()`, `isReliable()`
- [ ] After each `FLOW_START` event: update histogram
- [ ] Persist histogram to SPIFFS on every update
- [ ] Add serial log: `[BRAIN] Peak shower slot: 06:00 (8 hits)`

### Phase 3 — Auto pre-heat
- [ ] Add `BrainTask` (FreeRTOS, Core 1, runs every 60s)
- [ ] Implement the decision tree above
- [ ] Wire to `boiler_state` — brain can turn ON just like the user can
- [ ] Add `AUTO_PREHEAT` event logging
- [ ] Add 90-minute watchdog: if no flow after auto start → turn OFF

### Phase 4 — Adaptive lead time
- [ ] Track heat-up duration per session
- [ ] Store rolling average in `heatup.bin`
- [ ] Replace fixed 20-min lead time with measured average + 3 min margin

### Phase 5 — Weather adjustment
- [ ] Wire OpenWeatherMap outdoor temp into the brain (already fetched in `ui_manager.cpp`)
- [ ] Implement `inlet_temp_estimate()` function
- [ ] Adjust lead time based on estimated inlet temperature
- [ ] Add sunny-day passive delay logic (cloud cover check)
- [ ] Log `WEATHER_ADJUST` events

### Phase 6 — User-defined target time
- [ ] Add "Ready By" time picker to settings screen (hour + minute, per weekday/weekend)
- [ ] Persist target times to `settings.bin` in SPIFFS
- [ ] Brain reads target time on startup and after UI change
- [ ] Implement back-calculation: `start_time = ready_by - adjusted_lead_time`
- [ ] Implement real-time temperature monitoring during heat-up (prevent overshoot)
- [ ] Handle conflict between fixed time and learned histogram

### Phase 7 — UI
- [ ] Add schedule card to right panel
- [ ] Add learning progress indicator
- [ ] Add settings screen with on/off toggle

---

## 9. Key Design Decisions

**Why SPIFFS and not a server?**  
The system must work without internet. All learning happens on-device. WiFi is optional (weather, OTA updates) — not required for the core intelligence.

**Why not a neural network or ML model?**  
Overkill for this problem. A histogram of 96 time slots is deterministic, explainable, and uses 192 bytes of storage. A TensorFlow model would need megabytes and still do the same job.

**Why 7 days minimum before acting?**  
A single data point is not a pattern. 7 days catches weekly habits (different weekend vs weekday behaviour) and avoids false triggers from one unusual day.

**What if the user's schedule changes?**  
The histogram uses a sliding window — events older than 30 days are expired. The system re-learns naturally as the histogram shifts toward the new pattern.
