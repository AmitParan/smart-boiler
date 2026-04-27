#ifndef SYSTEM_MANAGER_TEST_H
#define SYSTEM_MANAGER_TEST_H

/// FreeRTOS task entry point — runs all SystemManager test scenarios
/// in a continuous loop.  Pin to Core 1 (same as MasterComms).
void TaskSystemManagerTest(void* pvParameters);

#endif // SYSTEM_MANAGER_TEST_H
