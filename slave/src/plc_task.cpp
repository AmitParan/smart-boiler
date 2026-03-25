#include "plc_task.h"
#include "plc_comms.h"

/**
 * PLC Communication Task
 * Runs continuously to handle PLC packet reception and processing
 * Non-blocking operation with noise filtering
 */
void TaskPLC(void * pvParameters) {
    // Initialize PLC communication
    PLC_Init();
    
    Serial.println("PLC Task started");
    
    for(;;) {
        // Non-blocking packet reception
        // This will filter noise and only process valid packets
        PLC_ReceivePacket();
        
        // Small delay to prevent tight loop
        // The actual communication is event-driven by Serial2
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
