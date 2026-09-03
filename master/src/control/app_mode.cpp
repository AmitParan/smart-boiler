#include "app_mode.h"

// Boot default is DEMO (safe without real sensors). Overwritten by
// DataManager::loadAppMode() in setup() if a persisted mode exists.
volatile AppMode appMode = MODE_DEMO;

volatile float   demo_temp       = 35.0f;
volatile float   demo_flow       = 0.0f;
volatile bool    demo_ui_on      = true;
volatile bool    demo_stop_comms  = false;
volatile bool    demo_fault_sim   = false;
volatile bool    demo_solar_active = false;

