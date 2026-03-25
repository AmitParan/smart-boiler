#include "shared_data.h"

// Here the variables are actually created in memory
float current_flow = 0.0;
float temps[3] = {0, 0, 0};
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;