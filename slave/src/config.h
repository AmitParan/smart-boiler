#ifndef CONFIG_H
#define CONFIG_H

// Sensors
#define PIN_TEMP_BUS    4    // Temperature sensors
#define PIN_FLOW_SENSOR 13   // Flow sensor (moved from 16 because 16 is RX)
#define PIN_SSR         26   // Relay

// Communication with Master (Serial2)
// WARNING: Serial2 is now used by PLC (plc_comms.cpp)
// If you need Master comms, use different pins/serial port
#define SLAVE_RX        16   // Used by PLC - RX from KQ330
#define SLAVE_TX        17   // Used by PLC - TX to KQ330

// PLC Communication uses Serial2 (RX:16, TX:17) at 9600 baud
// See plc_comms.cpp for protocol implementation

#endif