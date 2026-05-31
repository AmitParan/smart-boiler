#pragma once

// =============================================================================
// system/InteractiveTestBench.h
// PURPOSE : Part 1 — Interactive serial test bench for the SystemManager.
//
// ENABLE  : Set #define TEST_MODE 2 in main.cpp
// DISABLE : TEST_MODE 0 (default) — production
//           TEST_MODE 1 — automated scenario runner (SystemManagerTest)
//
// HOW TO USE:
//   1. Flash with TEST_MODE 2
//   2. Open Serial Monitor at 115200 baud
//   3. The menu prints automatically — type a command and press Enter
//
// =============================================================================

/// FreeRTOS task entry point.  Pin to Core 1, priority 1.
void TaskInteractiveTestBench(void* pvParameters);
