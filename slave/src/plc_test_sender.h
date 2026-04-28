// =============================================================================
// [TEST FILE] plc_test_sender.h
// PURPOSE : Scripted STATUS packet sender — drives master SystemManager
//           through all sensor-based scenarios over the real PLC line.
// ENABLE  : Set #define SLAVE_TEST_MODE 1 in slave/src/main.cpp
// DISABLE : SLAVE_TEST_MODE 0 (default) — real TaskPLC runs instead.
// DO NOT  : Enable in production. For development validation only.
// =============================================================================
#ifndef PLC_TEST_SENDER_H
#define PLC_TEST_SENDER_H

/// FreeRTOS task — replaces TaskPLC when SLAVE_TEST_MODE is enabled.
/// Cycles through scripted sensor scenarios, sending fabricated STATUS
/// packets to the master so every SystemManager state can be observed.
void TaskPLCTestSender(void* pvParameters);

#endif // PLC_TEST_SENDER_H
