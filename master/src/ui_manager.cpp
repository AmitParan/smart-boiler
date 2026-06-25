// ui_manager.cpp  — Multi-screen consumer redesign (v6)
// Screen: 800 x 480  |  LVGL v8  |  Montserrat fonts
// Layout/flow ported from docs/ui-mockup/index.html

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

#define WEATHER_API_KEY      "7a1028898a2cdcc08a58a8109fb061e4"  // local only - do not commit
#define WEATHER_CITY         "Tel Aviv"
#define WEATHER_COUNTRY_CODE "IL"
#define SETUP_AP_SSID        "Boiler-Setup"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// ---------------------------------------------------------------------------
//  Colour palette
// ---------------------------------------------------------------------------
#define CLR_BG       lv_color_hex(0xF0F4F8)  // light grey background
#define CLR_TOPBAR   lv_color_hex(0x1565C0)  // deep blue top bar / headers
#define CLR_ACCENT   lv_color_hex(0x1565C0)  // accent (buttons, ring)
#define CLR_ON       lv_color_hex(0x2E7D32)  // boiler ON  — green
#define CLR_OFF      lv_color_hex(0xC62828)  // boiler OFF — red
#define CLR_TEXT     lv_color_hex(0x1A1A2E)  // primary text
#define CLR_SUBTEXT  lv_color_hex(0x7B8794)  // secondary text
#define CLR_READY    lv_color_hex(0x2E7D32)  // shower ready
#define CLR_WAITING  lv_color_hex(0xE65100)  // shower not ready
#define CLR_BORDER   lv_color_hex(0xE0E0E0)  // card border
#define CLR_DIAG_BG  lv_color_hex(0x263238)  // diagnostics card background
#define CLR_DIAG_HDR lv_color_hex(0x37474F)  // diagnostics header background

static const int LEAD_TIME_MIN = 15;  // assumed pre-heat lead time for schedule math

// ---------------------------------------------------------------------------
//  Page containers (each 800x480, shown/hidden — only one visible at a time)
// ---------------------------------------------------------------------------
static lv_obj_t* page_home        = NULL;
static lv_obj_t* page_settings     = NULL;
static lv_obj_t* page_schedule     = NULL;
static lv_obj_t* page_network      = NULL;
static lv_obj_t* page_password     = NULL;
static lv_obj_t* page_stats        = NULL;
static lv_obj_t* page_diagnostics  = NULL;

// ---------------------------------------------------------------------------
//  Widget pointers — Home
// ---------------------------------------------------------------------------
lv_obj_t* lbl_water_temp     = NULL;
static lv_obj_t* lbl_target_inline = NULL;     // "Target 60°" inside the circle
lv_obj_t* lbl_time           = NULL;
lv_obj_t* lbl_date           = NULL;
lv_obj_t* btn_power          = NULL;
lv_obj_t* lbl_power_status   = NULL;
lv_obj_t* led_heating        = NULL;
lv_obj_t* lbl_wifi_icon      = NULL;
lv_obj_t* lbl_weather_icon   = NULL;
lv_obj_t* lbl_weather_temp   = NULL;
static lv_obj_t* temp_circle_obj      = NULL;
static lv_obj_t* lbl_shower_readiness = NULL;
static lv_obj_t* btn_auto_badge       = NULL;
static lv_obj_t* lbl_auto_badge       = NULL;

// Kept for header API compatibility
lv_obj_t* lbl_target_temp  = NULL;
lv_obj_t* btn_timer        = NULL;   // unused
lv_obj_t* lbl_timer_status = NULL;   // unused
lv_obj_t* btn_temp_up      = NULL;
lv_obj_t* btn_temp_down    = NULL;
lv_obj_t* lbl_boost_temp   = NULL;   // unused (no longer surfaced to consumer view)
lv_obj_t* lbl_flow_rate    = NULL;   // unused
lv_obj_t* lbl_showers      = NULL;   // unused
lv_obj_t* lbl_wait_time    = NULL;   // unused
lv_obj_t* lbl_system_mode  = NULL;   // shown on Stats page
lv_obj_t* led_ssr_internal = NULL;   // unused
lv_obj_t* led_ssr_boost    = NULL;   // unused

// ---------------------------------------------------------------------------
//  Widget pointers — Network / Password
// ---------------------------------------------------------------------------
static lv_obj_t* net_list_container = NULL;
static lv_obj_t* lbl_net_status     = NULL;
static lv_obj_t* lbl_pwd_title      = NULL;
static lv_obj_t* lbl_pwd_error      = NULL;
static lv_obj_t* password_textarea  = NULL;
static lv_obj_t* keyboard           = NULL;
static lv_obj_t* lbl_eye_icon       = NULL;
static lv_obj_t* btn_connect        = NULL;
static lv_obj_t* lbl_connect_btn_text = NULL;
static lv_obj_t* save_modal         = NULL;
static lv_obj_t* lbl_save_modal_ssid = NULL;

// ---------------------------------------------------------------------------
//  Widget pointers — Schedule
// ---------------------------------------------------------------------------
static lv_obj_t* sched_body         = NULL;
static lv_obj_t* btn_sched_auto     = NULL;
static lv_obj_t* lbl_sched_auto     = NULL;
static lv_obj_t* btn_mode_smart     = NULL;
static lv_obj_t* lbl_mode_smart     = NULL;
static lv_obj_t* btn_mode_ready     = NULL;
static lv_obj_t* lbl_mode_ready     = NULL;
static lv_obj_t* panel_ready        = NULL;
static lv_obj_t* panel_smart        = NULL;
static lv_obj_t* btn_day_wk         = NULL;
static lv_obj_t* lbl_day_wk         = NULL;
static lv_obj_t* btn_day_we         = NULL;
static lv_obj_t* lbl_day_we         = NULL;
static lv_obj_t* lbl_hh             = NULL;
static lv_obj_t* lbl_mm             = NULL;
static lv_obj_t* lbl_next_preheat   = NULL;
static lv_obj_t* lbl_skip_btn       = NULL;

// ---------------------------------------------------------------------------
//  Widget pointers — Stats (live status) / Diagnostics
// ---------------------------------------------------------------------------
static lv_obj_t* lbl_stat_power = NULL;
static lv_obj_t* lbl_stat_flow  = NULL;
static lv_obj_t* lbl_stat_plc   = NULL;
static lv_obj_t* bar_temp_progress = NULL;
static lv_obj_t* lbl_temp_progress_text = NULL;

static lv_obj_t* lbl_diag_tint   = NULL;
static lv_obj_t* lbl_diag_tboost = NULL;
static lv_obj_t* lbl_diag_flow   = NULL;
static lv_obj_t* lbl_diag_power  = NULL;
static lv_obj_t* lbl_diag_plc    = NULL;
static lv_obj_t* lbl_diag_ssr    = NULL;
static lv_obj_t* lbl_diag_rssi   = NULL;
static lv_obj_t* lbl_diag_heap   = NULL;
static lv_obj_t* lbl_diag_uptime = NULL;

// ---------------------------------------------------------------------------
//  Screensaver
// ---------------------------------------------------------------------------
static lv_obj_t* screensaver      = NULL;
static lv_obj_t* screensaver_time = NULL;
static lv_obj_t* screensaver_date = NULL;
unsigned long last_touch_time = 0;
#define SCREENSAVER_TIMEOUT 60000

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

// Schedule — local UI state only. No scheduling backend exists on this
// branch (SystemManager has no auto-preheat support), so this page is a
// front-end placeholder: it doesn't drive any heating decision yet.
static bool g_auto_enabled  = true;
static bool g_smart_learn   = false;   // false = "Ready by" mode
static int  g_ready_hh[2]   = {7, 9};  // [0]=weekday [1]=weekend
static int  g_ready_mm[2]   = {0, 0};
static int  g_sched_day_idx = 0;
static bool g_skip_today    = false;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static bool stationConnected() { return WiFi.status() == WL_CONNECTED; }

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
//  Page navigation
// ---------------------------------------------------------------------------
static void show_page(lv_obj_t* target) {
    lv_obj_t* pages[] = { page_home, page_settings, page_schedule, page_network,
                          page_password, page_stats, page_diagnostics };
    for (lv_obj_t* p : pages) {
        if (p == NULL) continue;
        if (p == target) lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    last_touch_time = millis();
}

static void nav_back_cb(lv_event_t* e) {
    last_touch_time = millis();
    lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(e);
    show_page(target);
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
//  Reusable component helpers
// ---------------------------------------------------------------------------
static lv_obj_t* make_header(lv_obj_t* parent, lv_color_t bg) {
    lv_obj_t* header = lv_obj_create(parent);
    disableScroll(header);
    lv_obj_set_size(header, 800, 64);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, bg, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 12, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return header;
}

static lv_obj_t* make_back_btn(lv_obj_t* header, lv_obj_t* target_page) {
    lv_obj_t* btn = lv_btn_create(header);
    lv_obj_set_size(btn, 64, 48);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, nav_back_cb, LV_EVENT_CLICKED, target_page);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t* make_header_title(lv_obj_t* header, const char* text) {
    lv_obj_t* lbl = lv_label_create(header);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_flex_grow(lbl, 1);
    return lbl;
}

static lv_obj_t* make_tile(lv_obj_t* parent, const char* icon, const char* text,
                            lv_color_t bg, lv_color_t border, lv_color_t fg,
                            lv_event_cb_t cb) {
    lv_obj_t* tile = lv_btn_create(parent);
    disableScroll(tile);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, 116);
    lv_obj_set_style_radius(tile, 22, 0);
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, border, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 8, 0);
    if (cb) lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_icon = lv_label_create(tile);
    lv_label_set_text(lbl_icon, icon);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_icon, fg, 0);

    lv_obj_t* lbl_text = lv_label_create(tile);
    lv_label_set_text(lbl_text, text);
    lv_obj_set_style_text_font(lbl_text, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_text, fg, 0);
    return tile;
}

