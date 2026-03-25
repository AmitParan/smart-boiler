#include "plc_comms.h"
#include "config.h"
#include "shared_data.h"

// Using Serial2 for KQ330 PLC communication
// RX: 16, TX: 17, Baud: 9600

// State machine for packet reception
enum PacketState {
    WAIT_START,
    READ_LENGTH,
    READ_COMMAND,
    READ_DATA,
    READ_CHECKSUM
};

static PacketState rxState = WAIT_START;
static PLCPacket currentPacket;
static unsigned long lastByteTime = 0;
static const unsigned long PACKET_TIMEOUT = 100; // 100ms timeout between bytes

/**
 * Initialize PLC communication
 * Sets up Serial2 with RX=16, TX=17 at 9600 baud
 */
void PLC_Init() {
    Serial2.begin(9600, SERIAL_8N1, 16, 17);
    Serial.println("PLC Communication initialized on Serial2 (RX:16, TX:17, 9600 baud)");
}

/**
 * Calculate checksum for packet validation
 * Simple summation of all bytes (length + command + data)
 */
byte calculateChecksum(byte length, byte command, byte data) {
    return (length + command + data) & 0xFF;
}

/**
 * Send a packet to the KQ330 PLC
 * @param cmd Command byte
 * @param data Data byte
 * 
 * Packet format: [0xAA][LENGTH][COMMAND][DATA][CHECKSUM]
 */
void PLC_SendPacket(byte cmd, byte data) {
    PLCPacket packet;
    
    packet.startByte = PLC_START_BYTE;
    packet.length = 2; // Command + Data
    packet.command = cmd;
    packet.data = data;
    packet.checksum = calculateChecksum(packet.length, packet.command, packet.data);
    
    // Send packet bytes
    Serial2.write(packet.startByte);
    delay(5); // Small delay for half-duplex
    Serial2.write(packet.length);
    delay(5);
    Serial2.write(packet.command);
    delay(5);
    Serial2.write(packet.data);
    delay(5);
    Serial2.write(packet.checksum);
    
    // Additional delay for noisy power line (~100bps effective rate)
    delay(50);
    
    // Debug output
    Serial.printf("PLC TX: [0x%02X][%d][0x%02X][0x%02X][0x%02X]\n", 
                  packet.startByte, packet.length, packet.command, 
                  packet.data, packet.checksum);
}

/**
 * Non-blocking packet receiver with noise filtering
 * Call this repeatedly in loop() or from a task
 * 
 * @return true if a valid packet was received and processed
 */
bool PLC_ReceivePacket() {
    // Timeout protection: Reset state if too much time passed between bytes
    if (rxState != WAIT_START && (millis() - lastByteTime > PACKET_TIMEOUT)) {
        Serial.println("PLC RX: Timeout - resetting state");
        rxState = WAIT_START;
    }
    
    // Process incoming bytes
    while (Serial2.available()) {
        byte inByte = Serial2.read();
        lastByteTime = millis();
        
        switch (rxState) {
            case WAIT_START:
                // Look for start byte, ignore noise
                if (inByte == PLC_START_BYTE) {
                    currentPacket.startByte = inByte;
                    rxState = READ_LENGTH;
                    Serial.println("PLC RX: Start byte detected");
                } else {
                    // Noise filtered out
                    Serial.printf("PLC RX: Noise filtered (0x%02X)\n", inByte);
                }
                break;
                
            case READ_LENGTH:
                currentPacket.length = inByte;
                // Sanity check on length
                if (currentPacket.length > PLC_MAX_PAYLOAD || currentPacket.length < 2) {
                    Serial.printf("PLC RX: Invalid length %d - resetting\n", currentPacket.length);
                    rxState = WAIT_START;
                } else {
                    rxState = READ_COMMAND;
                }
                break;
                
            case READ_COMMAND:
                currentPacket.command = inByte;
                rxState = READ_DATA;
                break;
                
            case READ_DATA:
                currentPacket.data = inByte;
                rxState = READ_CHECKSUM;
                break;
                
            case READ_CHECKSUM:
                currentPacket.checksum = inByte;
                
                // Validate checksum
                byte expectedChecksum = calculateChecksum(currentPacket.length, 
                                                         currentPacket.command, 
                                                         currentPacket.data);
                
                if (currentPacket.checksum == expectedChecksum) {
                    // Valid packet received!
                    Serial.printf("PLC RX: Valid packet [CMD:0x%02X][DATA:0x%02X]\n", 
                                 currentPacket.command, currentPacket.data);
                    
                    // Process the command
                    PLC_ProcessCommand(currentPacket.command, currentPacket.data);
                    
                    rxState = WAIT_START;
                    return true;
                } else {
                    // Checksum mismatch - discard packet
                    Serial.printf("PLC RX: Checksum error (Expected:0x%02X, Got:0x%02X) - packet discarded\n", 
                                 expectedChecksum, currentPacket.checksum);
                    rxState = WAIT_START;
                }
                break;
        }
    }
    
    return false;
}

/**
 * Process received commands from PLC
 * Add your custom command handlers here
 */
void PLC_ProcessCommand(byte cmd, byte data) {
    switch (cmd) {
        case CMD_LED_ON:
            Serial.println("PLC CMD: LED ON");
            // Add your LED control code here
            // digitalWrite(LED_PIN, HIGH);
            
            // Send acknowledgment
            PLC_SendPacket(CMD_ACK, cmd);
            break;
            
        case CMD_LED_OFF:
            Serial.println("PLC CMD: LED OFF");
            // digitalWrite(LED_PIN, LOW);
            PLC_SendPacket(CMD_ACK, cmd);
            break;
            
        case CMD_SET_RELAY:
            Serial.printf("PLC CMD: Set Relay to %d\n", data);
            digitalWrite(PIN_SSR, data ? HIGH : LOW);
            PLC_SendPacket(CMD_ACK, cmd);
            break;
            
        case CMD_GET_TEMP:
            Serial.printf("PLC CMD: Get Temperature sensor %d\n", data);
            // Send temperature data back
            if (data < 3) {
                byte tempByte = (byte)constrain(temps[data], 0, 255);
                PLC_SendPacket(CMD_GET_TEMP, tempByte);
            }
            break;
            
        case CMD_GET_FLOW:
            Serial.println("PLC CMD: Get Flow");
            // Send flow data back
            byte flowByte = (byte)constrain(current_flow, 0, 255);
            PLC_SendPacket(CMD_GET_FLOW, flowByte);
            break;
            
        case CMD_ACK:
            Serial.println("PLC CMD: ACK received");
            break;
            
        default:
            Serial.printf("PLC CMD: Unknown command 0x%02X\n", cmd);
            break;
    }
}
