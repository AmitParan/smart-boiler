#include "comms_slave.h"
#include "config.h"
#include "shared_data.h"
#include <Arduino.h>
#include <ArduinoJson.h>

void TaskComms(void * pvParameters) {
    // אתחול Serial2 לפינים שהגדרנו
    Serial2.begin(9600, SERIAL_8N1, SLAVE_RX, SLAVE_TX);

    for(;;) {
        if (Serial2.available()) {
            String cmd = Serial2.readStringUntil('\n');
            cmd.trim();

            if (cmd == "GET_DATA") {
                // יצירת JSON מהנתונים המשותפים
                StaticJsonDocument<200> doc;
                
                // לוקחים את הנתונים העדכניים מ-shared_data
                doc["t1"] = temps[0];
                doc["t2"] = temps[1];
                doc["t3"] = temps[2];
                doc["flow"] = current_flow;

                String output;
                serializeJson(doc, output);
                
                // שליחה למאסטר
                Serial2.println(output);
            }
        }
        // בדיקה מהירה (לא צריך Delay גדול כי ה-Serial עושה את ההמתנה)
        vTaskDelay(10);
    }
}