#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ui_manager.h"

void TaskMasterComms(void * pvParameters) {
    // אתחול Serial1 (פינים 18,19 במחבר J2)
    Serial1.begin(9600, SERIAL_8N1, MASTER_RX_PIN, MASTER_TX_PIN);
    Serial.println("Master Comms Started - Waiting for Slave...");

    for(;;) {
        // 1. שליחת בקשה
        Serial1.println("GET_DATA");

        // 2. המתנה לתשובה
        unsigned long start = millis();
        String jsonResponse = "";
        bool received = false;

        // מחכים עד 500ms לתשובה
        while (millis() - start < 500) {
            if (Serial1.available()) {
                jsonResponse = Serial1.readStringUntil('\n');
                received = true;
                break;
            }
            vTaskDelay(10);
        }

        // 3. פענוח JSON ועדכון מסך
        if (received && jsonResponse.length() > 0) {
            // מסמך JSON בגודל קטן מספיק להודעה שלנו
            StaticJsonDocument<300> doc;
            DeserializationError error = deserializeJson(doc, jsonResponse);

            if (!error) {
                // שליפת נתונים
                float t1 = doc["t1"];
                float t2 = doc["t2"];
                float t3 = doc["t3"];
                float flow = doc["flow"];

                // עדכון המסך
                UI_UpdateTemp(0, t1);
                UI_UpdateTemp(1, t2);
                UI_UpdateTemp(2, t3);
                UI_UpdateFlow(flow);
                
                // Serial.println("Data Updated!"); // לדיבוג
            } else {
                Serial.print("JSON Error: ");
                Serial.println(error.c_str());
            }
        }
        
        // מבקשים נתונים פעם בשנייה
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}