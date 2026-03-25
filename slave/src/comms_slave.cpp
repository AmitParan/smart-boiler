#include "comms_slave.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>
#include <ArduinoJson.h>

void TaskComms(void * pvParameters) {
    // Initialize Serial2 with the pins we defined
    Serial2.begin(9600, SERIAL_8N1, SLAVE_RX, SLAVE_TX);

    for(;;) {
        if (Serial2.available()) {
            String cmd = Serial2.readStringUntil('\n');
            cmd.trim();

            if (cmd == "GET_DATA") {
                // Create JSON from shared data
                StaticJsonDocument<200> doc;
                
                // Get the latest data from shared_data (validate before sending)
                doc["t1"] = (temps[0] > -100 && temps[0] < 150) ? temps[0] : 0.0;
                doc["t2"] = (temps[1] > -100 && temps[1] < 150) ? temps[1] : 0.0;
                doc["t3"] = (temps[2] > -100 && temps[2] < 150) ? temps[2] : 0.0;
                doc["flow"] = (current_flow >= 0) ? current_flow : 0.0;

                String output;
                serializeJson(doc, output);
                
                // Send to master
                Serial2.println(output);
                
                // Debug output to Serial
                Serial.print("Sent: ");
                Serial.println(output);
            }
        }
        // Quick check (don't need large delay because Serial does the waiting)
        vTaskDelay(10);
    }
}