#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  SystemMode — runtime execution mode
//
//  MODE_BENCH_TEST : Bench debugging without physical sensors attached.
//                    Safety interlocks (flow interlock, uncommanded-current)
//                    are bypassed so the SSRs can be exercised on the desk.
//  MODE_PRODUCTION : Full hardware present; all interlocks enforced.
//
//  Default is MODE_BENCH_TEST — safe for initial flash / desk testing.
//
//  Switch at runtime via the serial monitor:
//    'b' → MODE_BENCH_TEST
//    'p' → MODE_PRODUCTION
//    '?' → print current mode
// ---------------------------------------------------------------------------
enum SystemMode : uint8_t {
    MODE_BENCH_TEST = 0,
    MODE_PRODUCTION = 1,
};

extern volatile SystemMode currentMode;

// FreeRTOS task — handles serial commands for mode switching (Priority 1)
void TaskSerial(void* pvParameters);

#endif // SYSTEM_MODE_H
