// =============================================================================
// [TEST FILE] SystemManagerTest.h
// PURPOSE : Standalone test bench for SystemManager state machine.
// ENABLE  : Set #define TEST_MODE 1 in master/src/main.cpp
// DISABLE : TEST_MODE 0 (default) — this file is compiled but never runs.
// DO NOT  : Enable in production. For development validation only.
// =============================================================================
#ifndef SYSTEM_MANAGER_TEST_H
#define SYSTEM_MANAGER_TEST_H

/// FreeRTOS task entry point — runs all SystemManager test scenarios
/// in a continuous loop.  Pin to Core 1 (same as MasterComms).
void TaskSystemManagerTest(void* pvParameters);

#endif // SYSTEM_MANAGER_TEST_H
