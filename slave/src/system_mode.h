#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  SystemMode - runtime execution mode
//
//  MODE_BENCH_TEST : Legacy bench mode. Physical sensors bypassed, safety
//                   interlocks (flow, uncommanded current) disabled. Use only
//                   for initial bring-up without any hardware attached.
//
//  MODE_DEMO       : Automated demo / test bench. Sensor values are injected
//                   by the Master via demoTempX10/demoFlowX10 CMD fields.
//                   All safety interlocks remain ACTIVE (use mock data).
//                   Set automatically by CMD_DEMO_ACTIVE flag in every CMD.
//
//  MODE_PRODUCTION : Full hardware present. Real sensors, all interlocks.
//
//  Default at boot: MODE_BENCH_TEST (safe without any hardware attached).
//  Switch via serial: 'b'=BENCH_TEST  'd'=DEMO  'p'=PRODUCTION  '?'=status
// ---------------------------------------------------------------------------
enum SystemMode : uint8_t {
    MODE_BENCH_TEST = 0,
    MODE_DEMO       = 1,
    MODE_PRODUCTION = 2,
};

extern volatile SystemMode currentMode;

void TaskSerial(void* pvParameters);

#endif // SYSTEM_MODE_H
