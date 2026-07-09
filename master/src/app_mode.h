#ifndef APP_MODE_H
#define APP_MODE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  AppMode — runtime operation mode (toggled from the Settings screen)
//
//  APP_MODE_DEMO      : Master injects scripted sensor values into SystemManager.
//                       PLC is still active — slave receives real CMD packets
//                       and fires SSRs accordingly. Use this to demonstrate
//                       system behaviour or run automated test scenarios.
//
//  APP_MODE_REALTIME  : Master uses actual sensor readings from slave STATUS
//                       packets. Normal production operation.
// ---------------------------------------------------------------------------
enum AppMode : uint8_t {
    APP_MODE_DEMO     = 0,
    APP_MODE_REALTIME = 1,
};

extern volatile AppMode appMode;

// ---------------------------------------------------------------------------
//  Demo mode injected values
//  Written by the demo scenario task, read by sendCommand() in comms_master.cpp.
// ---------------------------------------------------------------------------
extern volatile float demo_temp;    ///< tank temperature [°C]
extern volatile float demo_flow;    ///< flow rate [L/min]
extern volatile bool  demo_ui_on;   ///< boiler ON/OFF button state

#endif // APP_MODE_H
