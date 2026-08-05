/**
 * @file      ui_calendar.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-06
 *
 */
#include "ui_define.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

void ui_calendar_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    /*Create a main page*/
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);


    lv_obj_t   *calendar = lv_calendar_create(main_page);
    lv_obj_set_size(calendar, lv_pct(100), lv_pct(100));
    lv_obj_align(calendar, LV_ALIGN_CENTER, 0, 27);
    // lv_obj_add_event_cb(calendar, event_handler, LV_EVENT_ALL, NULL);

    struct tm now = {};
    hw_get_date_time(now);
    const int year = now.tm_year + 1900;
    const int month = now.tm_mon + 1;
    const int day = now.tm_mday;

    lv_calendar_set_today_date(calendar, year, month, day);
    lv_calendar_set_showed_date(calendar, year, month);

    /*Highlight today*/
    static lv_calendar_date_t highlighted_days[1];       /*Only its pointer is stored, so it must be static*/
    highlighted_days[0].year = year;
    highlighted_days[0].month = month;
    highlighted_days[0].day = day;
    lv_calendar_set_highlighted_dates(calendar, highlighted_days, 1);

#if LV_USE_CALENDAR_HEADER_DROPDOWN
    lv_obj_t *header = lv_calendar_header_dropdown_create(calendar);
    lv_obj_t *year_dropdown = lv_obj_get_child(header, 0);
    // This LVGL release's built-in list ends at 2025. Prepend newer years in
    // descending order so its year-to-index calculation remains valid.
    for (int y = 2026; y <= 2035; ++y) {
        char year_text[5];
        snprintf(year_text, sizeof(year_text), "%d", y);
        lv_dropdown_add_option(year_dropdown, year_text, 0);
    }
    lv_calendar_set_showed_date(calendar, year, month);
#elif LV_USE_CALENDAR_HEADER_ARROW
    lv_calendar_header_arrow_create(calendar);
#endif

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
#endif

}


void ui_calendar_exit(lv_obj_t *parent)
{

}

app_t ui_calendar_main = {
    .setup_func_cb = ui_calendar_enter,
    .exit_func_cb = ui_calendar_exit,
    .user_data = nullptr,
};
