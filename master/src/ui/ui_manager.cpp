// ui_manager.cpp  — Consumer-First Redesign
// Screen: 800 x 480  |  LVGL v8  |  Montserrat fonts

#include "ui_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "config.h"
#include "storage/DataManager.h"
#include "boiler_protocol.h"
#include "shared/master_state.h"
#include "task_config.h"
#include "brain/brain_settings.h"
#include "brain/event_log.h"

#define WEATHER_API_KEY      "7a1028898a2cdcc08a58a8109fb061e4"  // local only - do not commit
#define WEATHER_CITY         "Tel Aviv"
#define WEATHER_COUNTRY_CODE "IL"
#define SETUP_AP_SSID        "Boiler-Setup"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// ---------------------------------------------------------------------------
//  Colour palette
// ---------------------------------------------------------------------------
#define CLR_BG      lv_color_hex(0xF0F4F8)  // light grey background
#define CLR_TOPBAR  lv_color_hex(0x1565C0)  // deep blue top bar
#define CLR_ACCENT  lv_color_hex(0x1565C0)  // accent (buttons, ring)
#define CLR_ON      lv_color_hex(0x2E7D32)  // boiler ON  — green
#define CLR_OFF     lv_color_hex(0xC62828)  // boiler OFF — red
#define CLR_TEXT    lv_color_hex(0x1A1A2E)  // primary text
#define CLR_SUBTEXT lv_color_hex(0x7B8794)  // secondary text
#define CLR_READY   lv_color_hex(0x2E7D32)  // shower ready
#define CLR_WAITING lv_color_hex(0xE65100)  // shower not ready

// ---------------------------------------------------------------------------
//  Widget pointers
// ---------------------------------------------------------------------------
lv_obj_t* lbl_water_temp         = NULL;
lv_obj_t* lbl_target_temp        = NULL;
lv_obj_t* lbl_weather_temp       = NULL;
lv_obj_t* lbl_weather_icon       = NULL;
lv_obj_t* lbl_time               = NULL;
lv_obj_t* lbl_date               = NULL;
lv_obj_t* btn_power              = NULL;
lv_obj_t* lbl_power_status       = NULL;
lv_obj_t* led_heating            = NULL;
lv_obj_t* btn_timer              = NULL;   // unused in consumer view
lv_obj_t* lbl_timer_status       = NULL;  // unused in consumer view
lv_obj_t* btn_temp_up            = NULL;
lv_obj_t* btn_temp_down          = NULL;

lv_obj_t* lbl_wifi_icon          = NULL;
lv_obj_t* wifi_modal             = NULL;
lv_obj_t* wifi_list              = NULL;
lv_obj_t* password_panel         = NULL;
lv_obj_t* password_textarea      = NULL;
lv_obj_t* keyboard               = NULL;
lv_obj_t* save_password_checkbox = NULL;

lv_obj_t* screensaver            = NULL;
lv_obj_t* screensaver_time       = NULL;
lv_obj_t* screensaver_date       = NULL;
unsigned long last_touch_time    = 0;
#define SCREENSAVER_TIMEOUT 60000

// Kept for API compatibility — not shown in consumer view
lv_obj_t* lbl_boost_temp   = NULL;
lv_obj_t* lbl_flow_rate    = NULL;
lv_obj_t* lbl_showers      = NULL;
lv_obj_t* lbl_wait_time    = NULL;
lv_obj_t* lbl_system_mode  = NULL;
lv_obj_t* led_ssr_internal = NULL;
lv_obj_t* led_ssr_boost    = NULL;

// Consumer shower card sub-widgets
static lv_obj_t* shower_card_obj      = NULL;
static lv_obj_t* lbl_shower_readiness = NULL;
static lv_obj_t* lbl_flow_status      = NULL;  // "● Flow Active" / "○ No Flow"
static lv_obj_t* temp_circle_obj      = NULL;

// Info bar widgets
static lv_obj_t* lbl_power_val  = NULL;
static lv_obj_t* lbl_flow_val   = NULL;
static lv_obj_t* lbl_plc_status = NULL;

// Ready By modal widgets
static lv_obj_t* ready_by_modal  = NULL;
static lv_obj_t* lbl_rb_hour     = NULL;
static lv_obj_t* lbl_rb_minute   = NULL;
static int       rb_hour         = 7;
static int       rb_minute       = 0;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
bool   boiler_state       = false;
bool   timer_enabled      = false;
int    target_temperature = 60;
bool   wifi_connected     = false;
char   selected_network[64] = "";
String scanned_networks[20];
int    num_networks         = 0;
unsigned long last_weather_update = 0;
static float  s_outdoorTempC     = 20.0f;  // last fetched outdoor temp (default 20°C)

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static bool stationConnected() { return WiFi.status() == WL_CONNECTED; }

static void publishUiSnapshot() {
    UiSnapshot snapshot{};
    snapshot.boilerOn = boiler_state;
    snapshot.targetShowerTempC = (float)target_temperature;
    snapshot.updatedAtTick = xTaskGetTickCount();
    snapshot.valid = true;

    MasterState_PublishUiSnapshot(snapshot);
}

static void disableScroll(lv_obj_t* obj) {
    if (obj == NULL) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void restoreSetupApIfNeeded() {
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPSSID() != String(SETUP_AP_SSID))
        WiFi.softAP(SETUP_AP_SSID, "12345678");
}

static int performWifiScan() {
    WiFi.scanDelete();
    WiFi.setSleep(false);
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_STA);
    delay(750);
    int found = WiFi.scanNetworks(false, true);
    Serial.printf("WiFi scan result: %d\n", found);
    return found;
}

