#include "DataManager.h"
#include <SPIFFS.h>

#define WIFI_FILE "/wifi.json"
#define SETTINGS_FILE "/settings.json"
#define LOGS_FILE "/logs.json"
#define MAX_LOG_ENTRIES 50  // Keep it small

void DataManager::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    Serial.println("SPIFFS initialized successfully");
    printStorageInfo();
}

// ==================== WiFi Credentials ====================
bool DataManager::saveWiFiCredentials(const char* ssid, const char* password) {
    DynamicJsonDocument doc(256);
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["timestamp"] = millis();
    
    File file = SPIFFS.open(WIFI_FILE, "w");
    if (!file) {
        Serial.println("Failed to open WiFi file for writing");
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.printf("WiFi credentials saved for SSID: %s\n", ssid);
    return true;
}

bool DataManager::loadWiFiCredentials(String& ssid, String& password) {
    if (!SPIFFS.exists(WIFI_FILE)) {
        Serial.println("WiFi credentials not found");
        return false;
    }
    
    File file = SPIFFS.open(WIFI_FILE, "r");
    if (!file) {
        Serial.println("Failed to open WiFi file for reading");
        return false;
    }
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("Failed to parse WiFi JSON: %s\n", error.c_str());
        return false;
    }
    
    ssid = doc["ssid"].as<String>();
    password = doc["password"].as<String>();
    
    Serial.printf("WiFi credentials loaded: %s\n", ssid.c_str());
    return true;
}

bool DataManager::WiFiCredentialsExist() {
    return SPIFFS.exists(WIFI_FILE);
}

bool DataManager::deleteWiFiCredentials() {
    if (SPIFFS.remove(WIFI_FILE)) {
        Serial.println("WiFi credentials deleted");
        return true;
    }
    return false;
}

// ==================== User Settings ====================
bool DataManager::saveSetting(const char* key, const char* value) {
    DynamicJsonDocument doc(1024);
    
    // Load existing settings
    if (SPIFFS.exists(SETTINGS_FILE)) {
        File file = SPIFFS.open(SETTINGS_FILE, "r");
        deserializeJson(doc, file);
        file.close();
    }
    
    // Update setting
    doc[key] = value;
    
    // Save back
    File file = SPIFFS.open(SETTINGS_FILE, "w");
    if (!file) {
        Serial.println("Failed to open settings file for writing");
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.printf("Setting saved: %s = %s\n", key, value);
    return true;
}

bool DataManager::saveSetting(const char* key, float value) {
    char buf[16];
    sprintf(buf, "%.2f", value);
    return saveSetting(key, buf);
}

bool DataManager::saveSetting(const char* key, int value) {
    char buf[16];
    sprintf(buf, "%d", value);
    return saveSetting(key, buf);
}

bool DataManager::loadSetting(const char* key, String& value) {
    if (!SPIFFS.exists(SETTINGS_FILE)) {
        return false;
    }
    
    File file = SPIFFS.open(SETTINGS_FILE, "r");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error || !doc.containsKey(key)) {
        return false;
    }
    
    value = doc[key].as<String>();
    return true;
}

bool DataManager::loadSetting(const char* key, float& value) {
    if (!SPIFFS.exists(SETTINGS_FILE)) {
        return false;
    }
    
    File file = SPIFFS.open(SETTINGS_FILE, "r");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error || !doc.containsKey(key)) {
        return false;
    }
    
    value = doc[key].as<float>();
    return true;
}

bool DataManager::loadSetting(const char* key, int& value) {
    if (!SPIFFS.exists(SETTINGS_FILE)) {
        return false;
    }
    
    File file = SPIFFS.open(SETTINGS_FILE, "r");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error || !doc.containsKey(key)) {
        return false;
    }
    
    value = doc[key].as<int>();
    return true;
}

// ==================== Sensor Data Logging ====================
bool DataManager::logSensorData(float temp, float humidity, float pressure) {
    DynamicJsonDocument doc(4096);
    
    // Load existing logs
    if (SPIFFS.exists(LOGS_FILE)) {
        File file = SPIFFS.open(LOGS_FILE, "r");
        deserializeJson(doc, file);
        file.close();
    }
    
    // Get or create logs array
    if (!doc.containsKey("logs")) {
        doc["logs"] = JsonArray();
    }
    JsonArray logs = doc["logs"].as<JsonArray>();
    
    // Add new entry
    JsonObject entry = logs.createNestedObject();
    entry["temp"] = temp;
    entry["humidity"] = humidity;
    entry["pressure"] = pressure;
    entry["timestamp"] = millis();
    
    // Keep only last MAX_LOG_ENTRIES
    if (logs.size() > MAX_LOG_ENTRIES) {
        logs.remove(0);
    }
    
    // Save logs
    File file = SPIFFS.open(LOGS_FILE, "w");
    if (!file) {
        Serial.println("Failed to open logs file for writing");
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.printf("Sensor data logged: T=%.1f°C H=%.1f%% P=%.0fhPa\n", temp, humidity, pressure);
    return true;
}

bool DataManager::getLastSensorData(float& temp, float& humidity, float& pressure) {
    if (!SPIFFS.exists(LOGS_FILE)) {
        return false;
    }
    
    File file = SPIFFS.open(LOGS_FILE, "r");
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error || !doc.containsKey("logs")) {
        return false;
    }
    
    JsonArray logs = doc["logs"].as<JsonArray>();
    if (logs.size() == 0) {
        return false;
    }
    
    JsonObject lastEntry = logs[logs.size() - 1];
    temp = lastEntry["temp"];
    humidity = lastEntry["humidity"];
    pressure = lastEntry["pressure"];
    
    return true;
}

void DataManager::clearOldLogs() {
    if (SPIFFS.remove(LOGS_FILE)) {
        Serial.println("Old logs cleared");
    }
}

// ==================== Utility ====================
void DataManager::printStorageInfo() {
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    
    Serial.printf("SPIFFS Total: %u bytes, Used: %u bytes, Free: %u bytes\n",
                  totalBytes, usedBytes, totalBytes - usedBytes);
}

bool DataManager::deleteFile(const char* filename) {
    if (SPIFFS.remove(filename)) {
        Serial.printf("File deleted: %s\n", filename);
        return true;
    }
    Serial.printf("Failed to delete: %s\n", filename);
    return false;
}
