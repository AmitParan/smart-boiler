#ifndef DEMO_SCENARIOS_H
#define DEMO_SCENARIOS_H

// FreeRTOS task — runs all 8 automated test scenarios sequentially in demo mode.
// Starts automatically; only executes when appMode == APP_MODE_DEMO.
void TaskAutomatedTestBench(void* pvParameters);

#endif // DEMO_SCENARIOS_H
