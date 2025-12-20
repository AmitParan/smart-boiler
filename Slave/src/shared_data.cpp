#include "shared_data.h"

// כאן המשתנים נוצרים באמת בזיכרון
float current_flow = 0.0;
float temps[3] = {0, 0, 0};
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;