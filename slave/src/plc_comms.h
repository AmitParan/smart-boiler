#ifndef PLC_COMMS_H
#define PLC_COMMS_H

#include <Arduino.h>
#include "boiler_protocol.h"

// ---------------------------------------------------------------------------
//  Master/slave protocol layer.
//  Protocol : Binary frames — see boiler_protocol.h
//  Transport: chosen at build time — KQ-330 power-line modem, or WiFi/UDP
//             when built with -DLINK_WIFI. See link.h; nothing here changes.
//
//  Call PLC_Init() once from TaskPLC.
//  Call PLC_ReceivePacket() every 10 ms (non-blocking).
//  Call PLC_SendStatus() once per second.
// ---------------------------------------------------------------------------

void PLC_Init();
void PLC_SendStatus();
bool PLC_ReceivePacket();   // returns true if a valid CMD packet was received

#endif // PLC_COMMS_H