static int scanForNetworks() {
    bool   was_connected = stationConnected();
    String cur_ssid      = was_connected ? WiFi.SSID() : String();
    String cur_psk       = was_connected ? WiFi.psk()  : String();

    int found = performWifiScan();
    if (found < 0) found = performWifiScan();

    if (was_connected) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(cur_ssid.c_str(), cur_psk.c_str());
    } else {
        restoreSetupApIfNeeded();
    }
    return found;
}

// ---------------------------------------------------------------------------
//  Touch / screensaver
// ---------------------------------------------------------------------------
static void screen_touched_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (screensaver != NULL && lv_obj_is_visible(screensaver))
        lv_obj_add_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
//  Button callbacks
// ---------------------------------------------------------------------------
static void power_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    boiler_state = !boiler_state;
    if (btn_power        != NULL) lv_obj_set_style_bg_color(btn_power, boiler_state ? CLR_ON : CLR_OFF, 0);
    if (lbl_power_status != NULL) lv_label_set_text(lbl_power_status, boiler_state ? "ON" : "OFF");
    publishUiSnapshot();
    Serial.printf("Boiler: %s\n", boiler_state ? "ON" : "OFF");
}

static void temp_up_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (target_temperature < 80) {
        target_temperature += 5;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d\xc2\xb0", target_temperature);
        if (lbl_target_temp) lv_label_set_text(lbl_target_temp, buf);
        publishUiSnapshot();
    }
}

static void temp_down_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (target_temperature > 30) {
        target_temperature -= 5;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d\xc2\xb0", target_temperature);
        if (lbl_target_temp) lv_label_set_text(lbl_target_temp, buf);
        publishUiSnapshot();
    }
}

// ---------------------------------------------------------------------------
//  WiFi modal
// ---------------------------------------------------------------------------
static void close_wifi_modal(lv_event_t* e) {
    if (wifi_modal != NULL) {
        lv_obj_del(wifi_modal);
        wifi_modal = wifi_list = password_panel = NULL;
        password_textarea = keyboard = save_password_checkbox = NULL;
    }
}

static void wifi_connect_btn_event_cb(lv_event_t* e) {
    const char* password = lv_textarea_get_text(password_textarea);
    if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect(true, true); delay(100); }
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(selected_network, password);
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        WiFi.softAPdisconnect(true);
        if (lbl_wifi_icon) lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
        if (save_password_checkbox && lv_obj_has_state(save_password_checkbox, LV_STATE_CHECKED))
            DataManager::saveWiFiCredentials(selected_network, password);
    } else {
        wifi_connected = false;
        WiFi.mode(WIFI_AP_STA);
        if (lbl_wifi_icon) lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
        if (WiFi.softAPSSID() != String(SETUP_AP_SSID)) WiFi.softAP(SETUP_AP_SSID, "12345678");
    }
    close_wifi_modal(e);
}

static void show_password_entry(const char* network_name) {
    if (wifi_modal == NULL) return;
    strncpy(selected_network, network_name, sizeof(selected_network) - 1);
    lv_obj_clean(wifi_modal);

    lv_obj_t* title = lv_label_create(wifi_modal);
    lv_label_set_text_fmt(title, "Connect to: %s", network_name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    password_panel = lv_obj_create(wifi_modal);
    disableScroll(password_panel);
    lv_obj_set_size(password_panel, 500, 350);
    lv_obj_align(password_panel, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(password_panel, lv_color_white(), 0);
    lv_obj_set_style_radius(password_panel, 15, 0);
    lv_obj_set_style_border_width(password_panel, 0, 0);

    lv_obj_t* pwd_label = lv_label_create(password_panel);
    lv_label_set_text(pwd_label, "Password:");
    lv_obj_set_style_text_font(pwd_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pwd_label, LV_ALIGN_TOP_LEFT, 20, 10);

    password_textarea = lv_textarea_create(password_panel);
    lv_obj_set_size(password_textarea, 460, 50);
    lv_obj_align(password_textarea, LV_ALIGN_TOP_MID, 0, 40);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_textarea_set_placeholder_text(password_textarea, "Enter password");
    lv_obj_set_style_text_font(password_textarea, &lv_font_montserrat_16, 0);

    keyboard = lv_keyboard_create(password_panel);
    lv_obj_set_size(keyboard, 460, 200);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, password_textarea);

    lv_obj_t* checkbox_cont = lv_obj_create(password_panel);
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

    lv_obj_t* btn_cont = lv_obj_create(wifi_modal);
    disableScroll(btn_cont);
    lv_obj_set_size(btn_cont, 500, 60);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_cancel = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_cancel, 150, 50);
    lv_obj_set_style_radius(btn_cancel, 15, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(btn_cancel, close_wifi_modal, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_connect = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_connect, 150, 50);
    lv_obj_set_style_radius(btn_connect, 15, 0);
    lv_obj_set_style_bg_color(btn_connect, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_connect, wifi_connect_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    lv_obj_set_style_text_font(lbl_connect, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_connect, lv_color_white(), 0);
    lv_obj_center(lbl_connect);
}

static void wifi_network_btn_event_cb(lv_event_t* e) {
    const char* network_name = (const char*)lv_event_get_user_data(e);
    show_password_entry(network_name);
}

static void show_wifi_menu() {
    if (wifi_modal != NULL) return;
    num_networks = scanForNetworks();
    for (int i = 0; i < num_networks && i < 20; i++)
        scanned_networks[i] = WiFi.SSID(i);

    wifi_modal = lv_obj_create(lv_scr_act());
    disableScroll(wifi_modal);
    lv_obj_set_size(wifi_modal, 600, 450);
    lv_obj_center(wifi_modal);
    lv_obj_set_style_bg_color(wifi_modal, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_radius(wifi_modal, 20, 0);
    lv_obj_set_style_border_width(wifi_modal, 2, 0);
    lv_obj_set_style_border_color(wifi_modal, CLR_ACCENT, 0);
    lv_obj_set_style_shadow_width(wifi_modal, 30, 0);
    lv_obj_set_style_shadow_opa(wifi_modal, LV_OPA_30, 0);

    lv_obj_t* title = lv_label_create(wifi_modal);
    lv_label_set_text_fmt(title, "Found %d Networks", num_networks);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    wifi_list = lv_list_create(wifi_modal);
    disableScroll(wifi_list);
    lv_obj_set_size(wifi_list, 550, 320);
    lv_obj_align(wifi_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(wifi_list, 15, 0);

    if (num_networks <= 0) {
        const char* msg = (num_networks == 0) ? "No networks found" : "WiFi scan failed";
        lv_obj_t* btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WARNING, msg);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        for (int i = 0; i < num_networks && i < 20; i++) {
            char btn_text[80];
            snprintf(btn_text, sizeof(btn_text), "%s (%d dBm)", scanned_networks[i].c_str(), WiFi.RSSI(i));
            lv_obj_t* btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, btn_text);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
            lv_obj_set_height(btn, 60);
            lv_obj_add_event_cb(btn, wifi_network_btn_event_cb, LV_EVENT_CLICKED, (void*)scanned_networks[i].c_str());
        }
    }

    lv_obj_t* btn_close = lv_btn_create(wifi_modal);
    lv_obj_set_size(btn_close, 120, 45);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(btn_close, 15, 0);
    lv_obj_set_style_bg_color(btn_close, CLR_OFF, 0);
    lv_obj_add_event_cb(btn_close, close_wifi_modal, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_close, lv_color_white(), 0);
    lv_obj_center(lbl_close);
}

static void wifi_icon_click_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    show_wifi_menu();
}

// ---------------------------------------------------------------------------
//  Weather
// ---------------------------------------------------------------------------
void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q="
                 + String(WEATHER_CITY) + "," + String(WEATHER_COUNTRY_CODE)
                 + "&appid=" + String(WEATHER_API_KEY) + "&units=metric";
    http.begin(url);
    if (http.GET() == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            float       temp      = doc["main"]["temp"];
            const char* condition = doc["weather"][0]["main"];
            UI_UpdateWeather(temp, condition);
            last_weather_update = millis();
        }
    }
    http.end();
}

