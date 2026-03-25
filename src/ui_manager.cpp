#include "ui_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "config.h"
#include "DataManager.h"

// OpenWeatherMap API - Get your free API key from https://openweathermap.org/api
#define WEATHER_API_KEY "7401fcd981349ea54d8cd0cbc98f38d9"  // Replace with your API key
#define WEATHER_CITY "Tel Aviv"  // Change to your city
#define WEATHER_COUNTRY_CODE "IL"  // Change to your country code
#define WEATHER_UPDATE_INTERVAL 600000  // Update every 10 minutes
#define SETUP_AP_SSID "Boiler-Setup"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// UI Elements
lv_obj_t* lbl_water_temp = NULL;
lv_obj_t* lbl_target_temp = NULL;
lv_obj_t* lbl_weather_temp = NULL;
lv_obj_t* lbl_weather_icon = NULL;
lv_obj_t* lbl_time = NULL;
lv_obj_t* lbl_date = NULL;
lv_obj_t* btn_power = NULL;
lv_obj_t* lbl_power_status = NULL;
lv_obj_t* led_heating = NULL;
lv_obj_t* btn_timer = NULL;
lv_obj_t* lbl_timer_status = NULL;
lv_obj_t* btn_temp_up = NULL;
lv_obj_t* btn_temp_down = NULL;

// WiFi UI Elements
lv_obj_t* lbl_wifi_icon = NULL;
lv_obj_t* wifi_modal = NULL;
lv_obj_t* wifi_list = NULL;
lv_obj_t* password_panel = NULL;
lv_obj_t* password_textarea = NULL;
lv_obj_t* keyboard = NULL;
lv_obj_t* save_password_checkbox = NULL;

// Screensaver Elements
lv_obj_t* screensaver = NULL;
lv_obj_t* screensaver_time = NULL;
lv_obj_t* screensaver_date = NULL;
unsigned long last_touch_time = 0;
#define SCREENSAVER_TIMEOUT 60000  // 1 minute of inactivity

bool boiler_state = false;
bool timer_enabled = false;
int target_temperature = 60;
bool wifi_connected = false;
char selected_network[64] = "";
String scanned_networks[20];
int num_networks = 0;
unsigned long last_weather_update = 0;

static bool stationConnected() {
    return WiFi.status() == WL_CONNECTED;
}

static void disableScroll(lv_obj_t* obj) {
    if (obj == NULL) {
        return;
    }

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void restoreSetupApIfNeeded() {
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPSSID() != String(SETUP_AP_SSID)) {
        WiFi.softAP(SETUP_AP_SSID, "12345678");
    }
}

static int performWifiScan() {
    WiFi.scanDelete();
    WiFi.setSleep(false);
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_STA);
    delay(750);

    int found_networks = WiFi.scanNetworks(false, true);
    Serial.printf("WiFi scan attempt result: %d\n", found_networks);
    return found_networks;
}

static int scanForNetworks() {
    bool was_connected = stationConnected();
    String current_ssid = was_connected ? WiFi.SSID() : String();
    String current_psk = was_connected ? WiFi.psk() : String();

    int found_networks = performWifiScan();
    if (found_networks < 0) {
        Serial.println("First WiFi scan failed, retrying after full radio reset");
        found_networks = performWifiScan();
    }
    Serial.printf("Final WiFi scan result: %d\n", found_networks);

    if (was_connected) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(current_ssid.c_str(), current_psk.c_str());
    } else {
        restoreSetupApIfNeeded();
    }

    return found_networks;
}

// --- Event Handlers ---
static void screen_touched_cb(lv_event_t* e) {
    last_touch_time = millis();
    
    // Hide screensaver if active
    if(screensaver != NULL && lv_obj_is_visible(screensaver)) {
        lv_obj_add_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
    }
}

