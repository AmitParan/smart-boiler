#include <Arduino.h>
#include "ui_manager.h"

// הצהרה על פונקציית התקשורת (נמצאת בקובץ comms_master.cpp)
extern void TaskMasterComms(void * pvParameters);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- MASTER UNIT STARTED ---");
    
    // 1. אתחול מסך ועיצוב
    UI_Init();

    // 2. הפעלת משימת התקשורת
    // מריצים על Core 1 כדי לא להפריע ל-WiFi בעתיד (שרץ על Core 0)
    xTaskCreatePinnedToCore(TaskMasterComms, "MasterComms", 4096, NULL, 1, NULL, 1);
}

void loop() {
    // ה-Loop הראשי נשאר ריק. הכל קורה ב-Tasks.
    vTaskDelay(1000);
}