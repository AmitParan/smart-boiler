#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdint.h>

// ===========================================================================
//  SystemManager — Core State Machine for the Smart Boiler Master Unit
//
//  Evaluates sensor inputs and UI settings, then calculates PWM duty cycles
//  for the two SSR relays on the Slave unit.
//
//  PWM output range: 0–100  (percentage)
//  Matches BoilerCmdPacket_t::pwmInternal/pwmBoost wire encoding.
//  The slave uses this as a time-proportion percentage: 50 → ON 500ms/OFF 500ms
//  per 1-second cycle.  Do NOT use 0-255 — SSRs are not hardware-PWM devices.
//
//  State priority (highest → lowest):
//    SAFETY_OVERRIDE  — plcConnected==false OR currentTemp >= TEMP_CUTOFF_C
//    STATE_OFF        — uiStateOn==false
//    STATE_BOOST      — flow > FLOW_THRESHOLD_LPM  (boost + proportional heat)
//    STATE_HEATING    — no flow AND currentTemp < targetTemp
//    STATE_STANDBY    — no flow AND currentTemp >= targetTemp
// ===========================================================================

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------
static constexpr float TEMP_CUTOFF_C      = 85.0f;  ///< Hard safety trip
static constexpr float FLOW_THRESHOLD_LPM =  0.5f;  ///< Min flow for boost
static constexpr float PROP_WINDOW_C      =  5.0f;  ///< Proportional band (°C)
static constexpr uint8_t PWM_MAX          = 100u;  ///< 100 % — matches wire protocol
static constexpr uint8_t PWM_OFF          =   0u;

// ---------------------------------------------------------------------------
//  BoilerState — textual state for logging / UI display
// ---------------------------------------------------------------------------
enum class BoilerState : uint8_t {
    SAFETY_OVERRIDE = 0,  ///< Hardware/comms protection — all heat cut
    STATE_OFF,            ///< User set boiler OFF
    STATE_BOOST,          ///< Water flowing — boost heater active
    STATE_HEATING,        ///< Tank below target — internal heater active
    STATE_STANDBY,        ///< Tank at target, no flow — heaters idle
};

// ---------------------------------------------------------------------------
//  SystemInputs — snapshot of all inputs to the state machine
// ---------------------------------------------------------------------------
struct SystemInputs {
    float currentTemp;    ///< Tank water temperature [°C]  (tempInternal)
    float flowRateLPM;    ///< Flow rate [L/min]
    float targetTemp;     ///< User-set target temperature [°C]
    bool  uiStateOn;      ///< true = boiler ON  (power button)
    bool  plcConnected;   ///< true = valid STATUS received within 3 s
};

// ---------------------------------------------------------------------------
//  SystemCommand — output of the state machine
// ---------------------------------------------------------------------------
struct SystemCommand {
    uint8_t    pwmInternal;   ///< Duty cycle for internal tank heater [0–100 %]
    uint8_t    pwmBoost;      ///< Duty cycle for boost inline heater   [0–100 %]
    BoilerState state;        ///< Current machine state
    const char* stateLabel;  ///< Human-readable label (static string, safe to log)
};

// ---------------------------------------------------------------------------
//  SystemManager
// ---------------------------------------------------------------------------
class SystemManager {
public:
    SystemManager() = default;

    /// Evaluate inputs and return the calculated command for this cycle.
    /// Thread-safe: no mutable state — pure calculation.
    SystemCommand process(const SystemInputs& inputs) const;

    /// Returns the label string for a given state (static storage, never null).
    static const char* labelFor(BoilerState state);

private:
    /// Proportional PWM: 0 at delta<=0, linear up to PWM_MAX at delta>=PROP_WINDOW_C.
    static uint8_t proportionalPWM(float currentTemp, float targetTemp);
};

#endif // SYSTEM_MANAGER_H
