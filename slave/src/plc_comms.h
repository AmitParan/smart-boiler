#ifndef PLC_COMMS_H
#define PLC_COMMS_H

#include <Arduino.h>

// Packet protocol constants
#define PLC_START_BYTE  0xAA
#define PLC_MAX_PAYLOAD 32

// Command definitions
#define CMD_LED_ON      0x01
#define CMD_LED_OFF     0x02
#define CMD_SET_RELAY   0x03
#define CMD_GET_TEMP    0x04
#define CMD_GET_FLOW    0x05
#define CMD_ACK         0xFF

// Packet structure
struct PLCPacket {
    byte startByte;     // 0xAA
    byte length;        // Length of payload (command + data)
    byte command;       // Command ID
    byte data;          // Data byte
    byte checksum;      // Checksum for validation
};

// Function declarations
void PLC_Init();
void PLC_SendPacket(byte cmd, byte data);
bool PLC_ReceivePacket();  // Returns true if valid packet received
void PLC_ProcessCommand(byte cmd, byte data);

#endif
