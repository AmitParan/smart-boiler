#include "ui_manager.h"
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h" // ודא שיש לך את הקובץ הזה מהפרויקט הקודם (דרייבר)
#include "config.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// מצביעים לתוויות על המסך
lv_obj_t* lbl_temps[3]; // נניח 3 חיישנים
lv_obj_t* lbl_flow_value = NULL;

// --- פונקציית עזר ליצירת כרטיס מעוצב ---
void create_card(lv_obj_t* parent, const char* title, lv_obj_t** label_ptr, const char* unit, bool is_temp) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 220, 160);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(card, 15, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // כותרת
    lv_obj_t * lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_transform_zoom(lbl_title, 340, 0);

    // מספר גדול
    *label_ptr = lv_label_create(card);
    lv_label_set_text(*label_ptr, "--.-");
    lv_obj_set_style_text_font(*label_ptr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(*label_ptr, 512, 0); // זום פי 2
    
    // צבע: אדום לטמפ', כחול למים
    if(is_temp) lv_obj_set_style_text_color(*label_ptr, lv_palette_main(LV_PALETTE_RED), 0);
    else lv_obj_set_style_text_color(*label_ptr, lv_palette_main(LV_PALETTE_BLUE), 0);
    
    // יחידה
    lv_obj_t * lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, unit);
    lv_obj_set_style_text_color(lbl_unit, lv_palette_main(LV_PALETTE_GREY), 0);
}

void UI_Init() {
    // אתחול החומרה של המסך (ספריית esp32_panel)
    Board *board = new Board();
    board->init();
    board->begin();
    lvgl_port_init(board->getLCD(), board->getTouch());

    // בניית הממשק
    lvgl_port_lock(-1);
    
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x202020), 0); // רקע כהה

    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont, 15, 0);
    lv_obj_set_style_border_width(cont, 0, 0);

    // יצירת כרטיסי טמפרטורה
    for(int i=0; i<3; i++) {
        char buf[32];
        sprintf(buf, "TEMP SENSOR %d", i+1);
        create_card(cont, buf, &lbl_temps[i], "Celsius", true);
    }

    // יצירת כרטיס זרימה
    create_card(cont, "WATER FLOW", &lbl_flow_value, "Liters/Min", false);

    lvgl_port_unlock();
}

void UI_UpdateTemp(int index, float value) {
    if(index > 2) return;
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        if(value > -100) {
            char buf[32];
            sprintf(buf, "%.1f", value);
            lv_label_set_text(lbl_temps[index], buf);
        } else {
            lv_label_set_text(lbl_temps[index], "ERR");
        }
        lvgl_port_unlock();
    }
}

void UI_UpdateFlow(float value) {
    if (lvgl_port_lock(UI_REFRESH_RATE)) {
        char buf[32];
        sprintf(buf, "%.1f", value);
        lv_label_set_text(lbl_flow_value, buf);
        lvgl_port_unlock();
    }
}