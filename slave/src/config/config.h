#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
//  Temperature sensors  (DS18B20 on 1-Wire bus)
//  Index:  temps[0] = tank internal
//          temps[1] = boiler outlet
//          temps[2] = boost heater outlet
//  All three sensors share a single 1-Wire bus on GPIO7
// ---------------------------------------------------------------------------
#define PIN_TEMP_BUS          7

// ---------------------------------------------------------------------------
//  Flow sensor  (YF-B6, pulse output)
//  Signal passes through MOSFET Q2 + level shifter U1 before GPIO6
//  Must use hardware interrupt (attachInterrupt)
// ---------------------------------------------------------------------------
#define PIN_FLOW_SENSOR       6

// ---------------------------------------------------------------------------
//  Solid State Relays (AC burst-firing PWM)
//  SSR_INT passes through upper safety channel (T2 BJT)
//  SSR_EXT passes through middle safety channel (T3 BJT) + flow interlock
// ---------------------------------------------------------------------------
#define PIN_SSR_INT           4    // Internal tank heater
#define PIN_SSR_EXT           5    // External inline boost heater

// ---------------------------------------------------------------------------
//  Current sensor  (ACS758LCB-050B, 5V supply, 40mV/A, quiescent = 2.5V)
//  Raw output is divided by a 1.8kΩ / 3.3kΩ voltage divider before GPIO3
//  ADC conversion must reverse the divider — see current_task.cpp
// ---------------------------------------------------------------------------
#define PIN_CURRENT_SENSOR       3
#define CURRENT_DIVIDER_RATIO    (3.3f / 5.1f)   // R2/(R1+R2) = 3.3/(1.8+3.3)
#define ACS758_SENSITIVITY       0.040f            // 40 mV/A
#define ACS758_VREF              2.5f              // Quiescent output at 0A (VCC/2)

// ---------------------------------------------------------------------------
//  PLC modem  (KQ-330, UART — RX=GPIO10 via level shifter U3, TX=GPIO11)
// ---------------------------------------------------------------------------
#define PLC_RX_PIN            10
#define PLC_TX_PIN            11
#define PLC_BAUD              9600

// ---------------------------------------------------------------------------
//  Timing
// ---------------------------------------------------------------------------
#define STATUS_SEND_INTERVAL_MS   1000u   // slave pushes STATUS once per second

#endif // CONFIG_H

