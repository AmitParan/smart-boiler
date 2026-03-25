#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Configuration placeholders - define these in your config.h or before including this file
#ifndef WIFI_SSID
#define WIFI_SSID "your_wifi_ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your_wifi_password"
#endif

#ifndef WIFI_API_KEY
#define WIFI_API_KEY "your_openweathermap_api_key"
#endif

#ifndef WIFI_CITY
#define WIFI_CITY "Tel Aviv"  // Default city for Israel
#endif

// NTP Configuration
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"

// Israel Timezone: IST (UTC+2) with IDT (UTC+3) - Daylight Saving Time
// DST starts on last Friday before March 26 at 02:00
// DST ends on last Sunday of October at 02:00
#define TIMEZONE_ISRAEL "IST-2IDT,M3.4.4/26,M10.5.0"

class NetworkManager {
private:
    bool wifiConnected = false;
    bool timeConfigured = false;
    
    // Weather data
    float currentTemp = 0.0;
    String weatherCondition = "Unknown";
    
    // Time data
    struct tm timeinfo;
    
public:
    // Initialize WiFi connection
    bool connectWiFi() {
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            return true;
        }
        
        Serial.println("Connecting to WiFi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi Connected!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            wifiConnected = true;
            return true;
        } else {
            Serial.println("\nWiFi Connection Failed!");
            wifiConnected = false;
            return false;
        }
    }
    
    // Configure NTP time sync
    bool configureTime() {
        if (!wifiConnected) {
            Serial.println("WiFi not connected. Cannot configure time.");
            return false;
        }
        
        Serial.println("Configuring time with NTP...");
        
        // Configure time with Israel timezone
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        setenv("TZ", TIMEZONE_ISRAEL, 1);
        tzset();
        
        // Wait for time to be set
        int retry = 0;
        while (!getLocalTime(&timeinfo) && retry < 10) {
            Serial.print(".");
            delay(500);
            retry++;
        }
        
        if (retry < 10) {
            Serial.println("\nTime synchronized!");
            Serial.printf("Current time: %02d:%02d:%02d\n", 
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            timeConfigured = true;
            return true;
        } else {
            Serial.println("\nFailed to sync time!");
            timeConfigured = false;
            return false;
        }
    }
    
    // Get current time
    bool getCurrentTime(int &hours, int &minutes, int &seconds) {
        if (!getLocalTime(&timeinfo)) {
            Serial.println("Failed to obtain time");
            return false;
        }
        
        hours = timeinfo.tm_hour;
        minutes = timeinfo.tm_min;
        seconds = timeinfo.tm_sec;
        return true;
    }
    
    // Get formatted time string (HH:MM)
    String getTimeString() {
        if (!getLocalTime(&timeinfo)) {
            return "--:--";
        }
        
        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        return String(timeStr);
    }
    
    // Get formatted date string
    String getDateString() {
        if (!getLocalTime(&timeinfo)) {
            return "--- --";
        }
        
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        
        char dateStr[10];
        sprintf(dateStr, "%s %02d", months[timeinfo.tm_mon], timeinfo.tm_mday);
        return String(dateStr);
    }
    
    // Fetch weather data from OpenWeatherMap
    bool fetchWeather() {
        if (!wifiConnected) {
            Serial.println("WiFi not connected. Cannot fetch weather.");
            return false;
        }
        
        HTTPClient http;
        
        // Build the URL
        String url = "http://api.openweathermap.org/data/2.5/weather?q=";
        url += WIFI_CITY;
        url += "&appid=";
        url += WIFI_API_KEY;
        url += "&units=metric";
        
        Serial.println("Fetching weather data...");
        Serial.println("URL: " + url);
        
        http.begin(url);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Weather data received");
            
            // Parse JSON
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (error) {
                Serial.print("JSON parsing failed: ");
                Serial.println(error.c_str());
                http.end();
                return false;
            }
            
            // Extract temperature
            currentTemp = doc["main"]["temp"];
            
            // Extract weather condition
            weatherCondition = doc["weather"][0]["main"].as<String>();
            
            Serial.printf("Temperature: %.1f°C\n", currentTemp);
            Serial.println("Condition: " + weatherCondition);
            
            http.end();
            return true;
        } else {
            Serial.printf("HTTP request failed, error: %d\n", httpCode);
            http.end();
            return false;
        }
    }
    
    // Main update function - fetches all internet data
    bool updateInternetData() {
        bool success = true;
        
        // Ensure WiFi is connected
        if (!connectWiFi()) {
            return false;
        }
        
        // Configure time if not done yet
        if (!timeConfigured) {
            if (!configureTime()) {
                success = false;
            }
        }
        
        // Fetch weather
        if (!fetchWeather()) {
            success = false;
        }
        
        return success;
    }
    
    // Getters for weather data
    float getTemperature() const {
        return currentTemp;
    }
    
    String getWeatherCondition() const {
        return weatherCondition;
    }
    
    // Check WiFi connection status
    bool isWiFiConnected() const {
        return (WiFi.status() == WL_CONNECTED);
    }
    
    // Get WiFi signal strength (RSSI)
    int getWiFiRSSI() const {
        if (isWiFiConnected()) {
            return WiFi.RSSI();
        }
        return 0;
    }
};

#endif // NETWORK_MANAGER_H
