#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ui_manager.h"

void TaskMasterComms(void * pvParameters) {
    // Initialize Serial1 (pins 10,17 on connector J2)
    Serial1.begin(9600, SERIAL_8N1, MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.println("Master Comms Started - Waiting for Slave...");

    for(;;) {
        // 1. Send request
        Serial1.println("GET_DATA");

        // 2. Wait for response
        unsigned long start = millis();
        String jsonResponse = "";
        bool received = false;

        // Wait up to 500ms for response
        while (millis() - start < 500) {
            if (Serial1.available()) {
                jsonResponse = Serial1.readStringUntil('\n');
                received = true;
                break;
            }
            vTaskDelay(10);
        }

        // 3. Decode JSON and update display
        if (received && jsonResponse.length() > 0) {
            // Debug: Print raw received data
            Serial.print("Received: [");
            Serial.print(jsonResponse);
            Serial.println("]");
            
            // JSON document size small enough for our message
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, jsonResponse);

            if (!error) {
                // Extract data
                float t1 = doc["t1"];
                float t2 = doc["t2"];
                float t3 = doc["t3"];
                float flow = doc["flow"];

                // Update display - use main water temperature (t1)
                UI_UpdateWaterTemp(t1);
                
                // You can also display heating status based on temp rise
                // UI_SetHeatingStatus(true/false);
                
                // Serial.println("Data Updated!"); // for debugging
            } else {
                Serial.print("JSON Error: ");
                Serial.println(error.c_str());
            }
        }
        
        // Request data once per second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}