// ---------------------------------------------------------------------------
//  Screensaver
// ---------------------------------------------------------------------------
static void createScreensaver() {
    screensaver = lv_obj_create(lv_scr_act());
    disableScroll(screensaver);
    lv_obj_set_size(screensaver, 800, 480);
    lv_obj_set_pos(screensaver, 0, 0);
    lv_obj_set_style_bg_color(screensaver, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_bg_opa(screensaver, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screensaver, 0, 0);
    lv_obj_add_event_cb(screensaver, screen_touched_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(screensaver);
    lv_label_set_text(title, "SMART BOILER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    screensaver_time = lv_label_create(screensaver);
    lv_label_set_text(screensaver_time, "--:--");
    lv_obj_set_style_text_font(screensaver_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(screensaver_time, lv_color_hex(0x90CAF9), 0);
    lv_obj_align(screensaver_time, LV_ALIGN_CENTER, 0, 20);

    screensaver_date = lv_label_create(screensaver);
    lv_label_set_text(screensaver_date, "");
    lv_obj_set_style_text_font(screensaver_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(screensaver_date, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(screensaver_date, LV_ALIGN_CENTER, 0, 80);

    lv_obj_t* hint = lv_label_create(screensaver);
    lv_label_set_text(hint, "Tap to wake");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555566), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
}

void checkScreensaver() {
    if (screensaver == NULL) return;
    if (millis() - last_touch_time > SCREENSAVER_TIMEOUT &&
        !lv_obj_is_visible(screensaver))
        lv_obj_clear_flag(screensaver, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
//  UI_Init  —  800 x 480 consumer dashboard
//
//  ┌──────────────────────────────────────────────────────────────────────┐
//  │  TOP BAR (800 x 55): [WiFi]      12:34   16.04.2026        ☀ 22°   │
//  ├──────────────────┬──────────────────────┬───────────────────────────┤
//  │  LEFT  260 x 425 │  CENTER  240 x 425   │  RIGHT  300 x 425        │
//  │                  │                      │                           │
//  │   ╭──────────╮   │  ┌────────────────┐  │  ┌───────────────────┐   │
//  │   │ Water    │   │  │  Set Temp      │  │  │                   │   │
//  │   │  Temp    │   │  │     [ ▲ ]      │  │  │       OFF         │   │
//  │   │   65°    │   │  │      60°       │  │  │                   │   │
//  │   │   ●      │   │  │     [ ▼ ]      │  │  └───────────────────┘   │
//  │   ╰──────────╯   │  └────────────────┘  │                           │
//  │                  │  ┌────────────────┐  │  ┌───────────────────┐   │
//  │                  │  │ MODE: Smart    │  │  │  1 Shower Ready   │   │
//  │                  │  └────────────────┘  │  │  ready in ~5 min  │   │
//  └──────────────────┴──────────────────────┴───────────────────────────┘
// ===========================================================================
void UI_Init() {
    Board* board = new Board();
    board->init();
    board->begin();
    lvgl_port_init(board->getLCD(), board->getTouch());

    lvgl_port_lock(-1);

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_add_event_cb(scr, screen_touched_cb, LV_EVENT_PRESSED, NULL);

    // ===================================================================
    //  TOP BAR  (800 x 55)  — deep-blue strip
    // ===================================================================
    lv_obj_t* top_bar = lv_obj_create(scr);
    disableScroll(top_bar);
    lv_obj_set_size(top_bar, 800, 55);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, CLR_TOPBAR, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_hor(top_bar, 16, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);

    // WiFi button — far left
    lv_obj_t* wifi_btn = lv_btn_create(top_bar);
    disableScroll(wifi_btn);
    lv_obj_set_size(wifi_btn, 50, 42);
    lv_obj_align(wifi_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(wifi_btn, 10, 0);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_icon_click_event_cb, LV_EVENT_CLICKED, NULL);
    lbl_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(lbl_wifi_icon);

    // Time — centre
    lbl_time = lv_label_create(top_bar);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 0);

    // Date — right of time
    lbl_date = lv_label_create(top_bar);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xBBCCEE), 0);
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    // Weather — far right
    lv_obj_t* weather_row = lv_obj_create(top_bar);
    disableScroll(weather_row);
    lv_obj_set_size(weather_row, 110, 48);
    lv_obj_align(weather_row, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(weather_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_row, 0, 0);
    lv_obj_set_style_pad_all(weather_row, 0, 0);
    lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather_row, 6, 0);

    lbl_weather_icon = lv_label_create(weather_row);
    lv_label_set_text(lbl_weather_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(lbl_weather_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_weather_icon, lv_color_hex(0xFFD54F), 0);

    lbl_weather_temp = lv_label_create(weather_row);
    lv_label_set_text(lbl_weather_temp, "--\xc2\xb0");
    lv_obj_set_style_text_font(lbl_weather_temp, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_weather_temp, lv_color_white(), 0);

    // ===================================================================
    //  MAIN ROW  (800 x 425, y=55)
    // ===================================================================
    lv_obj_t* main_row = lv_obj_create(scr);
    disableScroll(main_row);
    lv_obj_set_size(main_row, 800, 425);
    lv_obj_set_pos(main_row, 0, 55);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_row, 0, 0);
    lv_obj_set_style_pad_all(main_row, 0, 0);
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ===================================================================
    //  LEFT PANEL  (260 x 425)  — temperature circle
    // ===================================================================
    lv_obj_t* left_panel = lv_obj_create(main_row);
    disableScroll(left_panel);
    lv_obj_set_size(left_panel, 340, 425);
    lv_obj_set_style_bg_opa(left_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_panel, 0, 0);
    lv_obj_set_style_pad_all(left_panel, 0, 0);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Temperature circle  (240 px diameter)
    temp_circle_obj = lv_obj_create(left_panel);
    disableScroll(temp_circle_obj);
    lv_obj_set_size(temp_circle_obj, 240, 240);
    lv_obj_set_style_radius(temp_circle_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(temp_circle_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(temp_circle_obj, 8, 0);
    lv_obj_set_style_border_color(temp_circle_obj, CLR_ACCENT, 0);
    lv_obj_set_style_shadow_width(temp_circle_obj, 24, 0);
    lv_obj_set_style_shadow_opa(temp_circle_obj, LV_OPA_20, 0);
    lv_obj_set_flex_flow(temp_circle_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(temp_circle_obj, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(temp_circle_obj, 6, 0);

    lv_obj_t* lbl_water_title = lv_label_create(temp_circle_obj);
    lv_label_set_text(lbl_water_title, "Water Temp");
    lv_obj_set_style_text_font(lbl_water_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_water_title, CLR_SUBTEXT, 0);

    lbl_water_temp = lv_label_create(temp_circle_obj);
    lv_label_set_text(lbl_water_temp, "--\xc2\xb0");
    lv_obj_set_style_text_font(lbl_water_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_water_temp, CLR_ACCENT, 0);

    // Subtle heating dot
    led_heating = lv_led_create(temp_circle_obj);
    lv_obj_set_size(led_heating, 14, 14);
    lv_led_set_color(led_heating, lv_palette_main(LV_PALETTE_ORANGE));
    lv_led_off(led_heating);

    // Target temperature controls (below the circle)
    lv_obj_t* target_card = lv_obj_create(left_panel);
    disableScroll(target_card);
    lv_obj_set_size(target_card, 240, 130);
    lv_obj_set_style_radius(target_card, 20, 0);
    lv_obj_set_style_bg_color(target_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(target_card, 2, 0);
    lv_obj_set_style_border_color(target_card, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_shadow_width(target_card, 12, 0);
    lv_obj_set_style_shadow_opa(target_card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(target_card, 8, 0);

    lv_obj_t* lbl_target_title = lv_label_create(target_card);
    lv_label_set_text(lbl_target_title, "Target Temp");
    lv_obj_set_style_text_font(lbl_target_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_target_title, CLR_SUBTEXT, 0);
    lv_obj_align(lbl_target_title, LV_ALIGN_TOP_MID, 0, 0);

    btn_temp_up = lv_btn_create(target_card);
    lv_obj_set_size(btn_temp_up, 50, 40);
    lv_obj_align(btn_temp_up, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(btn_temp_up, 12, 0);
    lv_obj_set_style_bg_color(btn_temp_up, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_temp_up, temp_up_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_up = lv_label_create(btn_temp_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(lbl_up, lv_color_white(), 0);
    lv_obj_center(lbl_up);

    btn_temp_down = lv_btn_create(target_card);
    lv_obj_set_size(btn_temp_down, 50, 40);
    lv_obj_align(btn_temp_down, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(btn_temp_down, 12, 0);
    lv_obj_set_style_bg_color(btn_temp_down, lv_color_hex(0xE0E0E0), 0);
    lv_obj_add_event_cb(btn_temp_down, temp_down_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_dn = lv_label_create(btn_temp_down);
    lv_label_set_text(lbl_dn, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_dn, CLR_TEXT, 0);
    lv_obj_center(lbl_dn);

    lbl_target_temp = lv_label_create(target_card);
    char tgt_buf[8];
    snprintf(tgt_buf, sizeof(tgt_buf), "%d\xc2\xb0", target_temperature);
    lv_label_set_text(lbl_target_temp, tgt_buf);
    lv_obj_set_style_text_font(lbl_target_temp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_target_temp, CLR_TEXT, 0);
    lv_obj_align(lbl_target_temp, LV_ALIGN_CENTER, 0, 0);

    // ===================================================================
    //  RIGHT PANEL  (430 x 425)  — ON/OFF + Shower Readiness
    // ===================================================================
    lv_obj_t* right_panel = lv_obj_create(main_row);
    disableScroll(right_panel);
    lv_obj_set_size(right_panel, 430, 425);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 0, 0);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right_panel, 20, 0);

    // Giant ON/OFF button  (260 x 215)
    btn_power = lv_btn_create(right_panel);
    disableScroll(btn_power);
    lv_obj_set_size(btn_power, 260, 215);
    lv_obj_set_style_radius(btn_power, 32, 0);
    lv_obj_set_style_bg_color(btn_power, CLR_OFF, 0);
    lv_obj_set_style_shadow_width(btn_power, 24, 0);
    lv_obj_set_style_shadow_opa(btn_power, LV_OPA_30, 0);
    lv_obj_add_event_cb(btn_power, power_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_power_status = lv_label_create(btn_power);
    lv_label_set_text(lbl_power_status, "OFF");
    lv_obj_set_style_text_font(lbl_power_status, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_power_status, lv_color_white(), 0);
    lv_obj_center(lbl_power_status);

    // Shower Readiness card  (260 x 185)
    shower_card_obj = lv_obj_create(right_panel);
    lv_obj_t* shower_card = shower_card_obj;
    disableScroll(shower_card);
    lv_obj_set_size(shower_card, 260, 185);
    lv_obj_set_style_radius(shower_card, 24, 0);
    lv_obj_set_style_bg_color(shower_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(shower_card, 2, 0);
    lv_obj_set_style_border_color(shower_card, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_shadow_width(shower_card, 16, 0);
    lv_obj_set_style_shadow_opa(shower_card, LV_OPA_10, 0);
    lv_obj_set_flex_flow(shower_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(shower_card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(shower_card, 8, 0);

    // Card title
    lv_obj_t* shower_title = lv_label_create(shower_card);
    lv_label_set_text(shower_title, LV_SYMBOL_DRIVE "  Shower Status");
    lv_obj_set_style_text_font(shower_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(shower_title, CLR_SUBTEXT, 0);

    // Row 1: showers available
    lbl_shower_readiness = lv_label_create(shower_card);
    lv_label_set_text(lbl_shower_readiness, "Connecting...");
    lv_obj_set_style_text_font(lbl_shower_readiness, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_shower_readiness, CLR_SUBTEXT, 0);

    // Row 2: wait time
    lbl_wait_time = lv_label_create(shower_card);
    lv_label_set_text(lbl_wait_time, "");
    lv_obj_set_style_text_font(lbl_wait_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wait_time, CLR_SUBTEXT, 0);

    // Divider
    lv_obj_t* divider = lv_obj_create(shower_card);
    disableScroll(divider);
    lv_obj_set_size(divider, 220, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    // Row 3: Ready By button
    lv_obj_t* btn_ready_by = lv_btn_create(shower_card);
    disableScroll(btn_ready_by);
    lv_obj_set_size(btn_ready_by, 200, 36);
    lv_obj_set_style_radius(btn_ready_by, 18, 0);
    lv_obj_set_style_bg_color(btn_ready_by, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_ready_by, [](lv_event_t* e) {
        last_touch_time = millis();
        if (ready_by_modal != NULL) lv_obj_clear_flag(ready_by_modal, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_rb_btn = lv_label_create(btn_ready_by);
    lv_label_set_text(lbl_rb_btn, LV_SYMBOL_BELL "  Ready By");
    lv_obj_set_style_text_font(lbl_rb_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rb_btn, lv_color_white(), 0);
    lv_obj_center(lbl_rb_btn);

    // ===================================================================
    //  READY BY MODAL  — time picker overlay
    // ===================================================================
    ready_by_modal = lv_obj_create(scr);
    disableScroll(ready_by_modal);
    lv_obj_set_size(ready_by_modal, 360, 280);
    lv_obj_center(ready_by_modal);
    lv_obj_set_style_radius(ready_by_modal, 24, 0);
    lv_obj_set_style_bg_color(ready_by_modal, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(ready_by_modal, 32, 0);
    lv_obj_set_style_shadow_opa(ready_by_modal, LV_OPA_30, 0);
    lv_obj_add_flag(ready_by_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl_rb_title = lv_label_create(ready_by_modal);
    lv_label_set_text(lbl_rb_title, "Set Ready-By Time");
    lv_obj_set_style_text_font(lbl_rb_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_rb_title, CLR_TEXT, 0);
    lv_obj_align(lbl_rb_title, LV_ALIGN_TOP_MID, 0, 16);

    // Hour controls
    lv_obj_t* btn_h_up = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_h_up, 56, 40);
    lv_obj_align(btn_h_up, LV_ALIGN_CENTER, -70, -40);
    lv_obj_set_style_bg_color(btn_h_up, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_h_up, [](lv_event_t*) {
        rb_hour = (rb_hour + 1) % 24;
        char buf[4]; snprintf(buf, sizeof(buf), "%02d", rb_hour);
        lv_label_set_text(lbl_rb_hour, buf);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l = lv_label_create(btn_h_up); lv_label_set_text(l, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(l, lv_color_white(), 0); lv_obj_center(l);

    lbl_rb_hour = lv_label_create(ready_by_modal);
    char hbuf[4]; snprintf(hbuf, sizeof(hbuf), "%02d", rb_hour);
    lv_label_set_text(lbl_rb_hour, hbuf);
    lv_obj_set_style_text_font(lbl_rb_hour, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_rb_hour, CLR_TEXT, 0);
    lv_obj_align(lbl_rb_hour, LV_ALIGN_CENTER, -70, 10);

    lv_obj_t* btn_h_dn = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_h_dn, 56, 40);
    lv_obj_align(btn_h_dn, LV_ALIGN_CENTER, -70, 58);
    lv_obj_set_style_bg_color(btn_h_dn, lv_color_hex(0xE0E0E0), 0);
    lv_obj_add_event_cb(btn_h_dn, [](lv_event_t*) {
        rb_hour = (rb_hour + 23) % 24;
        char buf[4]; snprintf(buf, sizeof(buf), "%02d", rb_hour);
        lv_label_set_text(lbl_rb_hour, buf);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l2 = lv_label_create(btn_h_dn); lv_label_set_text(l2, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(l2, CLR_TEXT, 0); lv_obj_center(l2);

    // Colon separator
    lv_obj_t* lbl_colon = lv_label_create(ready_by_modal);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_colon, CLR_TEXT, 0);
    lv_obj_align(lbl_colon, LV_ALIGN_CENTER, 0, 10);

    // Minute controls
    lv_obj_t* btn_m_up = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_m_up, 56, 40);
    lv_obj_align(btn_m_up, LV_ALIGN_CENTER, 70, -40);
    lv_obj_set_style_bg_color(btn_m_up, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_m_up, [](lv_event_t*) {
        rb_minute = (rb_minute + 5) % 60;
        char buf[4]; snprintf(buf, sizeof(buf), "%02d", rb_minute);
        lv_label_set_text(lbl_rb_minute, buf);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l3 = lv_label_create(btn_m_up); lv_label_set_text(l3, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(l3, lv_color_white(), 0); lv_obj_center(l3);

    lbl_rb_minute = lv_label_create(ready_by_modal);
    char mbuf[4]; snprintf(mbuf, sizeof(mbuf), "%02d", rb_minute);
    lv_label_set_text(lbl_rb_minute, mbuf);
    lv_obj_set_style_text_font(lbl_rb_minute, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_rb_minute, CLR_TEXT, 0);
    lv_obj_align(lbl_rb_minute, LV_ALIGN_CENTER, 70, 10);

    lv_obj_t* btn_m_dn = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_m_dn, 56, 40);
    lv_obj_align(btn_m_dn, LV_ALIGN_CENTER, 70, 58);
    lv_obj_set_style_bg_color(btn_m_dn, lv_color_hex(0xE0E0E0), 0);
    lv_obj_add_event_cb(btn_m_dn, [](lv_event_t*) {
        rb_minute = (rb_minute + 55) % 60;
        char buf[4]; snprintf(buf, sizeof(buf), "%02d", rb_minute);
        lv_label_set_text(lbl_rb_minute, buf);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l4 = lv_label_create(btn_m_dn); lv_label_set_text(l4, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(l4, CLR_TEXT, 0); lv_obj_center(l4);

    // Save + Cancel buttons
    lv_obj_t* btn_rb_save = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_rb_save, 130, 38);
    lv_obj_align(btn_rb_save, LV_ALIGN_BOTTOM_LEFT, 20, -14);
    lv_obj_set_style_bg_color(btn_rb_save, CLR_ON, 0);
    lv_obj_add_event_cb(btn_rb_save, [](lv_event_t*) {
        const uint16_t totalMin = (uint16_t)(rb_hour * 60 + rb_minute);
        BrainSettings::setWeekdayReadyBy(totalMin);
        BrainSettings::setWeekendReadyBy(totalMin);
        EventLog::append(BoilerEvent::TARGET_TIME_SET, (float)totalMin);
        lv_obj_add_flag(ready_by_modal, LV_OBJ_FLAG_HIDDEN);
        Serial.printf("[UI] Ready-by saved: %02d:%02d\n", rb_hour, rb_minute);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_rb_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_OK "  Save");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0); lv_obj_center(lbl_save);

    lv_obj_t* btn_rb_cancel = lv_btn_create(ready_by_modal);
    lv_obj_set_size(btn_rb_cancel, 130, 38);
    lv_obj_align(btn_rb_cancel, LV_ALIGN_BOTTOM_RIGHT, -20, -14);
    lv_obj_set_style_bg_color(btn_rb_cancel, lv_color_hex(0xE0E0E0), 0);
    lv_obj_add_event_cb(btn_rb_cancel, [](lv_event_t*) {
        lv_obj_add_flag(ready_by_modal, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_cancel = lv_label_create(btn_rb_cancel);
    lv_label_set_text(lbl_cancel, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_color(lbl_cancel, CLR_TEXT, 0); lv_obj_center(lbl_cancel);

    // ===================================================================
    //  INFO BAR  (800 x 40, below main row) — power, flow rate, PLC status
    // ===================================================================
    lv_obj_t* info_bar = lv_obj_create(scr);
    disableScroll(info_bar);
    lv_obj_set_size(info_bar, 800, 42);
    lv_obj_set_pos(info_bar, 0, 438);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0xE8EEF6), 0);
    lv_obj_set_style_border_width(info_bar, 0, 0);
    lv_obj_set_style_radius(info_bar, 0, 0);
    lv_obj_set_style_pad_hor(info_bar, 20, 0);
    lv_obj_set_style_pad_ver(info_bar, 0, 0);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Power label
    lv_obj_t* lbl_power_icon = lv_label_create(info_bar);
    lv_label_set_text(lbl_power_icon, LV_SYMBOL_CHARGE "  Power:");
    lv_obj_set_style_text_font(lbl_power_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_power_icon, CLR_SUBTEXT, 0);

    lbl_power_val = lv_label_create(info_bar);
    lv_label_set_text(lbl_power_val, "-- W");
    lv_obj_set_style_text_font(lbl_power_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_power_val, CLR_TEXT, 0);

    // Flow label
    lv_obj_t* lbl_flow_icon = lv_label_create(info_bar);
    lv_label_set_text(lbl_flow_icon, LV_SYMBOL_PLAY "  Flow:");
    lv_obj_set_style_text_font(lbl_flow_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_flow_icon, CLR_SUBTEXT, 0);

    lbl_flow_val = lv_label_create(info_bar);
    lv_label_set_text(lbl_flow_val, "-- L/min");
    lv_obj_set_style_text_font(lbl_flow_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_flow_val, CLR_TEXT, 0);

    // PLC status
    lbl_plc_status = lv_label_create(info_bar);
    lv_label_set_text(lbl_plc_status, LV_SYMBOL_WARNING "  No Signal");
    lv_obj_set_style_text_font(lbl_plc_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_plc_status, lv_palette_main(LV_PALETTE_RED), 0);
    createScreensaver();
    last_touch_time = millis();

    lvgl_port_unlock();
}

// ===========================================================================
//  Public update functions
// ===========================================================================

void UI_UpdateWaterTemp(float value) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_water_temp != NULL && value > -100) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", value);
            lv_label_set_text(lbl_water_temp, buf);
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateWeather(float temp, const char* condition) {
    s_outdoorTempC = temp;  // make available to brain
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_weather_temp != NULL) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", temp);
            lv_label_set_text(lbl_weather_temp, buf);
        }
        if (lbl_weather_icon != NULL && condition != NULL) {
            if      (strstr(condition, "Clear"))  lv_label_set_text(lbl_weather_icon, LV_SYMBOL_IMAGE);
            else if (strstr(condition, "Cloud"))  lv_label_set_text(lbl_weather_icon, LV_SYMBOL_CALL);
            else if (strstr(condition, "Rain"))   lv_label_set_text(lbl_weather_icon, LV_SYMBOL_DOWNLOAD);
            else                                  lv_label_set_text(lbl_weather_icon, LV_SYMBOL_EYE_OPEN);
        }
        lvgl_port_unlock();
    }
}

float UI_GetOutdoorTempC() {
    return s_outdoorTempC;
}

void UI_SetBoilerState(bool is_on) {
    boiler_state = is_on;
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (btn_power        != NULL) lv_obj_set_style_bg_color(btn_power, is_on ? CLR_ON : CLR_OFF, 0);
        if (lbl_power_status != NULL) lv_label_set_text(lbl_power_status, is_on ? "ON" : "OFF");
        lvgl_port_unlock();
    }
}

void UI_SetHeatingStatus(bool is_heating) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (led_heating != NULL) {
            if (is_heating) lv_led_on(led_heating);
            else            lv_led_off(led_heating);
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateTime(int hour, int minute) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        if (lbl_time         != NULL) lv_label_set_text(lbl_time,         buf);
        if (screensaver_time != NULL) lv_label_set_text(screensaver_time, buf);
        lvgl_port_unlock();
    }
}

void UI_UpdateDate(const char* date_str) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_date         != NULL && date_str) lv_label_set_text(lbl_date,         date_str);
        if (screensaver_date != NULL && date_str) lv_label_set_text(screensaver_date, date_str);
        lvgl_port_unlock();
    }
}

void UI_UpdateWiFiStatus() {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_wifi_icon != NULL) {
            lv_obj_set_style_text_color(lbl_wifi_icon,
                (WiFi.status() == WL_CONNECTED)
                    ? lv_palette_main(LV_PALETTE_GREEN)
                    : lv_palette_main(LV_PALETTE_RED), 0);
        }
        lvgl_port_unlock();
    }
}

// Raw engineering values — intentionally hidden in the consumer view
void UI_UpdateBoostTemp(float value)  { (void)value; }
void UI_UpdateFlowRate(float value)   { (void)value; }

void UI_UpdatePLCStatus(bool connected) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_plc_status != NULL) {
            if (connected) {
                lv_label_set_text(lbl_plc_status, LV_SYMBOL_OK "  Connected");
                lv_obj_set_style_text_color(lbl_plc_status,
                    lv_palette_main(LV_PALETTE_GREEN), 0);
            } else {
                lv_label_set_text(lbl_plc_status, LV_SYMBOL_WARNING "  No Signal");
                lv_obj_set_style_text_color(lbl_plc_status,
                    lv_palette_main(LV_PALETTE_RED), 0);
            }
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateSystemMode(const char* mode) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_system_mode != NULL && mode != NULL)
            lv_label_set_text(lbl_system_mode, mode);
        lvgl_port_unlock();
    }
}

void UI_UpdateSSRStatus(bool internal_on, bool boost_on) {
    // SSR labels removed; keep heating LED in sync
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (led_heating != NULL) {
            if (internal_on || boost_on) lv_led_on(led_heating);
            else                         lv_led_off(led_heating);
        }
        lvgl_port_unlock();
    }
}

// Called by TaskMasterComms every second with live slave packet fields
void UI_UpdateSensorData(float t_internal, float t_boost, float flow, float power_w) {
    // --- Consumer metrics ---
    // Showers: 80 L tank, shower = 60 L mixed to 38°C from 20°C cold
    int showers = 0;
    if (t_internal > 20.0f)
        showers = (int)((t_internal - 20.0f) * 80.0f / (60.0f * 18.0f));

    // Minutes to target: P=2000 W, m=80 kg
    int wait_min = 0;
    if (target_temperature > (int)t_internal) {
        float delta = (float)target_temperature - t_internal;
        wait_min = (int)(delta * 80.0f * 4186.0f / (2000.0f * 60.0f));
    }

    bool internal_ssr = boiler_state && (t_internal < (float)target_temperature);
    bool boost_ssr    = boiler_state && (flow > 0.5f);

    if (lvgl_port_lock(UI_REFRESH_RATE)) {

        // Water temperature + dynamic ring colour
        if (lbl_water_temp != NULL && t_internal > -100) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", t_internal);
            lv_label_set_text(lbl_water_temp, buf);

            lv_color_t ring_col = (t_internal >= 55.0f)
                                  ? lv_palette_main(LV_PALETTE_RED)
                                  : (t_internal >= 38.0f)
                                  ? lv_palette_main(LV_PALETTE_ORANGE)
                                  : CLR_ACCENT;
            if (temp_circle_obj != NULL)
                lv_obj_set_style_border_color(temp_circle_obj, ring_col, 0);
        }

        // Shower Readiness — card turns green when hot water is available
        if (shower_card_obj != NULL) {
            if (showers >= 1) {
                lv_obj_set_style_bg_color(shower_card_obj, lv_color_hex(0xE8F5E9), 0);
                lv_obj_set_style_border_color(shower_card_obj, lv_color_hex(0x2E7D32), 0);
            } else {
                lv_obj_set_style_bg_color(shower_card_obj, lv_color_white(), 0);
                lv_obj_set_style_border_color(shower_card_obj, lv_color_hex(0xE0E0E0), 0);
            }
        }
        if (lbl_shower_readiness != NULL) {
            if (showers >= 1) {
                char msg[40];
                snprintf(msg, sizeof(msg), LV_SYMBOL_OK "  %d Shower%s Ready!",
                         showers, showers > 1 ? "s" : "");
                lv_label_set_text(lbl_shower_readiness, msg);
                lv_obj_set_style_text_font(lbl_shower_readiness, &lv_font_montserrat_24, 0);
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_READY, 0);
            } else {
                lv_label_set_text(lbl_shower_readiness, "Heating Up...");
                lv_obj_set_style_text_font(lbl_shower_readiness, &lv_font_montserrat_20, 0);
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_WAITING, 0);
            }
        }

        // Wait time sub-label
        if (lbl_wait_time != NULL) {
            if (wait_min > 0 && showers == 0) {
                char wbuf[24];
                snprintf(wbuf, sizeof(wbuf), "ready in ~%d min", wait_min);
                lv_label_set_text(lbl_wait_time, wbuf);
            } else {
                lv_label_set_text(lbl_wait_time, "");
            }
        }

        // Flow status indicator
        bool flowing = (flow > 0.5f);
        if (lbl_flow_status != NULL) {
            if (flowing) {
                char fmsg[28];
                snprintf(fmsg, sizeof(fmsg), LV_SYMBOL_PLAY "  %.1f L/min", flow);
                lv_label_set_text(lbl_flow_status, fmsg);
                lv_obj_set_style_text_color(lbl_flow_status, CLR_READY, 0);
            } else {
                lv_label_set_text(lbl_flow_status, LV_SYMBOL_STOP "  No Flow");
                lv_obj_set_style_text_color(lbl_flow_status, CLR_SUBTEXT, 0);
            }
        }

        // Power and flow rate in info bar
        if (lbl_power_val != NULL) {
            char pbuf[16];
            snprintf(pbuf, sizeof(pbuf), "%.0f W", power_w);
            lv_label_set_text(lbl_power_val, pbuf);
        }
        if (lbl_flow_val != NULL) {
            char fbuf[16];
            if (flow > 0.05f)
                snprintf(fbuf, sizeof(fbuf), "%.1f L/min", flow);
            else
                snprintf(fbuf, sizeof(fbuf), "0.0 L/min");
            lv_label_set_text(lbl_flow_val, fbuf);
        }

        // Heating LED
        if (led_heating != NULL) {
            if (internal_ssr || boost_ssr) lv_led_on(led_heating);
            else                           lv_led_off(led_heating);
        }

        lvgl_port_unlock();
    }
}

// ===========================================================================
//  TaskUi
//
//  Owns the UI application layer:
//    - Initializes LVGL/widgets once.
//    - Publishes user input through UiSnapshot from event callbacks.
//    - Pulls SensorSnapshot and CommandSnapshot for display.
//    - Calls lv_timer_handler() so LVGL renders and dispatches touch events.
// ===========================================================================

static void renderSnapshotsForDisplay(const SensorSnapshot& sensor,
                                      const CommandSnapshot& command) {
    if (sensor.valid) {
        UI_UpdateSensorData(sensor.tempInternalC,
                            sensor.tempBoostOutC,
                            sensor.flowLpm,
                            sensor.powerW);
    }

    if (command.valid) {
        const bool internal_on =
            (command.cmdFlags & CMD_HEATER_ENABLE) && command.pwmInternal > 0u;
        const bool boost_on =
            (command.cmdFlags & CMD_BOOST_ENABLE) && command.pwmBoost > 0u;

        UI_UpdateSSRStatus(internal_on, boost_on);
        UI_UpdateSystemMode(SystemManager::labelFor(command.state));
        UI_UpdatePLCStatus(command.plcConnected);
    }
}

void TaskUi(void* pvParameters) {
    (void)pvParameters;

    Serial.println("[UI] Task started");

    UI_Init();
    publishUiSnapshot();

    SensorSnapshot latestSensor{};
    CommandSnapshot latestCommand{};
    TickType_t lastRenderTick = 0;
    TickType_t lastWakeTick = xTaskGetTickCount();

    for (;;) {
        SensorSnapshot sensor{};
        if (MasterState_ReadSensorSnapshot(sensor) && sensor.valid) {
            latestSensor = sensor;
        }

        CommandSnapshot command{};
        if (MasterState_ReadCommandSnapshot(command) && command.valid) {
            latestCommand = command;
        }

        const TickType_t now = xTaskGetTickCount();
        if ((now - lastRenderTick) >= pdMS_TO_TICKS(TASK_UI_RENDER_PERIOD_MS)) {
            lastRenderTick = now;
            renderSnapshotsForDisplay(latestSensor, latestCommand);
            checkScreensaver();
        }

        if (lvgl_port_lock(TASK_UI_PERIOD_MS)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }

        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(TASK_UI_PERIOD_MS));
    }
}