// Small stat card used on the Stats page: title + big value label (returned)
static lv_obj_t* make_stat_card(lv_obj_t* parent, const char* title, lv_color_t value_color) {
    lv_obj_t* card = lv_obj_create(parent);
    disableScroll(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, 90);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, CLR_BORDER, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t* lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, CLR_SUBTEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* lbl_value = lv_label_create(card);
    lv_label_set_text(lbl_value, "--");
    lv_obj_set_style_text_font(lbl_value, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_value, value_color, 0);
    lv_obj_align(lbl_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return lbl_value;
}

// Dark diagnostics card: title + value label (returned)
static lv_obj_t* make_diag_card(lv_obj_t* parent, const char* title, lv_color_t value_color) {
    lv_obj_t* card = lv_obj_create(parent);
    disableScroll(card);
    lv_obj_set_style_bg_color(card, CLR_DIAG_BG, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    lv_obj_t* lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* lbl_value = lv_label_create(card);
    lv_label_set_text(lbl_value, "--");
    lv_obj_set_style_text_font(lbl_value, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_value, value_color, 0);
    lv_obj_align(lbl_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return lbl_value;
}

// ---------------------------------------------------------------------------
//  Target temperature (shared between Home circle subtitle and Settings)
// ---------------------------------------------------------------------------
static void updateTargetLabels() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d\xc2\xb0", target_temperature);
    if (lbl_target_temp != NULL) lv_label_set_text(lbl_target_temp, buf);
    if (lbl_target_inline != NULL) {
        char buf2[20];
        snprintf(buf2, sizeof(buf2), "Target %d\xc2\xb0", target_temperature);
        lv_label_set_text(lbl_target_inline, buf2);
    }
}

static void power_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    boiler_state = !boiler_state;
    if (btn_power        != NULL) lv_obj_set_style_bg_color(btn_power, boiler_state ? CLR_ON : CLR_OFF, 0);
    if (lbl_power_status != NULL) lv_label_set_text(lbl_power_status, boiler_state ? "ON" : "OFF");
    Serial.printf("Boiler: %s\n", boiler_state ? "ON" : "OFF");
}

static void temp_up_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (target_temperature < 80) {
        target_temperature += 5;
        updateTargetLabels();
    }
}

static void temp_down_btn_event_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (target_temperature > 30) {
        target_temperature -= 5;
        updateTargetLabels();
    }
}

// ---------------------------------------------------------------------------
//  Network / Password flow
//  Key change from v5: the "save password" checkbox is gone. Instead we ask
//  "Save this password?" only AFTER a successful connect.
// ---------------------------------------------------------------------------
static void refresh_network_status() {
    if (lbl_net_status == NULL) return;
    if (WiFi.status() == WL_CONNECTED) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Connected to %s", WiFi.SSID().c_str());
        lv_label_set_text(lbl_net_status, buf);
        lv_obj_set_style_text_color(lbl_net_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(lbl_net_status, LV_SYMBOL_WARNING "  Not connected");
        lv_obj_set_style_text_color(lbl_net_status, lv_palette_main(LV_PALETTE_RED), 0);
    }
}

static void net_row_click_cb(lv_event_t* e);

static void rebuild_network_list() {
    if (net_list_container == NULL) return;
    lv_obj_clean(net_list_container);

    num_networks = scanForNetworks();
    for (int i = 0; i < num_networks && i < 20; i++) scanned_networks[i] = WiFi.SSID(i);

    if (num_networks <= 0) {
        lv_obj_t* lbl = lv_label_create(net_list_container);
        lv_label_set_text(lbl, (num_networks == 0) ? "No networks found" : "Scan failed");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, CLR_SUBTEXT, 0);
        return;
    }

    for (int i = 0; i < num_networks && i < 20; i++) {
        lv_obj_t* row = lv_btn_create(net_list_container);
        disableScroll(row);
        lv_obj_set_size(row, lv_pct(100), 64);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_bg_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, CLR_BORDER, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        lv_obj_set_style_pad_hor(row, 18, 0);
        lv_obj_add_event_cb(row, net_row_click_cb, LV_EVENT_CLICKED, (void*)scanned_networks[i].c_str());

        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(icon, CLR_ACCENT, 0);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, scanned_networks[i].c_str());
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_flex_grow(name, 1);

        lv_obj_t* rssi = lv_label_create(row);
        char rbuf[16];
        snprintf(rbuf, sizeof(rbuf), "%d dBm", WiFi.RSSI(i));
        lv_label_set_text(rssi, rbuf);
        lv_obj_set_style_text_font(rssi, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(rssi, CLR_SUBTEXT, 0);
    }
}

static void net_scan_btn_cb(lv_event_t* e) {
    last_touch_time = millis();
    rebuild_network_list();
}

static void open_password_page(const char* ssid) {
    last_touch_time = millis();
    strncpy(selected_network, ssid, sizeof(selected_network) - 1);
    selected_network[sizeof(selected_network) - 1] = '\0';

    if (lbl_pwd_title     != NULL) lv_label_set_text(lbl_pwd_title, selected_network);
    if (password_textarea != NULL) {
        lv_textarea_set_text(password_textarea, "");
        lv_textarea_set_password_mode(password_textarea, true);
    }
    if (lbl_eye_icon != NULL) lv_label_set_text(lbl_eye_icon, LV_SYMBOL_EYE_CLOSE);
    if (lbl_pwd_error != NULL) lv_label_set_text(lbl_pwd_error, "");
    show_page(page_password);
}

static void net_row_click_cb(lv_event_t* e) {
    const char* ssid = (const char*)lv_event_get_user_data(e);
    open_password_page(ssid);
}

static void goto_network_cb(lv_event_t* e) {
    last_touch_time = millis();
    show_page(page_network);
    rebuild_network_list();
    refresh_network_status();
}

static void pwd_eye_toggle_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (password_textarea == NULL) return;
    bool was_hidden = lv_textarea_get_password_mode(password_textarea);
    lv_textarea_set_password_mode(password_textarea, !was_hidden);
    if (lbl_eye_icon != NULL)
        lv_label_set_text(lbl_eye_icon, was_hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static bool attempt_wifi_connect(const char* ssid, const char* password) {
    if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect(true, true); delay(100); }
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.softAPdisconnect(true);
        return true;
    }
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPSSID() != String(SETUP_AP_SSID)) WiFi.softAP(SETUP_AP_SSID, "12345678");
    return false;
}

static void pwd_connect_btn_cb(lv_event_t* e) {
    last_touch_time = millis();
    if (password_textarea == NULL) return;
    const char* password = lv_textarea_get_text(password_textarea);

    if (lbl_connect_btn_text != NULL) lv_label_set_text(lbl_connect_btn_text, "Connecting...");
    if (btn_connect          != NULL) lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_refr_now(NULL);

    bool ok = attempt_wifi_connect(selected_network, password);

    if (ok) {
        wifi_connected = true;
        if (lbl_wifi_icon != NULL) lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_GREEN), 0);
        if (lbl_pwd_error != NULL) lv_label_set_text(lbl_pwd_error, "");
        if (lbl_save_modal_ssid != NULL) lv_label_set_text(lbl_save_modal_ssid, selected_network);
        if (save_modal != NULL) lv_obj_clear_flag(save_modal, LV_OBJ_FLAG_HIDDEN);
    } else {
        wifi_connected = false;
        if (lbl_wifi_icon != NULL) lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
        if (lbl_pwd_error != NULL) lv_label_set_text(lbl_pwd_error, "Couldn't connect. Check the password and try again.");
    }

    if (lbl_connect_btn_text != NULL) lv_label_set_text(lbl_connect_btn_text, "Connect");
    if (btn_connect          != NULL) lv_obj_set_style_bg_color(btn_connect, CLR_ON, 0);
}

static void save_modal_close(bool save_password) {
    if (save_password) DataManager::saveWiFiCredentials(selected_network, lv_textarea_get_text(password_textarea));
    if (save_modal != NULL) lv_obj_add_flag(save_modal, LV_OBJ_FLAG_HIDDEN);
    refresh_network_status();
    show_page(page_network);
}

