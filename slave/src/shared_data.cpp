#include "shared_data.h"

// Sensor readings
float temps[3]       = {0.0f, 0.0f, 0.0f};
float current_flow   = 0.0f;
float current_rms    = 0.0f;
float power_watts    = 0.0f;

// SSR state flags
volatile bool    internal_ssr_on  = false;
volatile bool    boost_ssr_on     = false;

// Commands from master
volatile uint8_t cmd_pwm_internal = 0u;
volatile uint8_t cmd_pwm_boost    = 0u;
volatile uint8_t cmd_flags        = 0u;

// Safety
volatile bool    system_fault     = false;

// FreeRTOS mutexes (created in main.cpp setup(), before tasks start)
SemaphoreHandle_t mutex_temps   = nullptr;
SemaphoreHandle_t mutex_flow    = nullptr;
SemaphoreHandle_t mutex_current = nullptr;
SemaphoreHandle_t mutex_cmd     = nullptr;

// Spinlock for flow pulse counter ISR
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
