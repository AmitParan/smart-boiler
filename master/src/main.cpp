#include <Arduino.h>
#include "ui_manager.h"
#include "DataManager.h"
#include "SystemManagerTest.h"
#include <WiFi.h>
#include <time.h>

// Declaration of communication function (located in comms_master.cpp)
extern void TaskMasterComms(void * pvParameters);

// ---------------------------------------------------------------------------
//  DEMO TASK — simulates slave data so the UI can be tested without hardware
//  Set DEMO_MODE to 0 to disable when real slave is connected
// ---------------------------------------------------------------------------
#define DEMO_MODE 0

// ---------------------------------------------------------------------------
//  TEST MODE — runs SystemManager test bench, no slave needed
//  Set TEST_MODE 1 to verify state machine logic via serial monitor.
//  DEMO_MODE and TEST_MODE are mutually exclusive; TEST_MODE takes priority.
// ---------------------------------------------------------------------------
#define TEST_MODE 0

#if DEMO_MODE
static void TaskDemoData(void* pvParameters) {
    // Demo sequence: cold tank heating up, then shower running, then cooling
    float t_internal = 22.0f;   // start cold
    float t_boost    = 20.0f;
    float flow       = 0.0f;
    int   phase      = 0;       // 0=heating, 1=ready, 2=shower, 3=cooldown
    int   phase_tick = 0;

    for (;;) {
        phase_tick++;

        switch (phase) {
            case 0: // Heating up from 22 to 65
                t_internal += 0.5f;
                t_boost     = t_internal - 5.0f;
                flow        = 0.0f;
                if (t_internal >= 65.0f) { phase = 1; phase_tick = 0; }
                break;
            case 1: // Ready — hold for 10 seconds
                flow = 0.0f;
                if (phase_tick >= 10) { phase = 2; phase_tick = 0; }
                break;
            case 2: // Shower running — flow active, temp slowly drops
                flow        = 7.5f;
                t_internal -= 0.3f;
                t_boost     = 42.0f;
                if (phase_tick >= 15) { phase = 3; phase_tick = 0; }
                break;
            case 3: // Shower done, cooldown
                flow        = 0.0f;
                t_boost     = 20.0f;
                t_internal -= 0.1f;
                if (t_internal <= 25.0f) { phase = 0; phase_tick = 0; t_internal = 22.0f; }
                break;
        }

        UI_UpdateSensorData(t_internal, t_boost, flow, 0.0f);
        Serial.printf("[DEMO] t1=%.1f t2=%.1f flow=%.1f phase=%d\n",
                      t_internal, t_boost, flow, phase);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

namespace {
const char* kSetupApSsid = "Boiler-Setup";
const char* kSetupApPassword = "12345678";
const char* kNtpServer1 = "pool.ntp.org";
const char* kNtpServer2 = "time.nist.gov";
const char* kNtpServer3 = "time.google.com";
const char* kTimezoneIsrael = "IST-2IDT,M3.4.4/26,M10.5.0";

void startSetupAccessPoint() {
    WiFi.mode(WIFI_AP_STA);

    if (WiFi.softAP(kSetupApSsid, kSetupApPassword)) {
        Serial.printf("Setup AP started. SSID: %s, IP: %s\n",
                      kSetupApSsid,
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("Failed to start setup AP");
    }
}

bool syncTimeFromInternet() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skipping NTP sync");
        return false;
    }

    Serial.println("Synchronizing time from NTP...");
    configTime(0, 0, kNtpServer1, kNtpServer2, kNtpServer3);
    setenv("TZ", kTimezoneIsrael, 1);
    tzset();

    struct tm timeinfo;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (getLocalTime(&timeinfo, 1000)) {
            Serial.printf("Time sync OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                          timeinfo.tm_year + 1900,
                          timeinfo.tm_mon + 1,
                          timeinfo.tm_mday,
                          timeinfo.tm_hour,
                          timeinfo.tm_min,
                          timeinfo.tm_sec);
            return true;
        }
        Serial.println("Waiting for NTP time...");
    }

    Serial.println("NTP sync failed");
    return false;
}

bool updateUiClockFromSystem() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) {
        return false;
    }

    char date_buf[16];
    strftime(date_buf, sizeof(date_buf), "%d.%m.%Y", &timeinfo);
    UI_UpdateTime(timeinfo.tm_hour, timeinfo.tm_min);
    UI_UpdateDate(date_buf);
    return true;
}
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- MASTER UNIT STARTED ---");
    
    // 0. Initialize data storage (SPIFFS)
    DataManager::init();
    WiFi.persistent(false);
    WiFi.setSleep(false);

    // Keep station mode available for scanning/connecting, and AP mode
    // available as a local fallback network for first-time setup.
    WiFi.mode(WIFI_AP_STA);
    
    // 1. Initialize display and UI
    UI_Init();
    
    // 2. Try to reconnect to saved WiFi
    bool wifi_connected = false;
    String saved_ssid, saved_password;
    if(DataManager::loadWiFiCredentials(saved_ssid, saved_password)) {
        Serial.printf("Found saved WiFi: %s\n", saved_ssid.c_str());
        WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        
        // Wait for connection
        int timeout = 20;
        while(WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print(".");
            timeout--;
        }
        
        if(WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nReconnected to WiFi! IP: %s\n", WiFi.localIP().toString().c_str());
            wifi_connected = true;
            WiFi.softAPdisconnect(true);
            syncTimeFromInternet();
        } else {
            Serial.println("\nSaved WiFi reconnect failed, enabling setup AP");
            WiFi.disconnect(true, true);
            delay(200);
        }
    } else {
        Serial.println("No saved WiFi found, enabling setup AP");
        WiFi.disconnect(true, true);
        delay(200);
    }

    if (!wifi_connected) {
        startSetupAccessPoint();
    }

    // 3. Start communication task, demo task, or SystemManager test bench
