#ifndef SLAVE_STATE_H
#define SLAVE_STATE_H

#include "slave_snapshots.h"

bool SlaveState_Init();

void SlaveState_UpdateSensors(const SensorSnapshot& snapshot);
bool SlaveState_ReadSensors(SensorSnapshot& snapshot);

void SlaveState_UpdateCommand(const CommandSnapshot& snapshot);
bool SlaveState_ReadCommand(CommandSnapshot& snapshot);

#endif // SLAVE_STATE_H
