#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>

// Variables that all Tasks can access
extern float current_flow;    // Flow rate (calculated by Flow Task)
extern float temps[3];        // Temperatures (calculated by Temp Task)

// Object for interrupt locking (critical for flow)
extern portMUX_TYPE timerMux;

#endif