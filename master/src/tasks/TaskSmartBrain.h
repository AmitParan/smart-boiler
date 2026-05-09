#pragma once

// =============================================================================
// tasks/TaskSmartBrain.h
// PURPOSE : FreeRTOS task — Smart Brain decision engine (Phase 3).
//           Runs every 60 s. Reads histogram, decides if automatic pre-heat
//           should fire, and writes the result into the UiSnapshot queue so
//           the existing TaskBrain control loop picks it up transparently.
// =============================================================================

void TaskSmartBrain(void* pvParameters);
