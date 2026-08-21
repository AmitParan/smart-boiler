#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  SystemMode - matches master AppMode exactly (same names, same values)
//
//  MODE_DEMO     : Slave uses mock sensor data driven by master CMD flags.
//                  Overheat + flow interlock + uncommanded-current checks
//                  all ACTIVE (mock data is correct for each scenario).
//                  Safety faults auto-clear after 2s so the test cycle
//                  continues without a hardware reboot.
//
//  MODE_REALTIME : Slave reads real DS18B20 / YF-B6 / ACS758 sensors.
//                  All safety interlocks permanently active.
//
//  Default at boot: MODE_DEMO (safe without physical sensors attached).
//  Auto-switch: CMD_DEMO_ACTIVE flag in every CMD propagates the master mode.
//  Serial override: 'd' = DEMO,  'r' = REALTIME,  '?' = status
// ---------------------------------------------------------------------------
enum SystemMode : uint8_t {
    MODE_DEMO     = 0,
    MODE_REALTIME = 1,
};

extern volatile SystemMode currentMode;
extern volatile bool serialModeOverride;

void TaskSerial(void* pvParameters);

#endif // SYSTEM_MODE_H

