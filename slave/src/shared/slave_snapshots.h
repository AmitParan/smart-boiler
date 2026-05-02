#ifndef SLAVE_SNAPSHOTS_H
#define SLAVE_SNAPSHOTS_H

#include <Arduino.h>
#include <stdint.h>

// Snapshot of all sensor values produced by the sensor tasks and consumed by
// PLC/status/safety code.
struct SensorSnapshot {
    float tempsC[3];       // [0]=tank, [1]=boiler outlet, [2]=boost outlet
    float flowLpm;
    float currentRmsA;
    float powerW;
    TickType_t updatedAtTick;
    bool valid;
};

// Snapshot of the latest command received from the master.
struct CommandSnapshot {
    uint8_t pwmInternal;
    uint8_t pwmBoost;
    uint8_t flags;
    TickType_t receivedAtTick;
    bool valid;
};

#endif // SLAVE_SNAPSHOTS_H
