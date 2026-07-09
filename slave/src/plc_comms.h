#ifndef PLC_COMMS_H
#define PLC_COMMS_H

#include <Arduino.h>
#include "boiler_protocol.h"

// ---------------------------------------------------------------------------
//  PLC communication driver
//  Hardware : KQ-330 modem on Serial2 (RX=16, TX=17, 9600 baud)
//  Protocol : Binary frames — see boiler_protocol.h
//
//  Call PLC_Init() once from TaskPLC.
//  Call PLC_ReceivePacket() every 10 ms (non-blocking).
//  Call PLC_SendStatus() once per second.
// ---------------------------------------------------------------------------

void PLC_Init();
void PLC_SendStatus();
bool PLC_ReceivePacket();   // returns true if a valid CMD packet was received
bool PLC_IsReceiving();     // true if state machine is mid-packet (don't TX now)

#endif // PLC_COMMS_H

