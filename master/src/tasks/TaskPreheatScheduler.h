#pragma once

// =============================================================================
// tasks/TaskPreheatScheduler.h
// PURPOSE : FreeRTOS task — Predictive preheat decision engine.
//           Runs every 60 s. Reads the usage histogram and heatup history,
//           decides if automatic pre-heating should fire based on the learned
//           shower pattern (or user-set Ready-By time), and writes the result
//           into the UiSnapshot queue so TaskController picks it up transparently.
// =============================================================================

void TaskPreheatScheduler(void* pvParameters);
