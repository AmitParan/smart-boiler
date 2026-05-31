#pragma once

// =============================================================================
// tasks/TaskHardwareValidator.h  (SLAVE)
// PURPOSE : Part 2 — Hardware-in-the-loop LED validation test.
//
// ENABLE  : Set #define HW_TEST_MODE 1  in slave/src/main.cpp
// DISABLE : HW_TEST_MODE 0 (default) — production
//
// WIRING FOR THIS TEST (replace heaters with LEDs):
//   - LED_INT  : LED + 330Ω from GPIO4 (PIN_SSR_INT) to GND
//   - LED_BOOST: LED + 330Ω from GPIO5 (PIN_SSR_EXT) to GND
//   - Leave all safety hardware (LM393N, capacitors) in place — we are
//     testing that they work correctly, not bypassing them.
// =============================================================================

void TaskHardwareValidator(void* pvParameters);
