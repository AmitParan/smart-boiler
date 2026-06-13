#include "SystemManager.h"

// ===========================================================================
//  SystemManager::labelFor
// ===========================================================================
const char* SystemManager::labelFor(BoilerState state) {
    switch (state) {
        case BoilerState::SAFETY_OVERRIDE:   return "SAFETY_OVERRIDE";
        case BoilerState::STATE_OFF:         return "STATE_OFF";
        case BoilerState::STATE_SHOWER_BOOST: return "STATE_SHOWER_BOOST";
        case BoilerState::STATE_HEATING_TANK: return "STATE_HEATING_TANK";
        case BoilerState::STATE_STANDBY:     return "STATE_STANDBY";
        default:                             return "UNKNOWN";
    }
}

// ===========================================================================
//  SystemManager::process
//
//  Dual-target energy-saving logic:
//
//  Priority  State               Condition
//  --------  ------------------  -----------------------------------------
//  1 (HIGH)  SAFETY_OVERRIDE     !plcConnected  OR  currentTemp >= 85 °C
//  2         STATE_OFF           !uiStateOn
//  3         STATE_SHOWER_BOOST  flowRateLPM > 0.5  →  boost=100, internal=0
//  4         STATE_HEATING_TANK  no flow AND currentTemp < TARGET_TANK_TEMP
//                                →  internal=100, boost=0
//  5 (LOW)   STATE_STANDBY       no flow AND currentTemp >= TARGET_TANK_TEMP
//                                →  both=0  (waiting for next shower)
//
//  Key design decisions:
//  - Internal heater targets 40 °C only, not shower temp → less standing loss
//  - Both heaters are NEVER on simultaneously (breaker protection)
//  - Boost heater is binary (0 or 100) — it is always working at full power
//    to bridge the temperature gap quickly while water is flowing
// ===========================================================================
SystemCommand SystemManager::process(const SystemInputs& in) const {
    SystemCommand cmd{};

    // ------------------------------------------------------------------
    // 1. SAFETY OVERRIDE
    // ------------------------------------------------------------------
    if (!in.plcConnected || in.currentTemp >= TEMP_CUTOFF_C) {
        cmd.pwmInternal = PWM_OFF;
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::SAFETY_OVERRIDE;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 2. OFF — user pressed the power button
    // ------------------------------------------------------------------
    if (!in.uiStateOn) {
        cmd.pwmInternal = PWM_OFF;
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::STATE_OFF;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 3. SHOWER BOOST — tap is open
    //    Boost heater bridges tank temp → desired shower temp.
    //    Internal heater OFF: prevent simultaneous >6 kW draw on 16 A circuit.
    // ------------------------------------------------------------------
    if (in.flowRateLPM > FLOW_THRESHOLD_LPM) {
        cmd.pwmInternal = PWM_OFF;
        cmd.pwmBoost    = (in.currentTemp < BOOST_CUTOFF_C) ? PWM_MAX : PWM_OFF;
        cmd.state       = BoilerState::STATE_SHOWER_BOOST;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 4. HEATING TANK — no flow, tank below energy-saving base temp
    //    Internal heater brings tank up to TARGET_TANK_TEMP (40 °C).
    // ------------------------------------------------------------------
    if (in.currentTemp < TARGET_TANK_TEMP) {
        cmd.pwmInternal = PWM_MAX;
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::STATE_HEATING_TANK;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 5. STANDBY — tank warm, no flow, waiting for next shower
    // ------------------------------------------------------------------
    cmd.pwmInternal = PWM_OFF;
    cmd.pwmBoost    = PWM_OFF;
    cmd.state       = BoilerState::STATE_STANDBY;
    cmd.stateLabel  = labelFor(cmd.state);
    return cmd;
}

