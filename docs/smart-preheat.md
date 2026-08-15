# Smart Preheat — Self-Learning Decision Tree (V1)

**Status:** implemented on branch `feature/smart-learning` (off `v8`).
**Where:** master firmware only (`master/src/smart_preheat.{h,cpp}`).

This is the "learning brain" the project book describes (§ decision-tree / habit
learning): a **transparent, deterministic If/Else decision tree** — *not* AI —
that learns when the household showers and pre-heats the tank so hot water is
ready on time, saving energy the rest of the day.

---

## 1. Three operation modes

Chosen on the Schedule page (v8 UI). Read by the brain via `ui_manager` accessors
(`UI_GetOpMode`, `UI_GetReadyByMinute`, `UI_GetHouseholdSize`). **All three modes
read the real sensors + weather** — the mode only changes *when the boiler decides
to run*.

| Mode | UI state | Behaviour |
|---|---|---|
| **Dumb** | `g_auto_enabled = false` | Plain reactive — user presses ON/OFF. Brain idle. |
| **Ready-by** | auto on, `g_smart_learn = false` | User sets a shower time; system pre-heats so the tank is warm by then. Works immediately (no learning wait). |
| **Smart** | auto on, `g_smart_learn = true` | System learns shower times and pre-heats automatically. User doesn't touch it. |

## 2. What "pre-heat" means

The tank's energy-saving base target is **40 °C** (SystemManager). Pre-heat simply
raises the "boiler ON" request during a window around the expected shower, so
SystemManager warms the tank to 40 °C and the inline boost is ready. The brain:
- **never** overrides a manual ON/OFF,
- **never** touches the safety interlocks,
- cannot overheat — SystemManager still regulates to 40 °C and drops to STANDBY.

## 3. Learning model

A **96-slot histogram** (24 h × 15 min). Every real shower (flow crossing 0 → >1 L/min)
increments its slot. Persisted to SPIFFS flash (`/preheat.json` via `DataManager`)
so it survives reboots and accumulates over 1–2 weeks.

- **Reliable** when the busiest slot has been seen **≥ 3 times** AND **≥ 7 days**
  of data exist. Until then, Smart mode keeps learning and does nothing.
- **Predicted time** = centre of the busiest slot.

## 4. The decision tree (runs every 60 s)

```
s_wantsHeat = false
if mode == DUMB              → return   # brain idle
if PLC link lost (>5 s)      → return   # safety: stand down (watchdog)
if clock not synced          → return   # need real time
if tank temp >= 80 C         → return   # safety: hand off to hard protections
if user pressed ON           → return   # respect manual control
target = (mode==READY_BY) ? user_time : learned_peak
if SMART and not reliable    → return   # still learning
if now within [target − lead, target + grace] → s_wantsHeat = true
```

Safety & user priority always win: a manual action, a **PLC timeout > 5 s**
(watchdog in `comms_master`, feeds `plcConnected`), or an **over-temp (≥ 80 °C
at the brain; SystemManager hard-cuts at 85 °C)** bypass the engine and return
control to the controller's hard protections.

`comms_master` then sets `uiStateOn = manual_on || wantsHeat()`.

**Lead time (V1):** fixed `BASE_LEAD_MIN = 45 min` (+5 min/person above 2, capped 90).
`SHOWER_GRACE_MIN = 45 min` keeps the boiler ready through the shower.

## 5. Persistence (SPIFFS)

`DataManager::savePreheat/loadPreheat` store the histogram + `total`, `days`,
`lastYday` as `/preheat.json` (~200 bytes). Loaded once at boot in `setup()`.

## 6. Integration points

| File | Change |
|---|---|
| `smart_preheat.{h,cpp}` | the brain (new) |
| `DataManager.{h,cpp}` | `savePreheat` / `loadPreheat` (SPIFFS) |
| `ui_manager.{h,cpp}` | `UI_GetOpMode` / `UI_GetReadyByMinute` / `UI_GetHouseholdSize` |
| `comms_master.cpp` | shower detection (flow rising edge) + 60 s brain tick + `uiStateOn` OR |
| `main.cpp` | `SmartPreheat::init()` after `DataManager::init()` |

## 7. V1 limitations / V2 roadmap

- **Fixed lead time** — a real cold tank can take ~1.4 h to reach 40 °C. V2: adaptive
  lead measured from real heat-up sessions (a `HeatupTracker`).
- **Weather** is read by the system but not yet used to adjust the lead. V2: colder
  inlet → start earlier.
- **Learning runs in REALTIME only** (real showers). Demonstrating it needs real time
  passing or an event-injection test hook.
- **UI feedback** (showing predicted time / "learning… N/7 days" on the Smart panel)
  is a small follow-up; the control path (modes → real behaviour) is wired.
- **Time-of-day only** — the learning histogram aggregates all days into one
  96-slot model (it does not separate weekday vs weekend showers). Ready-by mode
  *does* distinguish weekday/weekend. Per-day-of-week learning is a clean V2.
- The decision engine is a **periodic 60 s routine inside `TaskMasterComms`**, not
  a standalone FreeRTOS task (functionally equivalent; could be split out later).
- **Skip-today** button is not yet gated into the brain.
