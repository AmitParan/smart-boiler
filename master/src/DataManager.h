#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

class DataManager {
public:
    static void init();
    
    // WiFi credential management
    static bool saveWiFiCredentials(const char* ssid, const char* password);
    static bool loadWiFiCredentials(String& ssid, String& password);
    static bool WiFiCredentialsExist();
    static bool deleteWiFiCredentials();
    
    // User settings
    static bool saveSetting(const char* key, const char* value);
    static bool saveSetting(const char* key, float value);
    static bool saveSetting(const char* key, int value);
    
    static bool loadSetting(const char* key, String& value);
    static bool loadSetting(const char* key, float& value);
    static bool loadSetting(const char* key, int& value);
    
    // Sensor data logging
    static bool logSensorData(float temp, float humidity, float pressure);
    static bool getLastSensorData(float& temp, float& humidity, float& pressure);
    static void clearOldLogs();

    // Water usage tracking (Stats page): 24 hourly buckets for one day
    static bool saveWaterUsage(int dayOfYear, const float* hourlyLiters, const float* hourlyTemps, int hours);
    static bool loadWaterUsage(int& dayOfYear, float* hourlyLiters, float* hourlyTemps, int hours);

    // Smart-preheat learning histogram (SmartPreheat brain)
    static bool savePreheat(const uint16_t* counts, int slots, uint16_t total, uint16_t days, int16_t lastYday);
    static bool loadPreheat(uint16_t* counts, int slots, uint16_t& total, uint16_t& days, int16_t& lastYday);

    // App mode persistence (DEMO / REALTIME survives reboot)
    static bool saveAppMode(uint8_t mode);
    static bool loadAppMode(uint8_t& mode);
    
    // Utility
    static void printStorageInfo();
    static bool deleteFile(const char* filename);
};

#endif

