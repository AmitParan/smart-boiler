#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdint.h>

// ===========================================================================
//  SystemManager — Dual-Target Energy-Saving State Machine
//
//  PHILOSOPHY:
//    Two heaters serve two separate jobs:
//
//    INTERNAL heater (main tank, 150 L):
//      Keeps bulk water at a LOW base temperature (TARGET_TANK_TEMP, ~40 °C).
//      Running continuously at 40 °C vs 60 °C cuts standing heat loss ~60 %.
//
//    EXTERNAL boost heater (inline, instant-on):
//      Only fires when the tap is open.  Bridges the gap from 40 °C → shower
//      temperature (targetShowerTemp, user-set on UI, e.g. 60 °C).
//      Energy spent only on water the user is *actually consuming*.
//
//    NEVER both on simultaneously — prevents tripping a 16 A house breaker
//    when each element is 3000 W+.
//
//  PWM range: 0–100  (percentage, matches BoilerCmdPacket_t wire encoding)
//  Slave converts: on_ms = pwm% * 10  within a 1-second SSR cycle.
//
//  State priority (highest → lowest):
//    SAFETY_OVERRIDE   — PLC lost OR currentTemp >= 85 °C
//    STATE_OFF         — user switched boiler OFF
//    STATE_SHOWER_BOOST — tap open (flow > 0.5 L/min) → boost only
//    STATE_HEATING_TANK — no flow, tank below base temp → internal only
//    STATE_STANDBY     — no flow, tank warm enough → all off, waiting
// ===========================================================================

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------
static constexpr float   TEMP_CUTOFF_C      = 85.0f;  ///< Hard safety trip
static constexpr float   FLOW_THRESHOLD_LPM =  0.5f;  ///< Min flow to trigger boost
static constexpr float   TARGET_TANK_TEMP   = 40.0f;  ///< Base tank temp (energy-saving)
static constexpr float   BOOST_CUTOFF_C     = 45.0f;  ///< Tank temp above which boost is not needed
static constexpr uint8_t PWM_MAX            = 100u;   ///< 100 % on the wire
static constexpr uint8_t PWM_OFF            =   0u;

// ---------------------------------------------------------------------------
//  BoilerState
// ---------------------------------------------------------------------------
enum class BoilerState : uint8_t {
    SAFETY_OVERRIDE   = 0,  ///< Fail-safe — all heat cut
    STATE_OFF,              ///< User pressed OFF
    STATE_SHOWER_BOOST,     ///< Tap open — boost heater bridges tank→shower temp
    STATE_HEATING_TANK,     ///< No flow — internal heater warming tank to base temp
    STATE_STANDBY,          ///< No flow, tank warm — waiting for next shower
};

// ---------------------------------------------------------------------------
//  SystemInputs
// ---------------------------------------------------------------------------
struct SystemInputs {
    float currentTemp;      ///< Tank water temperature [°C]  (tempInternal from slave)
    float flowRateLPM;      ///< Flow rate [L/min]
    float targetShowerTemp; ///< User-set desired shower temperature [°C]
    bool  uiStateOn;        ///< true = boiler ON (power button on touchscreen)
    bool  plcConnected;     ///< true = valid STATUS received within 3 s
};

// ---------------------------------------------------------------------------
//  SystemCommand
// ---------------------------------------------------------------------------
struct SystemCommand {
    uint8_t     pwmInternal;  ///< Internal tank heater duty [0–100 %]
    uint8_t     pwmBoost;     ///< Boost inline heater duty   [0–100 %]
    BoilerState state;        ///< Current state
    const char* stateLabel;   ///< Human-readable label (static, safe to log)
};

// ---------------------------------------------------------------------------
//  SystemManager
// ---------------------------------------------------------------------------
class SystemManager {
public:
    SystemManager() = default;

    /// Pure calculation — thread-safe, no internal state mutated.
    SystemCommand process(const SystemInputs& inputs) const;

    /// Static label lookup — never returns null.
    static const char* labelFor(BoilerState state);
};

#endif // SYSTEM_MANAGER_H
