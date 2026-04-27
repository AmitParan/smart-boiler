#include "SystemManager.h"

// ===========================================================================
//  SystemManager::labelFor
// ===========================================================================
const char* SystemManager::labelFor(BoilerState state) {
    switch (state) {
        case BoilerState::SAFETY_OVERRIDE: return "SAFETY_OVERRIDE";
        case BoilerState::STATE_OFF:       return "STATE_OFF";
        case BoilerState::STATE_BOOST:     return "STATE_BOOST";
        case BoilerState::STATE_HEATING:   return "STATE_HEATING";
        case BoilerState::STATE_STANDBY:   return "STATE_STANDBY";
        default:                           return "UNKNOWN";
    }
}

// ===========================================================================
//  SystemManager::proportionalPWM  (private)
//
//  Linear ramp across PROP_WINDOW_C degrees:
//    delta <= 0              → 0
//    0 < delta < PROP_WINDOW → (delta / PROP_WINDOW) * PWM_MAX
//    delta >= PROP_WINDOW    → PWM_MAX  (full power)
// ===========================================================================
uint8_t SystemManager::proportionalPWM(float currentTemp, float targetTemp) {
    float delta = targetTemp - currentTemp;
    if (delta <= 0.0f)               return PWM_OFF;
    if (delta >= PROP_WINDOW_C)      return PWM_MAX;
    return static_cast<uint8_t>((delta / PROP_WINDOW_C) * static_cast<float>(PWM_MAX));
}

// ===========================================================================
//  SystemManager::process
//
//  Priority order (evaluated top to bottom — first match wins):
//
//  1. SAFETY_OVERRIDE : !plcConnected  OR  currentTemp >= TEMP_CUTOFF_C
//  2. STATE_OFF       : !uiStateOn
//  3. STATE_BOOST     : flowRateLPM > FLOW_THRESHOLD_LPM
//                       → pwmBoost = PWM_MAX
//                       → pwmInternal = proportional (heat while water runs)
//  4. STATE_HEATING   : currentTemp < targetTemp
//                       → pwmInternal = proportional, pwmBoost = 0
//  5. STATE_STANDBY   : currentTemp >= targetTemp
//                       → both = 0
// ===========================================================================
SystemCommand SystemManager::process(const SystemInputs& in) const {
    SystemCommand cmd{};

    // ------------------------------------------------------------------
    // 1. SAFETY OVERRIDE — hard fail-safe, no further evaluation
    // ------------------------------------------------------------------
    if (!in.plcConnected || in.currentTemp >= TEMP_CUTOFF_C) {
        cmd.pwmInternal = PWM_OFF;
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::SAFETY_OVERRIDE;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 2. STATE_OFF — user switched boiler off
    // ------------------------------------------------------------------
    if (!in.uiStateOn) {
        cmd.pwmInternal = PWM_OFF;
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::STATE_OFF;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 3. STATE_BOOST — water is actively flowing
    //    Boost heater runs at full power.
    //    Internal heater runs proportionally so the tank stays hot.
    // ------------------------------------------------------------------
    if (in.flowRateLPM > FLOW_THRESHOLD_LPM) {
        cmd.pwmBoost    = PWM_MAX;
        cmd.pwmInternal = proportionalPWM(in.currentTemp, in.targetTemp);
        cmd.state       = BoilerState::STATE_BOOST;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 4. STATE_HEATING — tank below target, no flow
    // ------------------------------------------------------------------
    if (in.currentTemp < in.targetTemp) {
        cmd.pwmInternal = proportionalPWM(in.currentTemp, in.targetTemp);
        cmd.pwmBoost    = PWM_OFF;
        cmd.state       = BoilerState::STATE_HEATING;
        cmd.stateLabel  = labelFor(cmd.state);
        return cmd;
    }

    // ------------------------------------------------------------------
    // 5. STATE_STANDBY — tank at or above target, no flow
    // ------------------------------------------------------------------
    cmd.pwmInternal = PWM_OFF;
    cmd.pwmBoost    = PWM_OFF;
    cmd.state       = BoilerState::STATE_STANDBY;
    cmd.stateLabel  = labelFor(cmd.state);
    return cmd;
}
