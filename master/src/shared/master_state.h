#ifndef MASTER_STATE_H
#define MASTER_STATE_H

#include "shared/master_snapshots.h"

bool MasterState_Init();

void MasterState_PublishSensorSnapshot(const SensorSnapshot& snapshot);
bool MasterState_ReadSensorSnapshot(SensorSnapshot& snapshot);

void MasterState_PublishUiSnapshot(const UiSnapshot& snapshot);
bool MasterState_ReadUiSnapshot(UiSnapshot& snapshot);

void MasterState_PublishCommandSnapshot(const CommandSnapshot& snapshot);
bool MasterState_ReadCommandSnapshot(CommandSnapshot& snapshot);

#endif // MASTER_STATE_H
