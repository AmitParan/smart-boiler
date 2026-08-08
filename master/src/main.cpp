#include <Arduino.h>
#include "ui_manager.h"
#include "DataManager.h"
#include "SystemManagerTest.h"
#include "app_mode.h"
#include "demo_scenarios.h"
#include <WiFi.h>
#include <time.h>

// Declaration of communication function (located in comms_master.cpp)
extern void TaskMasterComms(void * pvParameters);

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

    // 3. Start PLC communication task (always — mode switching is runtime)
    Serial.printf("[BOOT] Starting in %s mode\n",
                  appMode == MODE_DEMO ? "DEMO" : "REALTIME");
    // PLC comms on Core 0 — completely isolated from LVGL (Core 1).
    // LVGL cannot preempt TaskMasterComms, eliminating inter-byte gaps.
    xTaskCreatePinnedToCore(TaskMasterComms,        "MasterComms",  4096, NULL, 2, NULL, 0);
    // Demo scenario runner stays on Core 1 (shares demo_* volatile vars safely)
    xTaskCreatePinnedToCore(TaskAutomatedTestBench, "TestBench",    4096, NULL, 1, NULL, 1);
    
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
    static unsigned long last_wifi_retry = 0;
    bool wifi_connected = (WiFi.status() == WL_CONNECTED);

    // setup() only tries the saved network once — the ESP32 WiFi driver
    // does not keep retrying on its own after that attempt fails. Retry
    // periodically here so a boot-time miss (router still starting up,
    // transient AP hiccup, etc.) or a later drop doesn't leave the device
    // stuck offline until someone reconnects it by hand.
    if (!wifi_connected && millis() - last_wifi_retry > 30000UL) {
        last_wifi_retry = millis();
        String saved_ssid, saved_password;
        if (DataManager::loadWiFiCredentials(saved_ssid, saved_password)) {
            Serial.printf("[WiFi] Not connected, retrying saved network: %s\n", saved_ssid.c_str());
            WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        }
    }

    if (wifi_connected && !last_wifi_connected) {
        Serial.println("[WiFi] Connected — disabling setup AP");
        WiFi.softAPdisconnect(true);
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

