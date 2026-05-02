#ifndef MASTER_SNAPSHOTS_H
#define MASTER_SNAPSHOTS_H

#include <Arduino.h>
#include "SystemManager.h"

// Data received from the slave by TaskMasterComms.
struct SensorSnapshot {
    float tempInternalC;
    float tempBoilerOutC;
    float tempBoostOutC;
    float flowLpm;
    float powerW;
    uint8_t statusByte;
    uint8_t sequence;
    TickType_t receivedAtTick;
    bool valid;
};

// Data produced by the touchscreen/UI task.
struct UiSnapshot {
    bool boilerOn;
    float targetShowerTempC;
    TickType_t updatedAtTick;
    bool valid;
};

// Data produced by TaskBrain and consumed by TaskMasterComms.
struct CommandSnapshot {
    uint8_t pwmInternal;
    uint8_t pwmBoost;
    uint8_t cmdFlags;
    BoilerState state;
    TickType_t decidedAtTick;
    bool plcConnected;
    bool valid;
};

#endif // MASTER_SNAPSHOTS_H