static void power_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    boiler_state = !boiler_state;
    
    if(boiler_state) {
        lv_obj_set_style_bg_color(btn_power, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_label_set_text(lbl_power_status, LV_SYMBOL_POWER " ON");
    } else {
        lv_obj_set_style_bg_color(btn_power, lv_palette_main(LV_PALETTE_RED), 0);
        lv_label_set_text(lbl_power_status, LV_SYMBOL_POWER " OFF");
    }
    
    Serial.printf("Boiler power: %s\n", boiler_state ? "ON" : "OFF");
}

static void timer_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    timer_enabled = !timer_enabled;
    
    if(timer_enabled) {
        lv_obj_set_style_bg_color(btn_timer, lv_palette_main(LV_PALETTE_BLUE), 0);
        lv_label_set_text(lbl_timer_status, LV_SYMBOL_LOOP " AUTO");
    } else {
        lv_obj_set_style_bg_color(btn_timer, lv_color_hex(0x555555), 0);
        lv_label_set_text(lbl_timer_status, LV_SYMBOL_LOOP " MANUAL");
    }
    
    Serial.printf("Timer mode: %s\n", timer_enabled ? "AUTO" : "MANUAL");
}

static void temp_up_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if(target_temperature < 80) {
        target_temperature += 5;
        char buf[16];
        sprintf(buf, "%d°", target_temperature);
        lv_label_set_text(lbl_target_temp, buf);
        Serial.printf("Target temp: %d\n", target_temperature);
    }
}

static void temp_down_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if(target_temperature > 30) {
        target_temperature -= 5;
        char buf[16];
        sprintf(buf, "%d°", target_temperature);
        lv_label_set_text(lbl_target_temp, buf);
        Serial.printf("Target temp: %d\n", target_temperature);
    }
}

// --- WiFi Event Handlers ---
static void close_wifi_modal(lv_event_t* e) {
    if(wifi_modal != NULL) {
        lv_obj_del(wifi_modal);
        wifi_modal = NULL;
        wifi_list = NULL;
        password_panel = NULL;
        password_textarea = NULL;
        keyboard = NULL;
        save_password_checkbox = NULL;
    }
}

static void wifi_connect_btn_event_cb(lv_event_t* e) {
    // Get password from textarea
    const char* password = lv_textarea_get_text(password_textarea);
    
    Serial.printf("Attempting to connect to: %s\n", selected_network);
    
    // Disconnect if already connected
    if(WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true, true);
        delay(100);
    }
    
    // Attempt connection
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(selected_network, password);
    
    // Wait for connection (with timeout)
    int timeout = 20; // 20 seconds
    while(WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        WiFi.softAPdisconnect(true);
        
        // Update top bar icon to green
        if(lbl_wifi_icon != NULL) {
            lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
        }
        
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        
        // Save credentials if checkbox is checked
        if(save_password_checkbox != NULL && lv_obj_has_state(save_password_checkbox, LV_STATE_CHECKED)) {
            if(DataManager::saveWiFiCredentials(selected_network, password)) {
                Serial.println("WiFi credentials saved to device!");
            }
        }
    } else {
        wifi_connected = false;
        WiFi.mode(WIFI_AP_STA);
        
        // Keep icon red
        if(lbl_wifi_icon != NULL) {
            lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
        }
        
        Serial.println("\nWiFi connection failed!");
        if (WiFi.softAPSSID() != String(SETUP_AP_SSID)) {
            WiFi.softAP(SETUP_AP_SSID, "12345678");
        }
    }
    
    // Close modal
    close_wifi_modal(e);
}