#if TEST_MODE
    Serial.println("[TEST MODE] Starting SystemManager test bench");
    xTaskCreatePinnedToCore(TaskSystemManagerTest, "SMTest", 4096, NULL, 1, NULL, 1);
#elif DEMO_MODE
    Serial.println("[DEMO MODE] Starting demo data task instead of real comms");
    xTaskCreatePinnedToCore(TaskDemoData, "DemoData", 4096, NULL, 1, NULL, 1);
#else
    // Running on Core 1 to avoid interfering with future WiFi (which runs on Core 0)
    xTaskCreatePinnedToCore(TaskMasterComms, "MasterComms", 4096, NULL, 1, NULL, 1);
#endif
    
    // Example: Update time periodically (you can use RTC or NTP later)
    // UI_UpdateTime(12, 30);
    // UI_UpdateDate("Jan 10");
    
    // Example: Update weather (connect to weather API later)
    // UI_UpdateWeather(22.5, "sunny");
}

void loop() {
    // Update WiFi status every 5 seconds
    static unsigned long last_wifi_check = 0;
    if(millis() - last_wifi_check > 5000) {
        last_wifi_check = millis();
        UI_UpdateWiFiStatus();
    }

    // Keep time/date synced from the internet when WiFi is available
    static bool last_wifi_connected = false;
    static bool time_synced = false;
    static unsigned long last_ntp_sync = 0;
    bool wifi_connected = (WiFi.status() == WL_CONNECTED);

    if (wifi_connected && !last_wifi_connected) {
        time_synced = syncTimeFromInternet();
        last_ntp_sync = millis();
    }

    if (!wifi_connected) {
        time_synced = false;
    }

    if (wifi_connected && (!time_synced || millis() - last_ntp_sync > 3600000UL)) {
        time_synced = syncTimeFromInternet();
        last_ntp_sync = millis();
    }

    static unsigned long last_clock_update = 0;
    if (millis() - last_clock_update > 1000) {
        last_clock_update = millis();
        updateUiClockFromSystem();
    }

    last_wifi_connected = wifi_connected;
    
    // Fetch weather: immediately on first boot, then every 10 minutes
    static unsigned long last_weather_fetch = 0;
    static bool weather_fetched_once = false;
    if (!weather_fetched_once || millis() - last_weather_fetch > 600000UL) {
        if (WiFi.status() == WL_CONNECTED) {
            weather_fetched_once = true;
            last_weather_fetch = millis();
            fetchWeather();
        }
    }
    
    // Fetch solar forecast: once per day (on first connection, then again
    // after local midnight). Needs a synced clock to know the local day.
    static int solar_fetch_day = -1;
    if (wifi_connected && time_synced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100) && timeinfo.tm_yday != solar_fetch_day) {
            solar_fetch_day = timeinfo.tm_yday;
            fetchSolarForecast();
        }
    }

    // Screensaver disabled per user request
    // checkScreensaver();
    
    vTaskDelay(1000);
}
