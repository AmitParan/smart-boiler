#include "app_mode.h"

// Default to DEMO so the board is safe to run without real sensors connected.
volatile AppMode appMode    = APP_MODE_DEMO;

// Initial demo values — overwritten by the scenario task when running scenarios.
volatile float   demo_temp  = 35.0f;   // below TARGET_TANK_TEMP → STATE_HEATING_TANK
volatile float   demo_flow  = 0.0f;
volatile bool    demo_ui_on = true;
