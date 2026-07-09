#include "app_mode.h"

// Default to DEMO so the board is safe to run without real sensors connected.
volatile AppMode appMode = MODE_DEMO;

volatile float   demo_temp       = 35.0f;
volatile float   demo_flow       = 0.0f;
volatile bool    demo_ui_on      = true;
volatile bool    demo_stop_comms  = false;
volatile bool    demo_fault_sim   = false;
volatile bool    demo_solar_active = false;