static void save_modal_yes_cb(lv_event_t* e) { last_touch_time = millis(); save_modal_close(true); }
static void save_modal_no_cb(lv_event_t* e)  { last_touch_time = millis(); save_modal_close(false); }

static void wifi_icon_click_event_cb(lv_event_t* e) { goto_network_cb(e); }

// ---------------------------------------------------------------------------
//  Schedule (local UI state only — see note at g_auto_enabled declaration)
// ---------------------------------------------------------------------------
static void update_auto_badge() {
    if (btn_auto_badge == NULL || lbl_auto_badge == NULL) return;
    if (!g_auto_enabled) {
        lv_obj_set_style_bg_color(btn_auto_badge, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(lbl_auto_badge, LV_SYMBOL_CHARGE "  Auto Off");
        return;
    }
    int total = g_ready_hh[g_sched_day_idx] * 60 + g_ready_mm[g_sched_day_idx] - LEAD_TIME_MIN;
    if (total < 0) total += 1440;
    char buf[24];
    snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  Auto %02d:%02d", total / 60, total % 60);
    lv_obj_set_style_bg_color(btn_auto_badge, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(lbl_auto_badge, buf);
}

static void recompute_next_preheat() {
    if (lbl_next_preheat == NULL) return;
    int total = g_ready_hh[g_sched_day_idx] * 60 + g_ready_mm[g_sched_day_idx] - LEAD_TIME_MIN;
    if (total < 0) total += 1440;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    lv_label_set_text(lbl_next_preheat, buf);
    update_auto_badge();
}

static void update_time_labels() {
    char hb[4], mb[4];
    snprintf(hb, sizeof(hb), "%02d", g_ready_hh[g_sched_day_idx]);
    snprintf(mb, sizeof(mb), "%02d", g_ready_mm[g_sched_day_idx]);
    if (lbl_hh != NULL) lv_label_set_text(lbl_hh, hb);
    if (lbl_mm != NULL) lv_label_set_text(lbl_mm, mb);
    recompute_next_preheat();
}

static void update_day_buttons() {
    bool wk = (g_sched_day_idx == 0);
    if (btn_day_wk != NULL) lv_obj_set_style_bg_color(btn_day_wk, wk ? CLR_ACCENT : lv_color_white(), 0);
    if (lbl_day_wk != NULL) lv_obj_set_style_text_color(lbl_day_wk, wk ? lv_color_white() : CLR_TEXT, 0);
    if (btn_day_we != NULL) lv_obj_set_style_bg_color(btn_day_we, !wk ? CLR_ACCENT : lv_color_white(), 0);
    if (lbl_day_we != NULL) lv_obj_set_style_text_color(lbl_day_we, !wk ? lv_color_white() : CLR_TEXT, 0);
}

static void update_mode_buttons() {
    if (btn_mode_smart != NULL) lv_obj_set_style_bg_color(btn_mode_smart, g_smart_learn ? CLR_ACCENT : lv_color_white(), 0);
    if (lbl_mode_smart != NULL) lv_obj_set_style_text_color(lbl_mode_smart, g_smart_learn ? lv_color_white() : CLR_TEXT, 0);
    if (btn_mode_ready != NULL) lv_obj_set_style_bg_color(btn_mode_ready, !g_smart_learn ? CLR_ACCENT : lv_color_white(), 0);
    if (lbl_mode_ready != NULL) lv_obj_set_style_text_color(lbl_mode_ready, !g_smart_learn ? lv_color_white() : CLR_TEXT, 0);
    if (panel_ready != NULL) { if (g_smart_learn) lv_obj_add_flag(panel_ready, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(panel_ready, LV_OBJ_FLAG_HIDDEN); }
    if (panel_smart != NULL) { if (g_smart_learn) lv_obj_clear_flag(panel_smart, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(panel_smart, LV_OBJ_FLAG_HIDDEN); }
}

static void sched_mode_smart_cb(lv_event_t* e) { last_touch_time = millis(); g_smart_learn = true;  update_mode_buttons(); }
static void sched_mode_ready_cb(lv_event_t* e) { last_touch_time = millis(); g_smart_learn = false; update_mode_buttons(); }
static void sched_day_wk_cb(lv_event_t* e) { last_touch_time = millis(); g_sched_day_idx = 0; update_day_buttons(); update_time_labels(); }
static void sched_day_we_cb(lv_event_t* e) { last_touch_time = millis(); g_sched_day_idx = 1; update_day_buttons(); update_time_labels(); }
static void sched_hh_up_cb(lv_event_t* e) { last_touch_time = millis(); g_ready_hh[g_sched_day_idx] = (g_ready_hh[g_sched_day_idx] + 1) % 24; update_time_labels(); }
static void sched_hh_dn_cb(lv_event_t* e) { last_touch_time = millis(); g_ready_hh[g_sched_day_idx] = (g_ready_hh[g_sched_day_idx] + 23) % 24; update_time_labels(); }
static void sched_mm_up_cb(lv_event_t* e) { last_touch_time = millis(); g_ready_mm[g_sched_day_idx] = (g_ready_mm[g_sched_day_idx] + 5) % 60; update_time_labels(); }
static void sched_mm_dn_cb(lv_event_t* e) { last_touch_time = millis(); g_ready_mm[g_sched_day_idx] = (g_ready_mm[g_sched_day_idx] + 55) % 60; update_time_labels(); }

static void sched_auto_toggle_cb(lv_event_t* e) {
    last_touch_time = millis();
    g_auto_enabled = !g_auto_enabled;
    if (btn_sched_auto != NULL) lv_obj_set_style_bg_color(btn_sched_auto, g_auto_enabled ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_GREY), 0);
    if (lbl_sched_auto != NULL) lv_label_set_text(lbl_sched_auto, g_auto_enabled ? LV_SYMBOL_CHARGE "  Auto: ON" : LV_SYMBOL_CHARGE "  Auto: OFF");
    if (sched_body     != NULL) lv_obj_set_style_opa(sched_body, g_auto_enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    update_auto_badge();
}

static void sched_skip_cb(lv_event_t* e) {
    last_touch_time = millis();
    g_skip_today = !g_skip_today;
    if (lbl_skip_btn != NULL)
        lv_label_set_text(lbl_skip_btn, g_skip_today ? LV_SYMBOL_OK "  Auto skipped today" : LV_SYMBOL_NEXT "  Skip auto today");
}

static void goto_schedule_cb(lv_event_t* e) { last_touch_time = millis(); show_page(page_schedule); }
static void goto_settings_cb(lv_event_t* e) { last_touch_time = millis(); show_page(page_settings); }
static void goto_stats_cb(lv_event_t* e)    { last_touch_time = millis(); show_page(page_stats); }
static void goto_diagnostics_cb(lv_event_t* e) { last_touch_time = millis(); show_page(page_diagnostics); }

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
static void createScreensaver(lv_obj_t* parent) {
    screensaver = lv_obj_create(parent);
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

// ---------------------------------------------------------------------------
//  Diagnostics live-refresh timer (heap / uptime / RSSI — not packet-driven)
// ---------------------------------------------------------------------------
static void diag_timer_cb(lv_timer_t* t) {
    (void)t;
    if (lbl_diag_heap != NULL) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%u kB", (unsigned)(ESP.getFreeHeap() / 1024));
        lv_label_set_text(lbl_diag_heap, buf);
    }
    if (lbl_diag_uptime != NULL) {
        unsigned long s = millis() / 1000;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
        lv_label_set_text(lbl_diag_uptime, buf);
    }
    if (lbl_diag_rssi != NULL) {
        char buf[16];
        if (WiFi.status() == WL_CONNECTED) snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
        else                               snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(lbl_diag_rssi, buf);
    }
}

// ===========================================================================
//  Page builders
// ===========================================================================
static void build_home_page(lv_obj_t* scr) {
    page_home = lv_obj_create(scr);
    disableScroll(page_home);
    lv_obj_set_size(page_home, 800, 480);
    lv_obj_set_pos(page_home, 0, 0);
    lv_obj_set_style_bg_color(page_home, CLR_BG, 0);
    lv_obj_set_style_border_width(page_home, 0, 0);
    lv_obj_set_style_radius(page_home, 0, 0);
    lv_obj_set_style_pad_all(page_home, 0, 0);

    // --- Top bar (800 x 55) ---
    lv_obj_t* top_bar = lv_obj_create(page_home);
    disableScroll(top_bar);
    lv_obj_set_size(top_bar, 800, 55);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, CLR_TOPBAR, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_hor(top_bar, 14, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);
    lv_obj_set_style_pad_column(top_bar, 10, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* wifi_btn = lv_btn_create(top_bar);
    lv_obj_set_size(wifi_btn, 50, 42);
    lv_obj_set_style_radius(wifi_btn, 10, 0);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_icon_click_event_cb, LV_EVENT_CLICKED, NULL);
    lbl_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(lbl_wifi_icon);

    btn_auto_badge = lv_btn_create(top_bar);
    lv_obj_set_width(btn_auto_badge, LV_SIZE_CONTENT);
    lv_obj_set_height(btn_auto_badge, 42);
    lv_obj_set_style_radius(btn_auto_badge, 21, 0);
    lv_obj_set_style_bg_color(btn_auto_badge, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_border_width(btn_auto_badge, 0, 0);
    lv_obj_set_style_pad_hor(btn_auto_badge, 14, 0);
    lv_obj_add_event_cb(btn_auto_badge, goto_schedule_cb, LV_EVENT_CLICKED, NULL);
    lbl_auto_badge = lv_label_create(btn_auto_badge);
    lv_label_set_text(lbl_auto_badge, LV_SYMBOL_CHARGE "  Auto");
    lv_obj_set_style_text_font(lbl_auto_badge, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_auto_badge, lv_color_white(), 0);
    lv_obj_center(lbl_auto_badge);

    lv_obj_t* center_box = lv_obj_create(top_bar);
    disableScroll(center_box);
    lv_obj_set_flex_grow(center_box, 1);
    lv_obj_set_height(center_box, 48);
    lv_obj_set_style_bg_opa(center_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_box, 0, 0);
    lv_obj_set_style_pad_all(center_box, 0, 0);
    lv_obj_set_flex_flow(center_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_box, 12, 0);

    lbl_time = lv_label_create(center_box);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);

    lbl_date = lv_label_create(center_box);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xBBCCEE), 0);

    lv_obj_t* weather_row = lv_obj_create(top_bar);
    disableScroll(weather_row);
    lv_obj_set_size(weather_row, 90, 48);
    lv_obj_set_style_bg_opa(weather_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_row, 0, 0);
    lv_obj_set_style_pad_all(weather_row, 0, 0);
    lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather_row, 6, 0);

    lbl_weather_icon = lv_label_create(weather_row);
    lv_label_set_text(lbl_weather_icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(lbl_weather_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_weather_icon, lv_color_hex(0xFFD54F), 0);

    lbl_weather_temp = lv_label_create(weather_row);
    lv_label_set_text(lbl_weather_temp, "--\xc2\xb0");
    lv_obj_set_style_text_font(lbl_weather_temp, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_weather_temp, lv_color_white(), 0);

    // --- Middle row (800 x 270, y=55) ---
    lv_obj_t* mid_row = lv_obj_create(page_home);
    disableScroll(mid_row);
    lv_obj_set_size(mid_row, 800, 270);
    lv_obj_set_pos(mid_row, 0, 55);
    lv_obj_set_style_bg_opa(mid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_row, 0, 0);
    lv_obj_set_style_pad_hor(mid_row, 18, 0);
    lv_obj_set_style_pad_ver(mid_row, 16, 0);
    lv_obj_set_flex_flow(mid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mid_row, 18, 0);

    lv_obj_t* circle_col = lv_obj_create(mid_row);
    disableScroll(circle_col);
    lv_obj_set_size(circle_col, 340, 230);
    lv_obj_set_style_bg_opa(circle_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle_col, 0, 0);
    lv_obj_set_style_pad_all(circle_col, 0, 0);
    lv_obj_set_flex_flow(circle_col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(circle_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    temp_circle_obj = lv_obj_create(circle_col);
    disableScroll(temp_circle_obj);
    lv_obj_set_size(temp_circle_obj, 218, 218);
    lv_obj_set_style_radius(temp_circle_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(temp_circle_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(temp_circle_obj, 8, 0);
    lv_obj_set_style_border_color(temp_circle_obj, CLR_ACCENT, 0);
    lv_obj_set_style_shadow_width(temp_circle_obj, 24, 0);
    lv_obj_set_style_shadow_opa(temp_circle_obj, LV_OPA_20, 0);
    lv_obj_set_flex_flow(temp_circle_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(temp_circle_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(temp_circle_obj, 4, 0);

    lv_obj_t* lbl_water_title = lv_label_create(temp_circle_obj);
    lv_label_set_text(lbl_water_title, "Water Temp");
    lv_obj_set_style_text_font(lbl_water_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_water_title, CLR_SUBTEXT, 0);

    lbl_water_temp = lv_label_create(temp_circle_obj);
    lv_label_set_text(lbl_water_temp, "--\xc2\xb0");
    lv_obj_set_style_text_font(lbl_water_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_water_temp, CLR_ACCENT, 0);

    lbl_target_inline = lv_label_create(temp_circle_obj);
    lv_label_set_text(lbl_target_inline, "Target 60\xc2\xb0");
    lv_obj_set_style_text_font(lbl_target_inline, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_target_inline, CLR_SUBTEXT, 0);

    led_heating = lv_led_create(temp_circle_obj);
    lv_obj_set_size(led_heating, 1, 1);   // kept for API compatibility, not shown
    lv_obj_add_flag(led_heating, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* right_col = lv_obj_create(mid_row);
    disableScroll(right_col);
    lv_obj_set_flex_grow(right_col, 1);
    lv_obj_set_height(right_col, 230);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 0, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right_col, 14, 0);

    btn_power = lv_btn_create(right_col);
    disableScroll(btn_power);
    lv_obj_set_size(btn_power, lv_pct(100), 150);
    lv_obj_set_style_radius(btn_power, 28, 0);
    lv_obj_set_style_bg_color(btn_power, CLR_OFF, 0);
    lv_obj_set_style_shadow_width(btn_power, 24, 0);
    lv_obj_set_style_shadow_opa(btn_power, LV_OPA_30, 0);
    lv_obj_add_event_cb(btn_power, power_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_power_status = lv_label_create(btn_power);
    lv_label_set_text(lbl_power_status, "OFF");
    lv_obj_set_style_text_font(lbl_power_status, &lv_font_montserrat_46, 0);
    lv_obj_set_style_text_color(lbl_power_status, lv_color_white(), 0);
    lv_obj_center(lbl_power_status);

    lv_obj_t* shower_card = lv_obj_create(right_col);
    disableScroll(shower_card);
    lv_obj_set_size(shower_card, lv_pct(100), 84);
    lv_obj_set_style_radius(shower_card, 20, 0);
    lv_obj_set_style_bg_color(shower_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(shower_card, 2, 0);
    lv_obj_set_style_border_color(shower_card, CLR_BORDER, 0);
    lv_obj_set_flex_flow(shower_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shower_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(shower_card, 10, 0);

    lv_obj_t* droplet = lv_label_create(shower_card);
    lv_label_set_text(droplet, LV_SYMBOL_TINT);
    lv_obj_set_style_text_font(droplet, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(droplet, CLR_SUBTEXT, 0);

    lbl_shower_readiness = lv_label_create(shower_card);
    lv_label_set_text(lbl_shower_readiness, "Connecting...");
    lv_obj_set_style_text_font(lbl_shower_readiness, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_shower_readiness, CLR_SUBTEXT, 0);

    // --- Tile row (800 x 155, y=325) ---
    lv_obj_t* tile_row = lv_obj_create(page_home);
    disableScroll(tile_row);
    lv_obj_set_size(tile_row, 800, 155);
    lv_obj_set_pos(tile_row, 0, 325);
    lv_obj_set_style_bg_opa(tile_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tile_row, 0, 0);
    lv_obj_set_style_pad_hor(tile_row, 18, 0);
    lv_obj_set_style_pad_ver(tile_row, 0, 0);
    lv_obj_set_flex_flow(tile_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile_row, 18, 0);

    make_tile(tile_row, LV_SYMBOL_SETTINGS, "Settings",
              lv_color_hex(0xE3F2FD), lv_color_hex(0xBBDEFB), lv_color_hex(0x0C447C), goto_settings_cb);
    make_tile(tile_row, LV_SYMBOL_WIFI, "Network",
              lv_color_hex(0xE8F5E9), lv_color_hex(0xC8E6C9), lv_color_hex(0x1B5E20), goto_network_cb);
    make_tile(tile_row, LV_SYMBOL_BARS, "Stats",
              lv_color_hex(0xFFF3E0), lv_color_hex(0xFFE0B2), lv_color_hex(0xBF360C), goto_stats_cb);

    lv_obj_add_event_cb(page_home, screen_touched_cb, LV_EVENT_PRESSED, NULL);
}

static void build_settings_page(lv_obj_t* scr) {
    page_settings = lv_obj_create(scr);
    disableScroll(page_settings);
    lv_obj_set_size(page_settings, 800, 480);
    lv_obj_set_pos(page_settings, 0, 0);
    lv_obj_set_style_bg_color(page_settings, CLR_BG, 0);
    lv_obj_set_style_border_width(page_settings, 0, 0);
    lv_obj_set_style_radius(page_settings, 0, 0);
    lv_obj_set_style_pad_all(page_settings, 0, 0);
    lv_obj_add_flag(page_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_settings, CLR_TOPBAR);
    make_back_btn(header, page_home);
    make_header_title(header, "Settings");

    lv_obj_t* body = lv_obj_create(page_settings);
    disableScroll(body);
    lv_obj_set_size(body, 800, 416);
    lv_obj_set_pos(body, 0, 64);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 22, 0);

    lv_obj_t* lbl_target_title = lv_label_create(body);
    lv_label_set_text(lbl_target_title, "Target temperature");
    lv_obj_set_style_text_font(lbl_target_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_target_title, CLR_SUBTEXT, 0);
    lv_obj_align(lbl_target_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* ctrl_row = lv_obj_create(body);
    disableScroll(ctrl_row);
    lv_obj_set_size(ctrl_row, lv_pct(100), 130);
    lv_obj_set_pos(ctrl_row, 0, 36);
    lv_obj_set_style_bg_color(ctrl_row, lv_color_white(), 0);
    lv_obj_set_style_border_width(ctrl_row, 2, 0);
    lv_obj_set_style_border_color(ctrl_row, CLR_BORDER, 0);
    lv_obj_set_style_radius(ctrl_row, 24, 0);
    lv_obj_set_style_pad_hor(ctrl_row, 20, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_temp_down = lv_btn_create(ctrl_row);
    lv_obj_set_size(btn_temp_down, 92, 92);
    lv_obj_set_style_radius(btn_temp_down, 22, 0);
    lv_obj_set_style_bg_color(btn_temp_down, CLR_BORDER, 0);
    lv_obj_add_event_cb(btn_temp_down, temp_down_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_dn = lv_label_create(btn_temp_down);
    lv_label_set_text(lbl_dn, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(lbl_dn, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_dn, CLR_TEXT, 0);
    lv_obj_center(lbl_dn);

    lv_obj_t* target_col = lv_obj_create(ctrl_row);
    disableScroll(target_col);
    lv_obj_set_size(target_col, 220, 110);
    lv_obj_set_style_bg_opa(target_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(target_col, 0, 0);
    lv_obj_set_style_pad_all(target_col, 0, 0);
    lv_obj_set_flex_flow(target_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(target_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lbl_target_temp = lv_label_create(target_col);
    char tgt_buf[8];
    snprintf(tgt_buf, sizeof(tgt_buf), "%d\xc2\xb0", target_temperature);
    lv_label_set_text(lbl_target_temp, tgt_buf);
    lv_obj_set_style_text_font(lbl_target_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_target_temp, CLR_ACCENT, 0);

    lv_obj_t* lbl_range = lv_label_create(target_col);
    lv_label_set_text(lbl_range, "range 30" "\xe2\x80\x93" "80" "\xc2\xb0");
    lv_obj_set_style_text_font(lbl_range, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_range, CLR_SUBTEXT, 0);

    btn_temp_up = lv_btn_create(ctrl_row);
    lv_obj_set_size(btn_temp_up, 92, 92);
    lv_obj_set_style_radius(btn_temp_up, 22, 0);
    lv_obj_set_style_bg_color(btn_temp_up, CLR_ACCENT, 0);
    lv_obj_add_event_cb(btn_temp_up, temp_up_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_up = lv_label_create(btn_temp_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(lbl_up, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_up, lv_color_white(), 0);
    lv_obj_center(lbl_up);

    lv_obj_t* nav_row = lv_obj_create(body);
    disableScroll(nav_row);
    lv_obj_set_size(nav_row, lv_pct(100), 92);
    lv_obj_set_pos(nav_row, 0, 186);
    lv_obj_set_style_bg_opa(nav_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nav_row, 0, 0);
    lv_obj_set_style_pad_all(nav_row, 0, 0);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nav_row, 16, 0);

    auto make_settings_nav_btn = [&](const char* icon, const char* text, lv_color_t fg, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_btn_create(nav_row);
        disableScroll(btn);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, 92);
        lv_obj_set_style_radius(btn, 20, 0);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, CLR_BORDER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 12, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, icon);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(icon_lbl, fg, 0);

        lv_obj_t* text_lbl = lv_label_create(btn);
        lv_label_set_text(text_lbl, text);
        lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(text_lbl, fg, 0);
    };

    make_settings_nav_btn(LV_SYMBOL_LIST, "Schedule", CLR_ACCENT, goto_schedule_cb);
    make_settings_nav_btn(LV_SYMBOL_EDIT, "Diagnostics", CLR_SUBTEXT, goto_diagnostics_cb);
}

static lv_obj_t* make_seg_btn(lv_obj_t* parent, const char* text, lv_event_cb_t cb, lv_obj_t** out_lbl) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    if (out_lbl) *out_lbl = lbl;
    return btn;
}

static void build_schedule_page(lv_obj_t* scr) {
    page_schedule = lv_obj_create(scr);
    disableScroll(page_schedule);
    lv_obj_set_size(page_schedule, 800, 480);
    lv_obj_set_pos(page_schedule, 0, 0);
    lv_obj_set_style_bg_color(page_schedule, CLR_BG, 0);
    lv_obj_set_style_border_width(page_schedule, 0, 0);
    lv_obj_set_style_radius(page_schedule, 0, 0);
    lv_obj_set_style_pad_all(page_schedule, 0, 0);
    lv_obj_add_flag(page_schedule, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_schedule, CLR_TOPBAR);
    make_back_btn(header, page_settings);
    make_header_title(header, LV_SYMBOL_LIST "  Schedule");

    btn_sched_auto = lv_btn_create(header);
    lv_obj_set_width(btn_sched_auto, LV_SIZE_CONTENT);
    lv_obj_set_height(btn_sched_auto, 48);
    lv_obj_set_style_radius(btn_sched_auto, 24, 0);
    lv_obj_set_style_bg_color(btn_sched_auto, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_border_width(btn_sched_auto, 0, 0);
    lv_obj_set_style_pad_hor(btn_sched_auto, 18, 0);
    lv_obj_add_event_cb(btn_sched_auto, sched_auto_toggle_cb, LV_EVENT_CLICKED, NULL);
    lbl_sched_auto = lv_label_create(btn_sched_auto);
    lv_label_set_text(lbl_sched_auto, LV_SYMBOL_CHARGE "  Auto: ON");
    lv_obj_set_style_text_font(lbl_sched_auto, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_sched_auto, lv_color_white(), 0);
    lv_obj_center(lbl_sched_auto);

    sched_body = lv_obj_create(page_schedule);
    disableScroll(sched_body);
    lv_obj_set_size(sched_body, 800, 416);
    lv_obj_set_pos(sched_body, 0, 64);
    lv_obj_set_style_bg_opa(sched_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sched_body, 0, 0);
    lv_obj_set_style_pad_all(sched_body, 18, 0);
    lv_obj_set_flex_flow(sched_body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sched_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(sched_body, 18, 0);

    // --- Left column: mode selector + Ready-by / Smart-learn panels ---
    lv_obj_t* left_col = lv_obj_create(sched_body);
    disableScroll(left_col);
    lv_obj_set_size(left_col, 372, lv_pct(100));
    lv_obj_set_style_bg_opa(left_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_col, 0, 0);
    lv_obj_set_style_pad_all(left_col, 0, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left_col, 14, 0);

    lv_obj_t* seg_mode = lv_obj_create(left_col);
    disableScroll(seg_mode);
    lv_obj_set_size(seg_mode, lv_pct(100), 64);
    lv_obj_set_style_bg_color(seg_mode, CLR_BORDER, 0);
    lv_obj_set_style_border_width(seg_mode, 0, 0);
    lv_obj_set_style_radius(seg_mode, 16, 0);
    lv_obj_set_style_pad_all(seg_mode, 5, 0);
    lv_obj_set_flex_flow(seg_mode, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(seg_mode, 6, 0);

    btn_mode_smart = make_seg_btn(seg_mode, "Smart learn", sched_mode_smart_cb, &lbl_mode_smart);
    btn_mode_ready = make_seg_btn(seg_mode, "Ready by", sched_mode_ready_cb, &lbl_mode_ready);

    // Ready-by panel
    panel_ready = lv_obj_create(left_col);
    disableScroll(panel_ready);
    lv_obj_set_size(panel_ready, lv_pct(100), 220);
    lv_obj_set_style_bg_opa(panel_ready, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_ready, 0, 0);
    lv_obj_set_style_pad_all(panel_ready, 0, 0);
    lv_obj_set_flex_flow(panel_ready, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel_ready, 12, 0);

    lv_obj_t* seg_day = lv_obj_create(panel_ready);
    disableScroll(seg_day);
    lv_obj_set_size(seg_day, lv_pct(100), 54);
    lv_obj_set_style_bg_color(seg_day, lv_color_white(), 0);
    lv_obj_set_style_border_width(seg_day, 2, 0);
    lv_obj_set_style_border_color(seg_day, CLR_BORDER, 0);
    lv_obj_set_style_radius(seg_day, 14, 0);
    lv_obj_set_style_pad_all(seg_day, 5, 0);
    lv_obj_set_flex_flow(seg_day, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(seg_day, 6, 0);

    btn_day_wk = make_seg_btn(seg_day, "Mon-Fri", sched_day_wk_cb, &lbl_day_wk);
    btn_day_we = make_seg_btn(seg_day, "Sat-Sun", sched_day_we_cb, &lbl_day_we);

    lv_obj_t* time_card = lv_obj_create(panel_ready);
    disableScroll(time_card);
    lv_obj_set_size(time_card, lv_pct(100), 140);
    lv_obj_set_style_bg_color(time_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(time_card, 2, 0);
    lv_obj_set_style_border_color(time_card, CLR_BORDER, 0);
    lv_obj_set_style_radius(time_card, 18, 0);
    lv_obj_set_flex_flow(time_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_stepper = [&](lv_event_cb_t up_cb, lv_event_cb_t dn_cb, lv_obj_t** out_lbl) {
        lv_obj_t* col = lv_obj_create(time_card);
        disableScroll(col);
        lv_obj_set_size(col, 70, 120);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* up = lv_btn_create(col);
        lv_obj_set_size(up, 64, 40);
        lv_obj_set_style_radius(up, 12, 0);
        lv_obj_set_style_bg_color(up, lv_color_hex(0xE3F2FD), 0);
        lv_obj_set_style_shadow_width(up, 0, 0);
        lv_obj_add_event_cb(up, up_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t* up_lbl = lv_label_create(up);
        lv_label_set_text(up_lbl, LV_SYMBOL_UP);
        lv_obj_set_style_text_color(up_lbl, CLR_ACCENT, 0);
        lv_obj_center(up_lbl);

        lv_obj_t* val = lv_label_create(col);
        lv_label_set_text(val, "00");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_48, 0);
        if (out_lbl) *out_lbl = val;

        lv_obj_t* dn = lv_btn_create(col);
        lv_obj_set_size(dn, 64, 40);
        lv_obj_set_style_radius(dn, 12, 0);
        lv_obj_set_style_bg_color(dn, lv_color_hex(0xE3F2FD), 0);
        lv_obj_set_style_shadow_width(dn, 0, 0);
        lv_obj_add_event_cb(dn, dn_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t* dn_lbl = lv_label_create(dn);
        lv_label_set_text(dn_lbl, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_color(dn_lbl, CLR_ACCENT, 0);
        lv_obj_center(dn_lbl);
    };

    make_stepper(sched_hh_up_cb, sched_hh_dn_cb, &lbl_hh);
    lv_obj_t* colon = lv_label_create(time_card);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_42, 0);
    lv_obj_set_style_text_color(colon, CLR_SUBTEXT, 0);
    make_stepper(sched_mm_up_cb, sched_mm_dn_cb, &lbl_mm);

    // Smart-learn panel (placeholder — no learning backend on this branch yet)
    panel_smart = lv_obj_create(left_col);
    disableScroll(panel_smart);
    lv_obj_set_size(panel_smart, lv_pct(100), 220);
    lv_obj_set_style_bg_opa(panel_smart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_smart, 0, 0);
    lv_obj_set_style_pad_all(panel_smart, 0, 0);
    lv_obj_set_flex_flow(panel_smart, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel_smart, 10, 0);
    lv_obj_add_flag(panel_smart, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* learn_card = lv_obj_create(panel_smart);
    disableScroll(learn_card);
    lv_obj_set_size(learn_card, lv_pct(100), 130);
    lv_obj_set_style_bg_color(learn_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(learn_card, 2, 0);
    lv_obj_set_style_border_color(learn_card, CLR_BORDER, 0);
    lv_obj_set_style_radius(learn_card, 18, 0);
    lv_obj_set_style_pad_all(learn_card, 16, 0);

    lv_obj_t* learn_title = lv_label_create(learn_card);
    lv_label_set_text(learn_title, "Learning your routine");
    lv_obj_set_style_text_font(learn_title, &lv_font_montserrat_18, 0);
    lv_obj_align(learn_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* learn_sub = lv_label_create(learn_card);
    lv_label_set_text(learn_sub, "Not available yet on this firmware build.");
    lv_obj_set_style_text_font(learn_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(learn_sub, CLR_SUBTEXT, 0);
    lv_obj_align(learn_sub, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t* learn_bar = lv_obj_create(learn_card);
    disableScroll(learn_bar);
    lv_obj_set_size(learn_bar, lv_pct(100), 14);
    lv_obj_align(learn_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(learn_bar, CLR_BORDER, 0);
    lv_obj_set_style_border_width(learn_bar, 0, 0);
    lv_obj_set_style_radius(learn_bar, 7, 0);

    // --- Right column: next pre-heat estimate + skip-today ---
    lv_obj_t* right_col = lv_obj_create(sched_body);
    disableScroll(right_col);
    lv_obj_set_size(right_col, 392, lv_pct(100));
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 0, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right_col, 14, 0);

    lv_obj_t* next_card = lv_obj_create(right_col);
    disableScroll(next_card);
    lv_obj_set_size(next_card, lv_pct(100), 150);
    lv_obj_set_style_bg_color(next_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(next_card, 2, 0);
    lv_obj_set_style_border_color(next_card, lv_color_hex(0xBBDEFB), 0);
    lv_obj_set_style_radius(next_card, 20, 0);
    lv_obj_set_style_pad_all(next_card, 20, 0);

    lv_obj_t* next_title = lv_label_create(next_card);
    lv_label_set_text(next_title, "Next auto pre-heat");
    lv_obj_set_style_text_font(next_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(next_title, CLR_SUBTEXT, 0);
    lv_obj_align(next_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_next_preheat = lv_label_create(next_card);
    lv_label_set_text(lbl_next_preheat, "06:45");
    lv_obj_set_style_text_font(lbl_next_preheat, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_next_preheat, CLR_ACCENT, 0);
    lv_obj_align(lbl_next_preheat, LV_ALIGN_TOP_LEFT, 0, 32);

    lv_obj_t* next_sub = lv_label_create(next_card);
    char lead_buf[40];
    snprintf(lead_buf, sizeof(lead_buf), "%d min pre-heat lead (estimate)", LEAD_TIME_MIN);
    lv_label_set_text(next_sub, lead_buf);
    lv_obj_set_style_text_font(next_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(next_sub, CLR_SUBTEXT, 0);
    lv_obj_align(next_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t* skip_btn = lv_btn_create(right_col);
    disableScroll(skip_btn);
    lv_obj_set_size(skip_btn, lv_pct(100), 66);
    lv_obj_set_style_radius(skip_btn, 18, 0);
    lv_obj_set_style_bg_color(skip_btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(skip_btn, 2, 0);
    lv_obj_set_style_border_color(skip_btn, lv_color_hex(0xF0997B), 0);
    lv_obj_set_style_shadow_width(skip_btn, 0, 0);
    lv_obj_add_event_cb(skip_btn, sched_skip_cb, LV_EVENT_CLICKED, NULL);
    lbl_skip_btn = lv_label_create(skip_btn);
    lv_label_set_text(lbl_skip_btn, LV_SYMBOL_NEXT "  Skip auto today");
    lv_obj_set_style_text_font(lbl_skip_btn, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_skip_btn, lv_color_hex(0x993C1D), 0);
    lv_obj_center(lbl_skip_btn);

    update_mode_buttons();
    update_day_buttons();
    update_time_labels();
}

static void build_network_page(lv_obj_t* scr) {
    page_network = lv_obj_create(scr);
    disableScroll(page_network);
    lv_obj_set_size(page_network, 800, 480);
    lv_obj_set_pos(page_network, 0, 0);
    lv_obj_set_style_bg_color(page_network, CLR_BG, 0);
    lv_obj_set_style_border_width(page_network, 0, 0);
    lv_obj_set_style_radius(page_network, 0, 0);
    lv_obj_set_style_pad_all(page_network, 0, 0);
    lv_obj_add_flag(page_network, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_network, CLR_TOPBAR);
    make_back_btn(header, page_home);
    make_header_title(header, LV_SYMBOL_WIFI "  Network");

    lv_obj_t* scan_btn = lv_btn_create(header);
    lv_obj_set_width(scan_btn, LV_SIZE_CONTENT);
    lv_obj_set_height(scan_btn, 48);
    lv_obj_set_style_radius(scan_btn, 12, 0);
    lv_obj_set_style_bg_color(scan_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scan_btn, LV_OPA_30, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    lv_obj_set_style_pad_hor(scan_btn, 20, 0);
    lv_obj_add_event_cb(scan_btn, net_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(scan_lbl, lv_color_white(), 0);
    lv_obj_center(scan_lbl);

    lbl_net_status = lv_label_create(page_network);
    lv_label_set_text(lbl_net_status, LV_SYMBOL_WARNING "  Not connected");
    lv_obj_set_style_text_font(lbl_net_status, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_net_status, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_pos(lbl_net_status, 18, 74);

    net_list_container = lv_obj_create(page_network);
    lv_obj_set_size(net_list_container, 764, 330);
    lv_obj_set_pos(net_list_container, 18, 112);
    lv_obj_set_style_bg_opa(net_list_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(net_list_container, 0, 0);
    lv_obj_set_style_pad_all(net_list_container, 0, 0);
    lv_obj_set_flex_flow(net_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(net_list_container, 10, 0);
}

static void build_password_page(lv_obj_t* scr) {
    page_password = lv_obj_create(scr);
    disableScroll(page_password);
    lv_obj_set_size(page_password, 800, 480);
    lv_obj_set_pos(page_password, 0, 0);
    lv_obj_set_style_bg_color(page_password, CLR_BG, 0);
    lv_obj_set_style_border_width(page_password, 0, 0);
    lv_obj_set_style_radius(page_password, 0, 0);
    lv_obj_set_style_pad_all(page_password, 0, 0);
    lv_obj_add_flag(page_password, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_password, CLR_TOPBAR);
    lv_obj_set_size(header, 800, 60);
    make_back_btn(header, page_network);
    lbl_pwd_title = make_header_title(header, "Connect");
    lv_obj_set_style_text_font(lbl_pwd_title, &lv_font_montserrat_22, 0);

    lv_obj_t* field_row = lv_obj_create(page_password);
    disableScroll(field_row);
    lv_obj_set_size(field_row, 776, 58);
    lv_obj_set_pos(field_row, 12, 70);
    lv_obj_set_style_bg_opa(field_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(field_row, 0, 0);
    lv_obj_set_style_pad_all(field_row, 0, 0);
    lv_obj_set_flex_flow(field_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(field_row, 10, 0);

    password_textarea = lv_textarea_create(field_row);
    lv_obj_set_flex_grow(password_textarea, 1);
    lv_obj_set_height(password_textarea, 52);
    lv_textarea_set_one_line(password_textarea, true);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_textarea_set_placeholder_text(password_textarea, "Enter password");
    lv_obj_set_style_text_font(password_textarea, &lv_font_montserrat_20, 0);
    lv_obj_set_style_border_color(password_textarea, CLR_ACCENT, 0);
    lv_obj_set_style_radius(password_textarea, 12, 0);

    lv_obj_t* eye_btn = lv_btn_create(field_row);
    lv_obj_set_size(eye_btn, 50, 50);
    lv_obj_set_style_radius(eye_btn, 10, 0);
    lv_obj_set_style_bg_color(eye_btn, lv_color_hex(0xE8F0FE), 0);
    lv_obj_set_style_shadow_width(eye_btn, 0, 0);
    lv_obj_add_event_cb(eye_btn, pwd_eye_toggle_cb, LV_EVENT_CLICKED, NULL);
    lbl_eye_icon = lv_label_create(eye_btn);
    lv_label_set_text(lbl_eye_icon, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(lbl_eye_icon, CLR_ACCENT, 0);
    lv_obj_center(lbl_eye_icon);

    btn_connect = lv_btn_create(field_row);
    lv_obj_set_size(btn_connect, 130, 52);
    lv_obj_set_style_radius(btn_connect, 12, 0);
    lv_obj_set_style_bg_color(btn_connect, CLR_ON, 0);
    lv_obj_set_style_shadow_width(btn_connect, 0, 0);
    lv_obj_add_event_cb(btn_connect, pwd_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lbl_connect_btn_text = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect_btn_text, "Connect");
    lv_obj_set_style_text_font(lbl_connect_btn_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_connect_btn_text, lv_color_white(), 0);
    lv_obj_center(lbl_connect_btn_text);

    lbl_pwd_error = lv_label_create(page_password);
    lv_label_set_text(lbl_pwd_error, "");
    lv_obj_set_style_text_font(lbl_pwd_error, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_pwd_error, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_pos(lbl_pwd_error, 12, 130);

    keyboard = lv_keyboard_create(page_password);
    lv_obj_set_size(keyboard, 800, 280);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, password_textarea);

    // Post-connect "save password?" modal — replaces the old pre-connect checkbox
    save_modal = lv_obj_create(page_password);
    disableScroll(save_modal);
    lv_obj_set_size(save_modal, 800, 480);
    lv_obj_set_pos(save_modal, 0, 0);
    lv_obj_set_style_bg_color(save_modal, lv_color_hex(0x0A0C14), 0);
    lv_obj_set_style_bg_opa(save_modal, LV_OPA_50, 0);
    lv_obj_set_style_border_width(save_modal, 0, 0);
    lv_obj_set_style_radius(save_modal, 0, 0);
    lv_obj_add_flag(save_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* modal_card = lv_obj_create(save_modal);
    disableScroll(modal_card);
    lv_obj_set_size(modal_card, 480, 260);
    lv_obj_center(modal_card);
    lv_obj_set_style_bg_color(modal_card, lv_color_white(), 0);
    lv_obj_set_style_radius(modal_card, 24, 0);
    lv_obj_set_style_pad_all(modal_card, 24, 0);

    lv_obj_t* modal_title = lv_label_create(modal_card);
    lv_label_set_text(modal_title, "Connected");
    lv_obj_set_style_text_font(modal_title, &lv_font_montserrat_24, 0);
    lv_obj_align(modal_title, LV_ALIGN_TOP_MID, 0, 0);

    lbl_save_modal_ssid = lv_label_create(modal_card);
    lv_label_set_text(lbl_save_modal_ssid, "");
    lv_obj_set_style_text_font(lbl_save_modal_ssid, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_save_modal_ssid, CLR_SUBTEXT, 0);
    lv_obj_align(lbl_save_modal_ssid, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t* modal_sub = lv_label_create(modal_card);
    lv_label_set_text(modal_sub, "Save this password so you won't\nneed to type it again next time?");
    lv_obj_set_style_text_font(modal_sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(modal_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(modal_sub, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t* modal_btn_row = lv_obj_create(modal_card);
    disableScroll(modal_btn_row);
    lv_obj_set_size(modal_btn_row, lv_pct(100), 64);
    lv_obj_align(modal_btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(modal_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(modal_btn_row, 0, 0);
    lv_obj_set_style_pad_all(modal_btn_row, 0, 0);
    lv_obj_set_flex_flow(modal_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modal_btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modal_btn_row, 14, 0);

    lv_obj_t* btn_not_now = lv_btn_create(modal_btn_row);
    lv_obj_set_flex_grow(btn_not_now, 1);
    lv_obj_set_height(btn_not_now, 64);
    lv_obj_set_style_radius(btn_not_now, 16, 0);
    lv_obj_set_style_bg_color(btn_not_now, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn_not_now, 2, 0);
    lv_obj_set_style_border_color(btn_not_now, lv_color_hex(0xCFD8DC), 0);
    lv_obj_set_style_shadow_width(btn_not_now, 0, 0);
    lv_obj_add_event_cb(btn_not_now, save_modal_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_not_now = lv_label_create(btn_not_now);
    lv_label_set_text(lbl_not_now, "Not now");
    lv_obj_set_style_text_color(lbl_not_now, lv_color_hex(0x546E7A), 0);
    lv_obj_center(lbl_not_now);

    lv_obj_t* btn_save = lv_btn_create(modal_btn_row);
    lv_obj_set_flex_grow(btn_save, 1);
    lv_obj_set_height(btn_save, 64);
    lv_obj_set_style_radius(btn_save, 16, 0);
    lv_obj_set_style_bg_color(btn_save, CLR_ON, 0);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, save_modal_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_SAVE "  Save");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);
}

static void build_stats_page(lv_obj_t* scr) {
    page_stats = lv_obj_create(scr);
    disableScroll(page_stats);
    lv_obj_set_size(page_stats, 800, 480);
    lv_obj_set_pos(page_stats, 0, 0);
    lv_obj_set_style_bg_color(page_stats, CLR_BG, 0);
    lv_obj_set_style_border_width(page_stats, 0, 0);
    lv_obj_set_style_radius(page_stats, 0, 0);
    lv_obj_set_style_pad_all(page_stats, 0, 0);
    lv_obj_add_flag(page_stats, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_stats, CLR_TOPBAR);
    make_back_btn(header, page_home);
    make_header_title(header, LV_SYMBOL_BARS "  Stats");

    lv_obj_t* row1 = lv_obj_create(page_stats);
    disableScroll(row1);
    lv_obj_set_size(row1, 768, 100);
    lv_obj_set_pos(row1, 16, 80);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 12, 0);

    lbl_system_mode = make_stat_card(row1, "System Mode", CLR_ACCENT);
    lbl_stat_power  = make_stat_card(row1, "Power now", lv_palette_main(LV_PALETTE_ORANGE));
    lbl_stat_flow   = make_stat_card(row1, "Flow now", CLR_ACCENT);
    lbl_stat_plc    = make_stat_card(row1, "PLC Link", lv_palette_main(LV_PALETTE_RED));

    lv_obj_t* progress_card = lv_obj_create(page_stats);
    disableScroll(progress_card);
    lv_obj_set_size(progress_card, 768, 220);
    lv_obj_set_pos(progress_card, 16, 196);
    lv_obj_set_style_bg_color(progress_card, lv_color_white(), 0);
    lv_obj_set_style_border_width(progress_card, 2, 0);
    lv_obj_set_style_border_color(progress_card, CLR_BORDER, 0);
    lv_obj_set_style_radius(progress_card, 18, 0);
    lv_obj_set_style_pad_all(progress_card, 18, 0);

    lv_obj_t* progress_title = lv_label_create(progress_card);
    lv_label_set_text(progress_title, "Tank temperature vs. target");
    lv_obj_set_style_text_font(progress_title, &lv_font_montserrat_18, 0);
    lv_obj_align(progress_title, LV_ALIGN_TOP_LEFT, 0, 0);

    bar_temp_progress = lv_bar_create(progress_card);
    lv_obj_set_size(bar_temp_progress, lv_pct(100), 28);
    lv_obj_align(bar_temp_progress, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_bar_set_range(bar_temp_progress, 0, 100);
    lv_bar_set_value(bar_temp_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_temp_progress, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_temp_progress, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_temp_progress, 14, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_temp_progress, 14, LV_PART_INDICATOR);

    lbl_temp_progress_text = lv_label_create(progress_card);
    lv_label_set_text(lbl_temp_progress_text, "-- / -- " "\xc2\xb0" "C");
    lv_obj_set_style_text_font(lbl_temp_progress_text, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_temp_progress_text, CLR_TEXT, 0);
    lv_obj_align(lbl_temp_progress_text, LV_ALIGN_TOP_LEFT, 0, 96);
}

static void build_diagnostics_page(lv_obj_t* scr) {
    page_diagnostics = lv_obj_create(scr);
    disableScroll(page_diagnostics);
    lv_obj_set_size(page_diagnostics, 800, 480);
    lv_obj_set_pos(page_diagnostics, 0, 0);
    lv_obj_set_style_bg_color(page_diagnostics, lv_color_hex(0x1B2226), 0);
    lv_obj_set_style_border_width(page_diagnostics, 0, 0);
    lv_obj_set_style_radius(page_diagnostics, 0, 0);
    lv_obj_set_style_pad_all(page_diagnostics, 0, 0);
    lv_obj_add_flag(page_diagnostics, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = make_header(page_diagnostics, CLR_DIAG_HDR);
    make_back_btn(header, page_settings);
    make_header_title(header, LV_SYMBOL_EDIT "  Diagnostics");

    lv_obj_t* grid = lv_obj_create(page_diagnostics);
    disableScroll(grid);
    lv_obj_set_size(grid, 768, 396);
    lv_obj_set_pos(grid, 16, 70);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    static lv_coord_t col_dsc[] = {248, 248, 248, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {120, 120, 120, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);

    auto place = [&](lv_obj_t* card_value_owner, int col, int row) {
        lv_obj_t* card = lv_obj_get_parent(card_value_owner);
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    };

    lbl_diag_tint   = make_diag_card(grid, "T internal", lv_color_hex(0x4FC3F7));
    place(lbl_diag_tint, 0, 0);
    lbl_diag_tboost = make_diag_card(grid, "T boost out", lv_color_hex(0x4FC3F7));
    place(lbl_diag_tboost, 1, 0);
    lbl_diag_flow   = make_diag_card(grid, "Flow", lv_color_hex(0x81C784));
    place(lbl_diag_flow, 2, 0);
    lbl_diag_power  = make_diag_card(grid, "Power", lv_color_hex(0xFFB74D));
    place(lbl_diag_power, 0, 1);
    lbl_diag_plc    = make_diag_card(grid, "PLC", lv_color_hex(0xE57373));
    place(lbl_diag_plc, 1, 1);
    lbl_diag_ssr    = make_diag_card(grid, "SSR int/boost", lv_color_hex(0xB0BEC5));
    place(lbl_diag_ssr, 2, 1);
    lbl_diag_rssi   = make_diag_card(grid, "WiFi RSSI", lv_color_hex(0xB0BEC5));
    place(lbl_diag_rssi, 0, 2);
    lbl_diag_heap   = make_diag_card(grid, "Free heap", lv_color_hex(0xB0BEC5));
    place(lbl_diag_heap, 1, 2);
    lbl_diag_uptime = make_diag_card(grid, "Uptime", lv_color_hex(0xB0BEC5));
    place(lbl_diag_uptime, 2, 2);
}

// ===========================================================================
//  UI_Init  —  800 x 480, 7 pages (Home / Settings / Schedule / Network /
//  Password / Stats / Diagnostics), one shown at a time via show_page().
// ===========================================================================
void UI_Init() {
    Board* board = new Board();
    board->init();
    board->begin();
    lvgl_port_init(board->getLCD(), board->getTouch());

    lvgl_port_lock(-1);

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);

    build_home_page(scr);
    build_settings_page(scr);
    build_schedule_page(scr);
    build_network_page(scr);
    build_password_page(scr);
    build_stats_page(scr);
    build_diagnostics_page(scr);

    createScreensaver(scr);
    show_page(page_home);

    lv_timer_create(diag_timer_cb, 1000, NULL);

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

void UI_SetBoilerState(bool is_on) {
    boiler_state = is_on;
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (btn_power        != NULL) lv_obj_set_style_bg_color(btn_power, is_on ? CLR_ON : CLR_OFF, 0);
        if (lbl_power_status != NULL) lv_label_set_text(lbl_power_status, is_on ? "ON" : "OFF");
        lvgl_port_unlock();
    }
}

void UI_SetHeatingStatus(bool is_heating) {
    // Heating LED removed from the consumer Home page in v6 — kept as a
    // no-op for API compatibility. Use UI_UpdateSSRStatus for real status.
    (void)is_heating;
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

// Raw engineering values — superseded by UI_UpdateSensorData below
void UI_UpdateBoostTemp(float value)  { (void)value; }
void UI_UpdateFlowRate(float value)   { (void)value; }

void UI_UpdatePLCStatus(bool connected) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_stat_plc != NULL) {
            lv_label_set_text(lbl_stat_plc, connected ? "Connected" : "No Signal");
            lv_obj_set_style_text_color(lbl_stat_plc,
                connected ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
        }
        if (lbl_diag_plc != NULL) {
            lv_label_set_text(lbl_diag_plc, connected ? "OK" : "No Signal");
            lv_obj_set_style_text_color(lbl_diag_plc,
                connected ? lv_color_hex(0x81C784) : lv_color_hex(0xE57373), 0);
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
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if (lbl_diag_ssr != NULL) {
            char buf[20];
            snprintf(buf, sizeof(buf), "%s/%s", internal_on ? "on" : "off", boost_on ? "on" : "off");
            lv_label_set_text(lbl_diag_ssr, buf);
        }
        lvgl_port_unlock();
    }
}

// Called by TaskMasterComms every second with live slave packet fields
void UI_UpdateSensorData(float t_internal, float t_boost, float flow, float power_w) {
    // Minutes to target: P=2000 W, m=80 kg
    int wait_min = 0;
    if (target_temperature > (int)t_internal) {
        float delta = (float)target_temperature - t_internal;
        wait_min = (int)(delta * 80.0f * 4186.0f / (2000.0f * 60.0f));
    }
    bool showerReady = boiler_state && (t_internal >= (float)target_temperature - 3.0f);

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

        // Home shower-status line (single line, mirrors mockup)
        if (lbl_shower_readiness != NULL) {
            char msg[40];
            if (showerReady) {
                snprintf(msg, sizeof(msg), "Shower ready");
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_READY, 0);
            } else if (!boiler_state) {
                snprintf(msg, sizeof(msg), "Boiler is off");
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_SUBTEXT, 0);
            } else if (wait_min > 0) {
                snprintf(msg, sizeof(msg), "Heating up \xc2\xb7 ~%d min", wait_min);
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_WAITING, 0);
            } else {
                snprintf(msg, sizeof(msg), "Heating up...");
                lv_obj_set_style_text_color(lbl_shower_readiness, CLR_WAITING, 0);
            }
            lv_label_set_text(lbl_shower_readiness, msg);
        }

        // Stats (live status) page
        if (lbl_stat_power != NULL) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f W", power_w);
            lv_label_set_text(lbl_stat_power, buf);
        }
        if (lbl_stat_flow != NULL) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f L/min", flow);
            lv_label_set_text(lbl_stat_flow, buf);
        }
        if (bar_temp_progress != NULL && target_temperature > 0) {
            int pct = (int)(t_internal * 100.0f / (float)target_temperature);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            lv_bar_set_value(bar_temp_progress, pct, LV_ANIM_ON);
        }
        if (lbl_temp_progress_text != NULL) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%.0f / %d \xc2\xb0""C", t_internal, target_temperature);
            lv_label_set_text(lbl_temp_progress_text, buf);
        }

        // Diagnostics page
        if (lbl_diag_tint != NULL) {
            char buf[16]; snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", t_internal);
            lv_label_set_text(lbl_diag_tint, buf);
        }
        if (lbl_diag_tboost != NULL) {
            char buf[16]; snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", t_boost);
            lv_label_set_text(lbl_diag_tboost, buf);
        }
        if (lbl_diag_flow != NULL) {
            char buf[16]; snprintf(buf, sizeof(buf), "%.1f L/m", flow);
            lv_label_set_text(lbl_diag_flow, buf);
        }
        if (lbl_diag_power != NULL) {
            char buf[12]; snprintf(buf, sizeof(buf), "%.0f W", power_w);
            lv_label_set_text(lbl_diag_power, buf);
        }

        lvgl_port_unlock();
    }
}