static void show_password_entry(const char* network_name) {
    if(wifi_modal == NULL) return;
    
    strncpy(selected_network, network_name, sizeof(selected_network) - 1);
    
    // Clear modal content
    lv_obj_clean(wifi_modal);
    
    // Title
    lv_obj_t * title = lv_label_create(wifi_modal);
    lv_label_set_text_fmt(title, "Connect to: %s", network_name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Password panel
    password_panel = lv_obj_create(wifi_modal);
    disableScroll(password_panel);
    lv_obj_set_size(password_panel, 500, 350);
    lv_obj_align(password_panel, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(password_panel, lv_color_white(), 0);
    lv_obj_set_style_radius(password_panel, 15, 0);
    lv_obj_set_style_border_width(password_panel, 0, 0);
    
    // Password label
    lv_obj_t * pwd_label = lv_label_create(password_panel);
    lv_label_set_text(pwd_label, "Password:");
    lv_obj_set_style_text_font(pwd_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_label, LV_ALIGN_TOP_LEFT, 20, 10);
    
    // Password textarea
    password_textarea = lv_textarea_create(password_panel);
    lv_obj_set_size(password_textarea, 460, 50);
    lv_obj_align(password_textarea, LV_ALIGN_TOP_MID, 0, 40);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_textarea_set_placeholder_text(password_textarea, "Enter password");
    lv_obj_set_style_text_font(password_textarea, &lv_font_montserrat_16, 0);
    
    // Keyboard
    keyboard = lv_keyboard_create(password_panel);
    lv_obj_set_size(keyboard, 460, 200);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, password_textarea);
    
    // Save password checkbox
    lv_obj_t * checkbox_cont = lv_obj_create(password_panel);
    disableScroll(checkbox_cont);
    lv_obj_set_size(checkbox_cont, 460, 30);
    lv_obj_align(checkbox_cont, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(checkbox_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(checkbox_cont, 0, 0);
    lv_obj_set_flex_flow(checkbox_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(checkbox_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    save_password_checkbox = lv_checkbox_create(checkbox_cont);
    lv_checkbox_set_text(save_password_checkbox, "Save to device");
    lv_obj_set_style_text_font(save_password_checkbox, &lv_font_montserrat_14, 0);
    
    // Button container
    lv_obj_t * btn_cont = lv_obj_create(wifi_modal);
    disableScroll(btn_cont);
    lv_obj_set_size(btn_cont, 500, 60);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Cancel button
    lv_obj_t * btn_cancel = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_cancel, 150, 50);
    lv_obj_set_style_radius(btn_cancel, 15, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(btn_cancel, close_wifi_modal, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_cancel);
    
    // Connect button
    lv_obj_t * btn_connect = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_connect, 150, 50);
    lv_obj_set_style_radius(btn_connect, 15, 0);
    lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_connect, wifi_connect_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    lv_obj_set_style_text_font(lbl_connect, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_connect, lv_color_white(), 0);
    lv_obj_center(lbl_connect);
}

static void wifi_network_btn_event_cb(lv_event_t* e) {
    lv_obj_t * btn = lv_event_get_target(e);
    const char * network_name = (const char *)lv_event_get_user_data(e);
    show_password_entry(network_name);
}

static void show_wifi_menu() {
    if(wifi_modal != NULL) return; // Already open
    
    // Scan for WiFi networks
    Serial.println("Scanning WiFi networks...");
    num_networks = scanForNetworks();
    Serial.printf("Found %d networks\n", num_networks);
    
    // Store network names
    for(int i = 0; i < num_networks && i < 20; i++) {
        scanned_networks[i] = WiFi.SSID(i);
    }
    
    // Create modal background
    wifi_modal = lv_obj_create(lv_scr_act());
    disableScroll(wifi_modal);
    lv_obj_set_size(wifi_modal, 600, 450);
    lv_obj_center(wifi_modal);
    lv_obj_set_style_bg_color(wifi_modal, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_radius(wifi_modal, 20, 0);
    lv_obj_set_style_border_width(wifi_modal, 2, 0);
    lv_obj_set_style_border_color(wifi_modal, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_shadow_width(wifi_modal, 30, 0);
    lv_obj_set_style_shadow_opa(wifi_modal, LV_OPA_30, 0);
    
    // Title
    lv_obj_t * title = lv_label_create(wifi_modal);
    lv_label_set_text_fmt(title, "Found %d Networks", num_networks);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Network list
    wifi_list = lv_list_create(wifi_modal);
    disableScroll(wifi_list);
    lv_obj_set_size(wifi_list, 550, 320);
    lv_obj_align(wifi_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(wifi_list, 15, 0);
    
    // Add scanned networks to list
    if(num_networks <= 0) {
        const char* message = (num_networks == 0) ? "No networks found" : "WiFi scan failed";
        lv_obj_t * btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WARNING, message);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        for(int i = 0; i < num_networks && i < 20; i++) {
            // Show signal strength icon
            int rssi = WiFi.RSSI(i);
            const char* signal_icon = (rssi > -60) ? LV_SYMBOL_WIFI : 
                                     (rssi > -80) ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI;
            
            // Add network with RSSI
            char btn_text[80];
            snprintf(btn_text, sizeof(btn_text), "%s (%d dBm)", scanned_networks[i].c_str(), rssi);
            
            lv_obj_t * btn = lv_list_add_btn(wifi_list, signal_icon, btn_text);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
            lv_obj_set_height(btn, 60);
            
            // Store network name as user data
            lv_obj_add_event_cb(btn, wifi_network_btn_event_cb, LV_EVENT_CLICKED, (void*)scanned_networks[i].c_str());
        }
    }
    
    // Close button
    lv_obj_t * btn_close = lv_btn_create(wifi_modal);
    lv_obj_set_size(btn_close, 120, 45);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(btn_close, 15, 0);
    lv_obj_set_style_bg_color(btn_close, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_close, close_wifi_modal, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_close, lv_color_white(), 0);
    lv_obj_center(lbl_close);
}

static void wifi_icon_click_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    show_wifi_menu();
}

// --- Weather Fetching ---
void fetchWeather() {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot fetch weather");
        return;
    }
    
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(WEATHER_CITY) + 
                 "," + String(WEATHER_COUNTRY_CODE) + "&appid=" + String(WEATHER_API_KEY) + "&units=metric";
    
    Serial.println("Fetching weather from: " + url);
    http.begin(url);
    int httpCode = http.GET();
    
    if(httpCode == 200) {
        String payload = http.getString();
        Serial.println("Weather data: " + payload);
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if(!error) {
            float temp = doc["main"]["temp"];
            const char* description = doc["weather"][0]["main"];
            
            Serial.printf("Temperature: %.1f°C, Condition: %s\n", temp, description);
            UI_UpdateWeather(temp, description);
            last_weather_update = millis();
        } else {
            Serial.println("Failed to parse weather JSON");
        }
    } else {
        Serial.printf("Weather API error: %d\n", httpCode);
    }
    
    http.end();
}

// --- Screensaver Functions ---
void createScreensaver() {
    screensaver = lv_obj_create(lv_scr_act());
    disableScroll(screensaver);
    lv_obj_set_size(screensaver, 800, 480);
    lv_obj_set_pos(screensaver, 0, 0);
    lv_obj_set_style_bg_color(screensaver, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screensaver, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screensaver, 0, 0);
    lv_obj_clear_flag(screensaver, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screensaver, screen_touched_cb, LV_EVENT_CLICKED, NULL);
    
    // Title
    lv_obj_t * title = lv_label_create(screensaver);
    lv_label_set_text(title, "SMART BOILER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);
    
    // Time
    screensaver_time = lv_label_create(screensaver);
    lv_label_set_text(screensaver_time, "12:00");
    lv_obj_set_style_text_font(screensaver_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(screensaver_time, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(screensaver_time, LV_ALIGN_CENTER, 0, 20);
    
    // Date
    screensaver_date = lv_label_create(screensaver);
    lv_label_set_text(screensaver_date, "3.17.2026");
    lv_obj_set_style_text_font(screensaver_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(screensaver_date, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(screensaver_date, LV_ALIGN_CENTER, 0, 80);
    
    // Hint text
    lv_obj_t * hint = lv_label_create(screensaver);
    lv_label_set_text(hint, "Touch to wake");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    // Start hidden
    lv_obj_add_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
}

void checkScreensaver() {
    if(screensaver == NULL) return;
    
    unsigned long idle_time = millis() - last_touch_time;
    
    if(idle_time > SCREENSAVER_TIMEOUT && !lv_obj_is_visible(screensaver)) {
        // Show screensaver
        lv_obj_clear_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- Helper: Create compact info card ---
void create_info_card(lv_obj_t* parent, const char* title, lv_obj_t** label_ptr, const char* unit, lv_color_t color) {
    lv_obj_t * card = lv_obj_create(parent);
    disableScroll(card);
    lv_obj_set_size(card, 150, 100);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t * lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_make(100, 100, 100), 0);

    // Value
    *label_ptr = lv_label_create(card);
    lv_label_set_text(*label_ptr, "--");
    lv_obj_set_style_text_font(*label_ptr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(*label_ptr, 400, 0);
    lv_obj_set_style_text_color(*label_ptr, color, 0);
    
    // Unit
    lv_obj_t * lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, unit);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_unit, lv_palette_main(LV_PALETTE_GREY), 0);
}

void UI_Init() {
    // Initialize display hardware
    Board *board = new Board();
    board->init();
    board->begin();
    lvgl_port_init(board->getLCD(), board->getTouch());

    lvgl_port_lock(-1);
    
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF5F5F5), 0);

    // === TOP BAR ===
    lv_obj_t * top_bar = lv_obj_create(scr);
    disableScroll(top_bar);
    lv_obj_set_size(top_bar, 800, 40);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 20, 0);
    lv_obj_set_style_pad_right(top_bar, 20, 0);

    lbl_time = lv_label_create(top_bar);
    lv_label_set_text(lbl_time, "12:00");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_20, 0);

    lbl_date = lv_label_create(top_bar);
    lv_label_set_text(lbl_date, "3.17.2026");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    
    // WiFi icon
    lv_obj_t * wifi_btn = lv_btn_create(top_bar);
    disableScroll(wifi_btn);
    lv_obj_set_size(wifi_btn, 50, 30);
    lv_obj_set_style_radius(wifi_btn, 8, 0);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_icon_click_event_cb, LV_EVENT_CLICKED, NULL);
    
    lbl_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(lbl_wifi_icon);

    // === MAIN CONTENT - Single row layout ===
    lv_obj_t * main_row = lv_obj_create(scr);
    disableScroll(main_row);
    lv_obj_set_size(main_row, 800, 440);
    lv_obj_set_pos(main_row, 0, 40);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    lv_obj_set_style_pad_all(main_row, 10, 0);
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main_row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // === LEFT: Temperature Circle ===
    lv_obj_t * temp_circle = lv_obj_create(main_row);
    disableScroll(temp_circle);
    lv_obj_set_size(temp_circle, 200, 200);
    lv_obj_set_style_radius(temp_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(temp_circle, lv_color_white(), 0);
    lv_obj_set_style_border_width(temp_circle, 6, 0);
    lv_obj_set_style_border_color(temp_circle, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_flex_flow(temp_circle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(temp_circle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * lbl_temp_label = lv_label_create(temp_circle);
    lv_label_set_text(lbl_temp_label, "Water");
    lv_obj_set_style_text_font(lbl_temp_label, &lv_font_montserrat_14, 0);

    lbl_water_temp = lv_label_create(temp_circle);
    lv_label_set_text(lbl_water_temp, "--°");
    lv_obj_set_style_text_font(lbl_water_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_water_temp, lv_palette_main(LV_PALETTE_BLUE), 0);

    led_heating = lv_led_create(temp_circle);
    lv_obj_set_size(led_heating, 12, 12);
    lv_led_set_color(led_heating, lv_palette_main(LV_PALETTE_ORANGE));
    lv_led_off(led_heating);

    // === CENTER: Controls column ===
    lv_obj_t * center_col = lv_obj_create(main_row);
    disableScroll(center_col);
    lv_obj_set_size(center_col, 240, 400);
    lv_obj_set_style_bg_opa(center_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_col, 0, 0);
    lv_obj_set_flex_flow(center_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Target temp control
    lv_obj_t * target_row = lv_obj_create(center_col);
    disableScroll(target_row);
    lv_obj_set_size(target_row, 220, 70);
    lv_obj_set_style_bg_color(target_row, lv_color_white(), 0);
    lv_obj_set_style_radius(target_row, 35, 0);
    lv_obj_set_style_border_width(target_row, 0, 0);
    lv_obj_set_flex_flow(target_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(target_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_temp_down = lv_btn_create(target_row);
    disableScroll(btn_temp_down);
    lv_obj_set_size(btn_temp_down, 50, 50);
    lv_obj_set_style_radius(btn_temp_down, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_temp_down, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);
    lv_obj_add_event_cb(btn_temp_down, temp_down_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(btn_temp_down, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * lbl_minus = lv_label_create(btn_temp_down);
    lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(lbl_minus, 300, 0);
    lv_obj_center(lbl_minus);

    lv_obj_t * target_cont = lv_obj_create(target_row);
    disableScroll(target_cont);
    lv_obj_set_size(target_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(target_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(target_cont, 0, 0);
    lv_obj_set_flex_flow(target_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(target_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t * lbl_target_label = lv_label_create(target_cont);
    lv_label_set_text(lbl_target_label, "Target");
    lv_obj_set_style_text_color(lbl_target_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_target_label, &lv_font_montserrat_10, 0);
    
    lbl_target_temp = lv_label_create(target_cont);
    lv_label_set_text(lbl_target_temp, "60°");
    lv_obj_set_style_text_font(lbl_target_temp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(lbl_target_temp, 350, 0);
    lv_obj_set_style_text_color(lbl_target_temp, lv_color_hex(0x333333), 0);

    btn_temp_up = lv_btn_create(target_row);
    disableScroll(btn_temp_up);
    lv_obj_set_size(btn_temp_up, 50, 50);
    lv_obj_set_style_radius(btn_temp_up, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_temp_up, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);
    lv_obj_add_event_cb(btn_temp_up, temp_up_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(btn_temp_up, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * lbl_plus = lv_label_create(btn_temp_up);
    lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(lbl_plus, 300, 0);
    lv_obj_center(lbl_plus);

    // Weather card
    lv_obj_t * weather_card = lv_obj_create(center_col);
    disableScroll(weather_card);
    lv_obj_set_size(weather_card, 220, 80);
    lv_obj_set_style_radius(weather_card, 15, 0);
    lv_obj_set_style_bg_color(weather_card, lv_color_hex(0xFFE082), 0);
    lv_obj_set_style_border_width(weather_card, 0, 0);
    lv_obj_set_flex_flow(weather_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(weather_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lbl_weather_temp = lv_label_create(weather_card);
    lv_label_set_text(lbl_weather_temp, "--°");
    lv_obj_set_style_text_font(lbl_weather_temp, &lv_font_montserrat_24, 0);

    lbl_weather_icon = lv_label_create(weather_card);
    lv_label_set_text(lbl_weather_icon, "Loading...");
    lv_obj_set_style_text_font(lbl_weather_icon, &lv_font_montserrat_12, 0);

    // === RIGHT: Control Buttons ===
    lv_obj_t * right_col = lv_obj_create(main_row);
    disableScroll(right_col);
    lv_obj_set_size(right_col, 300, 400);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Power button
    btn_power = lv_btn_create(right_col);
    disableScroll(btn_power);
    lv_obj_set_size(btn_power, 250, 100);
    lv_obj_set_style_radius(btn_power, 20, 0);
    lv_obj_set_style_bg_color(btn_power, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_power, power_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_power_status = lv_label_create(btn_power);
    lv_label_set_text(lbl_power_status, "OFF");
    lv_obj_set_style_text_font(lbl_power_status, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_power_status, lv_color_white(), 0);
    lv_obj_center(lbl_power_status);

    // Timer button
    btn_timer = lv_btn_create(right_col);
    disableScroll(btn_timer);
    lv_obj_set_size(btn_timer, 250, 100);
    lv_obj_set_style_radius(btn_timer, 20, 0);
    lv_obj_set_style_bg_color(btn_timer, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(btn_timer, timer_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_timer_status = lv_label_create(btn_timer);
    lv_label_set_text(lbl_timer_status, "MANUAL");
    lv_obj_set_style_text_font(lbl_timer_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_timer_status, lv_color_white(), 0);
    lv_obj_center(lbl_timer_status);
    
    // Add touch event to screen to reset screensaver timer
    lv_obj_add_event_cb(scr, screen_touched_cb, LV_EVENT_PRESSED, NULL);
    
    // Create screensaver
    createScreensaver();
    
    // Initialize last touch time
    last_touch_time = millis();

    lvgl_port_unlock();
}


void UI_UpdateWaterTemp(float value) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(lbl_water_temp != NULL && value > -100) {
            char buf[16];
            sprintf(buf, "%.0f°", value);
            lv_label_set_text(lbl_water_temp, buf);
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateWeather(float temp, const char* condition) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(lbl_weather_temp != NULL) {
            char buf[16];
            sprintf(buf, "%.0f°", temp);
            lv_label_set_text(lbl_weather_temp, buf);
        }
        
        if(lbl_weather_icon != NULL && condition != NULL) {
            // Simple icon mapping
            if(strstr(condition, "sun") || strstr(condition, "clear")) {
                lv_label_set_text(lbl_weather_icon, LV_SYMBOL_IMAGE);  // Sunny
            } else if(strstr(condition, "cloud")) {
                lv_label_set_text(lbl_weather_icon, LV_SYMBOL_SETTINGS);  // Cloud placeholder
            } else if(strstr(condition, "rain")) {
                lv_label_set_text(lbl_weather_icon, LV_SYMBOL_DOWNLOAD);
            } else {
                lv_label_set_text(lbl_weather_icon, LV_SYMBOL_EYE_OPEN);
            }
        }
        
        lvgl_port_unlock();
    }
}

void UI_SetBoilerState(bool is_on) {
    boiler_state = is_on;
    
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(btn_power != NULL && lbl_power_status != NULL) {
            if(is_on) {
                lv_obj_set_style_bg_color(btn_power, lv_palette_main(LV_PALETTE_GREEN), 0);
                lv_label_set_text(lbl_power_status, LV_SYMBOL_POWER " ON");
            } else {
                lv_obj_set_style_bg_color(btn_power, lv_palette_main(LV_PALETTE_RED), 0);
                lv_label_set_text(lbl_power_status, LV_SYMBOL_POWER " OFF");
            }
        }
        lvgl_port_unlock();
    }
}

void UI_SetHeatingStatus(bool is_heating) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(led_heating != NULL) {
            if(is_heating) {
                lv_led_on(led_heating);
            } else {
                lv_led_off(led_heating);
            }
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateTime(int hour, int minute) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        char buf[16];
        sprintf(buf, "%02d:%02d", hour, minute);
        
        if(lbl_time != NULL) {
            lv_label_set_text(lbl_time, buf);
        }
        
        if(screensaver_time != NULL) {
            lv_label_set_text(screensaver_time, buf);
        }
        
        lvgl_port_unlock();
    }
}

void UI_UpdateDate(const char* date_str) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(lbl_date != NULL && date_str != NULL) {
            lv_label_set_text(lbl_date, date_str);
        }
        
        if(screensaver_date != NULL && date_str != NULL) {
            lv_label_set_text(screensaver_date, date_str);
        }
        
        lvgl_port_unlock();
    }
}

void UI_UpdateWiFiStatus() {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(lbl_wifi_icon != NULL) {
            if(WiFi.status() == WL_CONNECTED) {
                wifi_connected = true;
                lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
            } else {
                wifi_connected = false;
                lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
            }
        }
        lvgl_port_unlock();
    }
}
