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
    
    // Utility
    static void printStorageInfo();
    static bool deleteFile(const char* filename);
};

#endif
