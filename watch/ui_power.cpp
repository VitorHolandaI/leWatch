/**
 * @file      ui_power.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static lv_obj_t *menu = NULL;

// ---- power-off confirmation overlay (triggered by a long power-key press) ----
static lv_obj_t *power_off_overlay = NULL;

static void power_off_do_shutdown()
{
    lv_obj_t *shutdown_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(shutdown_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(shutdown_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shutdown_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(shutdown_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(shutdown_overlay, LV_OBJ_FLAG_SCROLLABLE);

    LV_IMG_DECLARE(img_poweroff);
    lv_obj_t *image = lv_image_create(shutdown_overlay);
    lv_image_set_src(image, &img_poweroff);
    lv_obj_center(image);

    lv_obj_t *label = lv_label_create(shutdown_overlay);
    lv_label_set_text(label, "Power Off...");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_refr_now(NULL);
    lv_delay_ms(1500);
    hw_shutdown();
}

static void power_off_confirm_cb(lv_event_t *e)
{
    (void)e;
    power_off_do_shutdown();
}

static void power_off_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (power_off_overlay) {
        lv_obj_del(power_off_overlay);
        power_off_overlay = NULL;
    }
}

// Shown over everything when the power key is long-pressed. Asks the user to
// confirm before shutting down (a 4 s hold still triggers the AXP2101 hardware
// power-off independently).
void ui_power_off_show()
{
    if (power_off_overlay) {
        return;
    }

    power_off_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(power_off_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(power_off_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(power_off_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(power_off_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(power_off_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(power_off_overlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, LV_SYMBOL_POWER "  Power off?");

    lv_obj_t *row = lv_obj_create(power_off_overlay);
    lv_obj_set_size(row, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t *off_btn = lv_btn_create(row);
    lv_obj_add_event_cb(off_btn, power_off_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *off_lbl = lv_label_create(off_btn);
    lv_label_set_text(off_lbl, "Power Off");
    lv_obj_center(off_lbl);

    lv_obj_t *cancel_btn = lv_btn_create(row);
    lv_obj_add_event_cb(cancel_btn, power_off_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
}

static void event_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (obj == NULL) {
        return;
    }

    const char *text = lv_label_get_text(lv_obj_get_child(obj, 0));
    printf("Button %s clicked\n", text);
    if (strcmp(text, "Shutdown") == 0) {
        power_off_do_shutdown();

    } else if (strcmp(text, "Sleep") == 0) {
        hw_sleep();
    } else if (strcmp(text, "Close") == 0) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

void ui_power_enter(lv_obj_t *parent)
{
    bool is_small = is_screen_small();
    uint16_t btn_w = is_small ? 75 : 120;
    uint16_t btn_h = is_small ? 30 : 40;

    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *label = lv_label_create(main_page);
    lv_label_set_text(label, hw_get_device_power_tips_string());
    if (is_small) {
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    } else {
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    }
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_width(label, lv_pct(90));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btns_cont = lv_obj_create(main_page);
    lv_obj_set_scroll_dir(btns_cont, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(btns_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(btns_cont, lv_pct(99), 50);
    lv_obj_set_style_margin_top(btns_cont, is_small ? 15 : 80, 0);
    lv_obj_set_style_border_width(btns_cont, 0, 0);

    lv_obj_t *btn_shutdown = lv_btn_create(btns_cont);
    lv_obj_set_size(btn_shutdown, btn_w, btn_h);
    lv_obj_align(btn_shutdown, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *label_btn = lv_label_create(btn_shutdown);
    lv_label_set_text(label_btn, "Shutdown");
    lv_obj_center(label_btn);
    lv_obj_add_event_cb(btn_shutdown, event_cb, LV_EVENT_CLICKED, NULL);
    if (is_small) {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_14, 0);
    } else {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_24, 0);
    }


    lv_obj_t *btn_sleep = lv_btn_create(btns_cont);
    lv_obj_set_size(btn_sleep, btn_w, btn_h);
    lv_obj_align(btn_sleep, LV_ALIGN_CENTER, 0, 0);
    label_btn = lv_label_create(btn_sleep);
    lv_label_set_text(label_btn, "Sleep");
    lv_obj_center(label_btn);
    lv_obj_add_event_cb(btn_sleep, event_cb, LV_EVENT_CLICKED, NULL);
    if (is_small) {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_14, 0);
    } else {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_24, 0);
    }

    lv_obj_t *quit_btn = lv_btn_create(btns_cont);
    lv_obj_set_size(quit_btn, btn_w, btn_h);
    lv_obj_align(quit_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    label_btn = lv_label_create(quit_btn);
    lv_label_set_text(label_btn, "Close");
    lv_obj_center(label_btn);
    lv_obj_add_event_cb(quit_btn, event_cb, LV_EVENT_CLICKED, NULL);
    if (is_small) {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_14, 0);
    } else {
        lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_24, 0);
    }

    lv_menu_set_page(menu, main_page);
}

void ui_power_exit(lv_obj_t *parent)
{

}

app_t ui_power_main = {
    .setup_func_cb = ui_power_enter,
    .exit_func_cb = ui_power_exit,
    .user_data = nullptr,
};
