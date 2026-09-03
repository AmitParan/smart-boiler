#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
//  Demo mode injected sensor values
//  Written by plc_comms from CMD flag bits. Read by temp/flow tasks.
// ---------------------------------------------------------------------------
extern volatile bool     slave_demo_flow_active; ///< CMD_DEMO_FLOW  → inject 6.5 L/min
extern volatile bool     slave_demo_overtemp;    ///< CMD_DEMO_OVERTEMP → inject 87°C (scenario 7)
extern volatile bool     slave_demo_fault_sim;   ///< CMD_DEMO_FAULT_SIM → stuck-SSR current (scenario 8)

// ---------------------------------------------------------------------------
//  PLC watchdog — updated every time a valid CMD is received
// ---------------------------------------------------------------------------
extern volatile uint32_t last_cmd_received_ms;  ///< millis() of last successful CMD parse
extern volatile bool     cmd_ever_received;     ///< guards against false timeout at boot
extern volatile bool     current_sensor_valid;  ///< true only if ACS758 VREF within ±10% of 2.5V at calibration

// ---------------------------------------------------------------------------
//  FreeRTOS mutual exclusion
//  guard_*    : Created in main.cpp before any task starts.
//  timerMux   : Spinlock used only inside the flow sensor ISR.
// ---------------------------------------------------------------------------
extern SemaphoreHandle_t guard_temps;    ///< guards temps[3]
extern SemaphoreHandle_t guard_flow;     ///< guards current_flow
extern SemaphoreHandle_t guard_current;  ///< guards current_rms, power_watts
extern SemaphoreHandle_t guard_cmd;      ///< guards cmd_pwm_internal/boost/flags
extern portMUX_TYPE      timerMux;       ///< ISR-safe spinlock for flow pulse counter

#endif // SHARED_DATA_H

