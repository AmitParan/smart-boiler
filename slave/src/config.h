#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
//  Temperature sensors  (DS18B20 on 1-Wire bus)
//  Index:  temps[0] = tank internal
//          temps[1] = boiler outlet
//          temps[2] = boost heater outlet
// ---------------------------------------------------------------------------
#define PIN_TEMP_BUS          4

// ---------------------------------------------------------------------------
//  Flow sensor  (YF-B6, pulse output)
// ---------------------------------------------------------------------------
#define PIN_FLOW_SENSOR       13

// ---------------------------------------------------------------------------
//  Solid State Relays (AC burst-firing PWM)
// ---------------------------------------------------------------------------
#define PIN_SSR_INT           26    // Internal tank heater
#define PIN_SSR_EXT           25    // External inline boost heater

// ---------------------------------------------------------------------------
//  Current sensor  (ACS758LCB-050B, analog output)
//  Pin 34 is input-only on ESP32 — ideal for ADC, no accidental output
// ---------------------------------------------------------------------------
#define PIN_CURRENT_SENSOR    34

// ---------------------------------------------------------------------------
//  PLC modem  (KQ-330, UART half-duplex)
//  Serial2 — RX = 16, TX = 17  @  9600 baud
// ---------------------------------------------------------------------------
#define PLC_RX_PIN            16
#define PLC_TX_PIN            17
#define PLC_BAUD              9600

// ---------------------------------------------------------------------------
//  Timing
// ---------------------------------------------------------------------------
#define STATUS_SEND_INTERVAL_MS   1000u   // slave sends status once per second

#endif // CONFIG_H
