#ifndef PLC_TEST_SENDER_H
#define PLC_TEST_SENDER_H

/// FreeRTOS task — replaces TaskPLC when SLAVE_TEST_MODE is enabled.
/// Cycles through scripted sensor scenarios, sending fabricated STATUS
/// packets to the master so every SystemManager state can be observed.
void TaskPLCTestSender(void* pvParameters);

#endif // PLC_TEST_SENDER_H
