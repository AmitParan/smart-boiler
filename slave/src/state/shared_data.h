#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>
#include "boiler_protocol.h"

// ---------------------------------------------------------------------------
//  Sensor readings
//  Written by sensor tasks (flow_task, temp_task, current_task).
//  Read by plc_task and safety_task.
// ---------------------------------------------------------------------------
extern float temps[3];          // °C  [0]=tank  [1]=boilerOut  [2]=boostOut
extern float current_flow;      // L/min  (YF-B6)
extern float current_rms;       // A RMS  (ACS758)
extern float power_watts;       // W      (current_rms × 220)

// ---------------------------------------------------------------------------
//  SSR state flags
//  Written by pwm_task, read by plc_task (included in the status byte).
// ---------------------------------------------------------------------------
extern volatile bool internal_ssr_on;   // true while internal SSR is HIGH
extern volatile bool boost_ssr_on;      // true while boost SSR is HIGH

// ---------------------------------------------------------------------------
//  Commands received from Master via PLC
//  Written by plc_task, read by pwm_task.
// ---------------------------------------------------------------------------
extern volatile uint8_t cmd_pwm_internal;   // 0‥100 % duty cycle
extern volatile uint8_t cmd_pwm_boost;      // 0‥100 % duty cycle
extern volatile uint8_t cmd_flags;          // CMD_* flags from boiler_protocol.h

// ---------------------------------------------------------------------------
//  Safety state
//  Written by safety_task, read by pwm_task.
//  Only cleared by a hardware reboot (deliberate safety practice).
// ---------------------------------------------------------------------------
extern volatile bool system_fault;

// ---------------------------------------------------------------------------
//  FreeRTOS mutual exclusion (used by flow_task ISR)
// ---------------------------------------------------------------------------
extern portMUX_TYPE timerMux;

#endif // SHARED_DATA_H
