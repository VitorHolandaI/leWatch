/**
 * @file      ui_pedometer.cpp
 * @license   MIT
 *
 * Dedicated low-power step-counter screen. Drives the BMA423 on-chip pedometer
 * feature and dims the backlight so the watch can be left counting steps while
 * spending as little energy as possible.
 */
#include "ui_define.h"

// Backlight level used while counting. Low enough to save power, high enough to
// glance at the count. The previous level is restored on exit.
#define PEDOMETER_DIM_LEVEL   20
// Slow refresh: the count changes at human pace, so 2 s keeps the CPU mostly idle.
#define PEDOMETER_REFRESH_MS  2000

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *steps_label = NULL;
static lv_timer_t *timer = NULL;
static uint8_t saved_brightness = 0;

static void restore_and_close()
{
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    hw_pedometer_stop();
    hw_set_disp_backlight(saved_brightness);
    steps_label = NULL;
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        restore_and_close();
        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

static void reset_event_handler(lv_event_t *e)
{
    (void)e;
    hw_pedometer_reset();
    if (steps_label) {
        lv_label_set_text(steps_label, "0");
    }
}

static void pedometer_tick(lv_timer_t *t)
{
    (void)t;
    if (steps_label) {
        lv_label_set_text_fmt(steps_label, "%u", (unsigned)hw_pedometer_get_steps());
    }
}

void ui_pedometer_enter(lv_obj_t *parent)
{
    saved_brightness = hw_get_disp_backlight();
    hw_pedometer_reset();
    hw_pedometer_start();

    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(main_page);
    lv_label_set_text(title, "Steps");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    steps_label = lv_label_create(main_page);
    lv_obj_set_style_text_font(steps_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(steps_label, "0");
    lv_obj_align(steps_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *reset_btn = lv_btn_create(main_page);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(reset_btn, reset_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset");
    lv_obj_center(reset_lbl);

    timer = lv_timer_create(pedometer_tick, PEDOMETER_REFRESH_MS, NULL);

    lv_menu_set_page(menu, main_page);

    // Dim last so the menu is built at the normal level first.
    hw_set_disp_backlight(PEDOMETER_DIM_LEVEL);
}

void ui_pedometer_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_pedometer_main = {
    .setup_func_cb = ui_pedometer_enter,
    .exit_func_cb = ui_pedometer_exit,
    .user_data = nullptr,
};
