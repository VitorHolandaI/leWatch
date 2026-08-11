/**
 * @file      ui.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#include "ui_define.h"

LV_IMG_DECLARE(img_home_wallpaper);
LV_IMG_DECLARE(img_microphone);
LV_IMG_DECLARE(img_ir_remote);
LV_IMG_DECLARE(img_music);
LV_IMG_DECLARE(img_wifi);
LV_IMG_DECLARE(img_configuration);
LV_IMG_DECLARE(img_radio);
LV_IMG_DECLARE(img_gps);
LV_IMG_DECLARE(img_power);
LV_IMG_DECLARE(img_monitoring);
LV_IMG_DECLARE(img_calendar);
LV_IMG_DECLARE(img_keyboard);
LV_IMG_DECLARE(img_gyroscope);
LV_IMG_DECLARE(img_msgchat);
LV_IMG_DECLARE(img_bluetooth);
LV_IMG_DECLARE(img_test);
LV_IMG_DECLARE(img_battery);
LV_IMG_DECLARE(img_camera);
LV_IMG_DECLARE(img_si4735);
LV_IMG_DECLARE(img_track);
LV_IMG_DECLARE(img_batter_low);


LV_FONT_DECLARE(font_alibaba_12);
LV_FONT_DECLARE(font_alibaba_24);
LV_FONT_DECLARE(font_alibaba_40);
LV_FONT_DECLARE(font_alibaba_60);
LV_FONT_DECLARE(font_alibaba_100);

#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)
#define SCREEN_TIMEOUT 5000

// Do not use periodic light sleep while the clock is visible. LilyGo's
// lightSleep() powers the display off on every cycle, which makes it blink.
// The normal display-timeout path still enters light sleep with the panel off.
// #define ENABLE_CLOCK_LIGHT_SLEEP

#ifdef ENABLE_CLOCK_LIGHT_SLEEP
// Max nap between screensaver clock refreshes when the opt-in clock-face
// light-sleep is enabled. 1 s keeps the clock ticking once per second.
#define CLOCK_LIGHT_SLEEP_PERIOD_MS 1000
#endif

lv_obj_t *main_screen;
lv_obj_t *menu_panel;
// Novos tiles do launcher horizontal: HOME (imagem+hora) <-> GRID (apps) -> APP.
static lv_obj_t *home_tile;
static lv_obj_t *grid_tile;
static lv_obj_t *app_tile;
lv_group_t *menu_g, *app_g;
static lv_timer_t *clock_timer;
static lv_obj_t *clock_page;
static lv_timer_t *disp_timer = NULL;
static lv_timer_t *dev_timer = NULL;
static uint32_t disp_time_ms = 0;
static uint8_t low_battery_samples = 0;

#define LOW_BATTERY_MV 3300
#define LOW_BATTERY_VALID_MIN_MV 2500
#define LOW_BATTERY_SAMPLE_LIMIT 3

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *seg;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_label;
} clock_label_t;;

static clock_label_t clock_label;   // screensaver (clock_page)
static clock_label_t home_label;    // home tile (sempre visivel)

#if LVGL_VERSION_MAJOR == 9
static uint32_t name_change_id;
#endif


static lv_obj_t *desc_label;
static RTC_DATA_ATTR uint8_t brightness_level = 0;
static RTC_DATA_ATTR uint8_t keyboard_level = 0;

void set_low_power_mode_flag(bool enable)
{
    if (enable) {
        lv_obj_add_flag(main_screen, DEVICE_CAN_SLEEP);
    } else {
        lv_obj_remove_flag(main_screen, DEVICE_CAN_SLEEP);
    }
}

bool get_enter_low_power_flag()
{
    bool rlst = lv_obj_has_flag(main_screen, DEVICE_CAN_SLEEP);
    return rlst;
}

void menu_show()
{
    set_default_group(menu_g);
    // Launcher = grade de apps (tile col=1,row=0). Back dos apps cai aqui.
    lv_tileview_set_tile_by_index(main_screen, 1, 0, LV_ANIM_ON);
    lv_timer_resume(disp_timer);
    lv_disp_trig_activity(NULL);
}

void menu_hidden()
{
    // App aberto = tile col=1,row=0 -> col=1,row=1 (container do app).
    lv_tileview_set_tile_by_index(main_screen, 1, 1, LV_ANIM_ON);
    lv_timer_pause(disp_timer);
}

bool isinMenu()
{
    return !lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

void set_default_group(lv_group_t *group)
{
    lv_indev_t *cur_drv = NULL;
    for (;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv) {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_POINTER) {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}


// Celula da grade de apps: mini quadrado com icone (reusa a imagem do app,
// encolhida pra caber) + label curto embaixo. Abre o app no tile APP.
static void create_app_cell(lv_obj_t *grid, const char *name, const lv_img_dsc_t *img, app_t *app_fun)
{
    lv_obj_t *cell = lv_btn_create(grid);
    lv_obj_set_size(cell, 72, 82);
    lv_obj_set_style_bg_opa(cell, LV_OPA_20, 0);
    lv_obj_set_style_radius(cell, 12, 0);
    lv_obj_set_style_pad_all(cell, 2, 0);
    lv_obj_set_style_outline_color(cell, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(cell, (void *)name);

    if (img != NULL) {
        const int target = 44;  // lado do icone no quadradinho
        lv_obj_t *icon = lv_image_create(cell);
        lv_image_set_src(icon, img);
        if (img->header.w > 0) {
            lv_image_set_scale(icon, (uint16_t)(256 * target / img->header.w));
        }
        lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_CENTER);
        lv_obj_set_size(icon, target, target);
    }

    lv_obj_t *label = lv_label_create(cell);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    /* Click to select event callback */
    lv_obj_add_event_cb(cell, [](lv_event_t *e) {
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        set_default_group(app_g);
        if (func_cb->setup_func_cb) {
            (*func_cb->setup_func_cb)(app_tile);
        }
        menu_hidden();
    },
    LV_EVENT_CLICKED, app_fun);
}


void menu_name_label_event_cb(lv_event_t *e)
{
#if LVGL_VERSION_MAJOR == 9
    const char *v = (const char *)lv_event_get_param(e);
    if (v) {
        lv_label_set_text(lv_event_get_target_obj(e), v);
    }
#else
    lv_obj_t *label = lv_event_get_target(e);
    lv_msg_t *m = lv_event_get_msg(e);
    const char *v = (const char *)lv_msg_get_payload(m);
    if (v) {
        lv_label_set_text(label, v);
    }
#endif
}



// Atualiza um relogio (screensaver OU home). O clock_label_t alvo vem no
// user_data do timer -> o mesmo callback serve as duas telas.
static void clock_update_datetime(lv_timer_t *t)
{
    clock_label_t *cl = (clock_label_t *)lv_timer_get_user_data(t);
    if (!cl) {
        cl = &clock_label;
    }

    // ":" fixo (sem piscar): mantem sempre visivel.
    lv_obj_remove_flag(cl->seg, LV_OBJ_FLAG_HIDDEN);

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(cl->hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(cl->minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(cl->date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
    monitor_params_t params;
    hw_get_monitor_params(params);
    lv_bar_set_value(cl->battery_bar, params.battery_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(cl->battery_label, "%d%%", params.battery_percent);
}

// Monta o "rosto" do relogio (containers da hora/min, ":", data, bateria, linha)
// dentro de `page`, guardando os labels em `cl`. Usado pela tela-descanso E pela
// home (mesma arte, alvos de label diferentes).
static void build_clock_face(lv_obj_t *page, clock_label_t *cl)
{

    const  lv_font_t *font = &font_alibaba_100;

    lv_coord_t w = LV_PCT(35);
    lv_coord_t h = LV_PCT(70);

    int x_offset = 35;
    int y_offset = -20;

    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    uint32_t phy_ver_res = lv_display_get_physical_vertical_resolution(NULL);
    if (phy_ver_res > 222) {
        h = LV_PCT(45);
    }

    if (phy_hor_res == 320 && phy_ver_res == 240) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    lv_obj_t *hour_cout = lv_obj_create(page);
    lv_obj_set_size(hour_cout, w, h);
    lv_obj_align(hour_cout, LV_ALIGN_LEFT_MID, x_offset, y_offset);
    lv_obj_set_style_bg_opa(hour_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hour_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(hour_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_cout = lv_obj_create(page);
    lv_obj_set_size(min_cout, w, h);
    lv_obj_align(min_cout, LV_ALIGN_RIGHT_MID, -x_offset, y_offset);
    lv_obj_set_style_bg_opa(min_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(min_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(min_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(page);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -10 + y_offset);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, ":");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    cl->seg = label;

    label = lv_label_create(hour_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "12");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    cl->hour = label;

    label = lv_label_create(min_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "34");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    cl->minute = label;

    int offset = -5;
    if (lv_display_get_physical_vertical_resolution(NULL) > 320) {
        offset = -45;
    }

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_24, LV_PART_MAIN);
    lv_label_set_text(label, "03-24 Mon");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, offset);
    cl->date = label;


    lv_obj_t *img = lv_image_create(page);
    lv_image_set_src(img, &img_battery);
    if (lv_display_get_physical_vertical_resolution(NULL) == 240) {
        lv_obj_align_to(img, min_cout, LV_ALIGN_OUT_BOTTOM_RIGHT, -10, 20);
    } else {
        lv_obj_align(img, LV_ALIGN_BOTTOM_RIGHT, -60, offset);
    }

    lv_obj_t *bar = lv_bar_create(img);
    lv_obj_set_size(bar, img_battery.header.w - 8, img_battery.header.h - 12);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_make(0, 255, 0), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_CENTER, -1, 0);
    cl->battery_bar = bar;


    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_12, LV_PART_MAIN);
    lv_label_set_text(label, "100%");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(label, img, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    cl->battery_label = label;

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 4);
    lv_style_set_line_color(&style_line, lv_color_white());

    lv_obj_t *line1;
    line1 = lv_line_create(page);
    static lv_point_t line_points[] = {
        {0, 0},
        {150, 0}
    };
    lv_line_set_points(line1, line_points, 2);
    lv_obj_add_style(line1, &style_line, 0);
    lv_obj_align(line1, LV_ALIGN_BOTTOM_MID, 0, 15);
    lv_obj_set_style_line_opa(line1, LV_OPA_60, LV_PART_MAIN);
}

// Estilo de fundo comum (imagem + sem borda) pro relogio. bg = imagem de fundo.
static void style_clock_bg(lv_obj_t *page, const lv_image_dsc_t *bg)
{
    lv_obj_set_style_bg_image_src(page, bg, LV_PART_MAIN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
}

// Tela-descanso (screensaver): pagina solta, escondida ate inatividade.
lv_obj_t *setupClock()
{
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    style_clock_bg(page, &img_home_wallpaper);
    build_clock_face(page, &clock_label);

    clock_timer = lv_timer_create(clock_update_datetime, 1000, &clock_label);
    lv_timer_pause(clock_timer);

    return page;
}

// Home permanente: mesmo relogio dentro do tile HOME, com timer sempre ativo.
static void build_home_clock(lv_obj_t *tile)
{
    style_clock_bg(tile, &img_home_wallpaper);
    build_clock_face(tile, &home_label);
    lv_timer_create(clock_update_datetime, 1000, &home_label);

    // Botao "apps": abre a grade (equivale ao swipe pra esquerda). Canto direito.
    lv_obj_t *apps_btn = lv_btn_create(tile);
    lv_obj_set_size(apps_btn, 44, 44);
    lv_obj_set_style_radius(apps_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(apps_btn, LV_OPA_40, 0);
    lv_obj_align(apps_btn, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *al = lv_label_create(apps_btn);
    lv_label_set_text(al, LV_SYMBOL_RIGHT);
    lv_obj_center(al);
    lv_obj_add_event_cb(apps_btn, [](lv_event_t *e) {
        lv_tileview_set_tile_by_index(main_screen, 1, 0, LV_ANIM_ON);
    }, LV_EVENT_CLICKED, NULL);
}



static void hw_device_poll(lv_timer_t *t)
{
    (void)t;
    monitor_params_t params;
    hw_get_monitor_params(params);
    bool valid_low_sample = params.battery_connected && !params.vbus_present &&
                            params.battery_voltage >= LOW_BATTERY_VALID_MIN_MV &&
                            params.battery_voltage < LOW_BATTERY_MV;
    low_battery_samples = valid_low_sample ? low_battery_samples + 1 : 0;
    if (low_battery_samples >= LOW_BATTERY_SAMPLE_LIMIT) {
        printf("Low battery voltage: %lu mV USB Voltage: %lu mV\n", params.battery_voltage, params.usb_voltage);
        low_battery_samples = 0;
        lv_timer_pause(dev_timer);

        lv_obj_t *overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *image = lv_image_create(overlay);
        lv_image_set_src(image, &img_batter_low);
        lv_obj_center(image);

        lv_obj_t *label = lv_label_create(overlay);
        lv_label_set_text(label, "Battery Low!\nShutting down...");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

        lv_refr_now(NULL);
        lv_delay_ms(3000);
        hw_shutdown();
    }
}

static void ui_poll_timer_callback(lv_timer_t *t)
{
    bool timeout = lv_display_get_inactive_time(NULL) > SCREEN_TIMEOUT;
    if (timeout) {
        if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN) && get_enter_low_power_flag()) {
            lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);

            keyboard_level = hw_get_kb_backlight();
            hw_set_kb_backlight(0);
            lv_obj_remove_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_timer_resume(clock_timer);

            hw_set_cpu_freq(80);

            if (hw_get_disp_timeout_ms() != 0) {
                disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
            } else {
                disp_time_ms = 0;
            }
        }
    } else {
        if (!lv_obj_has_flag(clock_page, LV_OBJ_FLAG_HIDDEN)) {

            hw_set_cpu_freq(240);

            lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(clock_timer);

            hw_set_kb_backlight(keyboard_level);
        }
    }

    if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
#ifdef ENABLE_CLOCK_LIGHT_SLEEP
        // Opt-in (default off): while idling on the screensaver clock, sleep the
        // CPU in short chunks instead of spinning at 80 MHz. Wakes on the timer
        // (to refresh the clock), touch, or power key. NEEDS HARDWARE VALIDATION
        // before enabling by default — the vendor lightSleep also blanks the
        // display for the duration, so expect a refresh blink each period.
        // Never light-sleep while recording or while the alarm is ringing.
        if (hw_get_disp_is_on() && !hw_record_active() && !hw_alarm_ringing()) {
            hw_light_sleep_timed(CLOCK_LIGHT_SLEEP_PERIOD_MS);
        }
#endif
        bool disp_on = hw_get_disp_is_on();
        // Never light-sleep while recording or while the alarm is ringing: it
        // would freeze the shared player/record task and cut the audio.
        if (disp_on && disp_time_ms != 0 && !hw_record_active() && !hw_alarm_ringing()) {
            if (lv_tick_get() > disp_time_ms) {
                printf("Disp off\n");

                brightness_level =  hw_get_disp_backlight();
                printf("brightness_level:%d\n", brightness_level);

                hw_dec_brightness(0);

                hw_low_power_loop();
#ifdef NO_ENTER_LIGHT_SLEEP
                printf("Enter sleep\n");
                pinMode(0, INPUT_PULLUP);
                while (digitalRead(0) == HIGH) {
                    delay(10);
                }
                printf("Wakeup\n");
#endif
                lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(clock_timer);

                hw_set_cpu_freq(240);

                lv_refr_now(NULL);

                lv_display_trigger_activity(NULL);

                hw_inc_brightness(brightness_level);

                hw_set_kb_backlight(keyboard_level);
            }
        }
    }
}

void setupGui()
{

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);
    lv_obj_t *start_logo = lv_label_create(lv_screen_active());
    lv_label_set_text(start_logo, "Lewatch");
    lv_obj_set_style_text_font(start_logo, &font_alibaba_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_logo, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(start_logo);
    lv_refr_now(NULL);
    lv_delay_ms(5000);
    lv_obj_delete(start_logo);

    disable_keyboard();

    const lv_font_t  *main_font = MAIN_FONT;
    lv_theme_default_init(NULL, lv_color_black(), lv_palette_darken(LV_PALETTE_GREY, 3),
                          LV_THEME_DEFAULT_DARK, main_font);

    theme_init();

    // Create groups
    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    static lv_style_t style_frameless;
    lv_style_init(&style_frameless);
    lv_style_set_radius(&style_frameless, 0);
    lv_style_set_border_width(&style_frameless, 0);
    lv_style_set_bg_color(&style_frameless, lv_color_white());
    lv_style_set_shadow_width(&style_frameless, 55);
    lv_style_set_shadow_color(&style_frameless, lv_color_black());

    /* opening animation */
    main_screen = lv_tileview_create(lv_screen_active());

    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    /* Tres tiles horizontais: HOME (imagem+hora) <-> GRID (apps) -> APP. */
    home_tile  = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_RIGHT);
    grid_tile  = lv_tileview_add_tile(main_screen, 1, 0, LV_DIR_LEFT);
    app_tile   = lv_tileview_add_tile(main_screen, 1, 1, LV_DIR_TOP);
    menu_panel = grid_tile;   // launcher = grade de apps

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);

    /* HOME: relogio grande + fundo, sempre visivel. */
    build_home_clock(home_tile);

    /* GRID: grade rolavel de mini-apps (icone + nome). */
    lv_obj_t *grid_panel = lv_obj_create(grid_tile);
    lv_obj_set_size(grid_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(grid_panel, 0, 0);
    lv_obj_set_style_radius(grid_panel, 0, 0);
    lv_obj_set_style_bg_opa(grid_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(grid_panel, 8, 0);
    lv_obj_set_style_pad_row(grid_panel, 10, 0);
    lv_obj_set_style_pad_column(grid_panel, 8, 0);
    lv_obj_set_scrollbar_mode(grid_panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(grid_panel, LV_DIR_VER);
    lv_obj_set_flex_flow(grid_panel, LV_FLEX_FLOW_ROW_WRAP);
    // track_cross = START: linhas comecam no topo (senao a 1a linha sai pra cima
    // do scroll e o launcher "comeca" no meio da lista).
    lv_obj_set_flex_align(grid_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    extern app_t ui_sys_main ;
    extern app_t ui_radio_main ;
    extern app_t ui_audio_main ;
    extern app_t ui_wireless_main ;
    extern app_t ui_monitor_main ;
    extern app_t ui_power_main ;
    extern app_t ui_calendar_main;
    extern app_t ui_info_main;
    extern app_t ui_microphone_main;
    extern app_t ui_keyboard_main;
    extern app_t ui_sensor_main;
    extern app_t ui_msgchat_main;
    extern app_t ui_ble_main;
    extern app_t ui_ble_kb_main;
    extern app_t ui_factory_main;

    /* Add application */
#if defined(USING_IR_REMOTE)
    extern app_t ui_ir_remote_main;
    create_app_cell(grid_panel,"IR Remote", &img_ir_remote, &ui_ir_remote_main);
#endif

#if defined(USING_EXTERN_NRF2401)
    extern app_t ui_nrf24_main;
    create_app_cell(grid_panel,"NRF24", &img_radio, &ui_nrf24_main);
#endif

#if defined(USING_BLE_CONTROL)
    create_app_cell(grid_panel,"Camera Remote", &img_camera, &ui_camera_remote_main);
#endif

#if defined(USING_SI473X_RADIO)
    extern app_t ui_si4735_main;
    create_app_cell(grid_panel,"Radio", &img_si4735, &ui_si4735_main);
#endif


    extern app_t ui_clock_main;
    create_app_cell(grid_panel,"Clock", &img_configuration, &ui_clock_main);

    create_app_cell(grid_panel,"Calendar", &img_calendar, &ui_calendar_main);

    extern app_t ui_nextcloud_main;
    create_app_cell(grid_panel,"Agenda", &img_calendar, &ui_nextcloud_main);

#if defined(USING_TRACKBALL)
    extern app_t ui_trackball_main;
    create_app_cell(grid_panel,"Trackball", &img_track, &ui_trackball_main);
#endif

    // NFC app intentionally left out of the launcher (deactivated by request).
    // ui_nfc.cpp / app_nfc.cpp remain; re-add a cell for ui_nfc_main to restore.

    extern app_t ui_recorder_main;
    create_app_cell(grid_panel,"Recorder", &img_microphone, &ui_recorder_main);

    extern app_t ui_servitor_main;
    create_app_cell(grid_panel,"Servitor", &img_microphone, &ui_servitor_main);

    extern app_t ui_share_main;
    create_app_cell(grid_panel,"Share", &img_wifi, &ui_share_main);

    create_app_cell(grid_panel,"Screen Test", &img_test, &ui_factory_main);
    create_app_cell(grid_panel,"Setting", &img_configuration, &ui_sys_main);
    create_app_cell(grid_panel,"Wireless", &img_wifi, &ui_wireless_main);

    extern app_t ui_weather_main;
    extern app_t ui_news_main;
    create_app_cell(grid_panel,"Weather", &img_gps, &ui_weather_main);
    create_app_cell(grid_panel,"News", &img_msgchat, &ui_news_main);

    // Bluetooth + BLE Keyboard apps removed: BLE is unused and its controller RAM
    // is released at boot (factory.ino) to free internal SRAM for LVGL buffers.
    // The physical/on-device keyboard app is kept (it is not BLE).
#if defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_has_keyboard()) {
        create_app_cell(grid_panel,"Keyboard", &img_keyboard, &ui_keyboard_main);
    }
#endif

    create_app_cell(grid_panel,"Music", &img_music, &ui_audio_main);
    create_app_cell(grid_panel,"LoRa", &img_radio, &ui_radio_main);
    create_app_cell(grid_panel,"LoRa Chat", &img_msgchat, &ui_msgchat_main);
    create_app_cell(grid_panel,"Monitor", &img_monitoring, &ui_monitor_main);
    create_app_cell(grid_panel,"Power", &img_power, &ui_power_main);
    create_app_cell(grid_panel,"Microphone", &img_microphone, &ui_microphone_main);
    create_app_cell(grid_panel,"IMU", &img_gyroscope, &ui_sensor_main);

    extern app_t ui_pedometer_main;
    create_app_cell(grid_panel,"Pedometer", &img_gyroscope, &ui_pedometer_main);

    // Inicia na HOME (relogio); swipe esquerda abre a grade.
    lv_tileview_set_tile_by_index(main_screen, 0, 0, LV_ANIM_OFF);

    clock_page = setupClock();
    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);

    disp_timer = lv_timer_create(ui_poll_timer_callback, 1000, NULL);

    dev_timer = lv_timer_create(hw_device_poll, 5000, NULL);

    // Allow low power mode
    set_low_power_mode_flag(true);
    lv_display_trigger_activity(NULL);
}




static lv_obj_t *canvas;
static lv_indev_t *touch_indev;

void touch_panel_init()
{
    uint32_t width = lv_disp_get_hor_res(NULL);
    uint32_t height = lv_disp_get_ver_res(NULL);
#if 1
    lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
    uint32_t buffer_size =    LV_DRAW_BUF_SIZE(width, height, cf);
    uint8_t *buf_draw_buf = (uint8_t *)malloc(buffer_size);
    uint16_t stride_size = LV_DRAW_BUF_STRIDE(width, cf);

    printf("data_size:%u\n", buffer_size);
    printf("stride:%u\n", stride_size);
    printf("cf:%u\n", cf);

    static lv_draw_buf_t draw_buf = {
        .header = {
            .magic = (0x19),
            .cf = (cf),
            .flags = LV_IMAGE_FLAGS_MODIFIABLE,
            .w = (width),
            .h = (height),
            .stride = stride_size,
            .reserved_2 = 0,
        },
        .data_size = buffer_size,
        .data = buf_draw_buf,
        .unaligned_data = buf_draw_buf,
    };

    lv_image_header_t *header = &draw_buf.header;
    lv_draw_buf_init(&draw_buf, header->w, header->h,
                     (lv_color_format_t)header->cf,
                     header->stride,
                     buf_draw_buf,
                     buffer_size);
    lv_draw_buf_set_flag(&draw_buf, LV_IMAGE_FLAGS_MODIFIABLE);

    printf("data_size:%u\n", draw_buf.data_size);
    printf("stride:%u\n", draw_buf.header.stride);
    printf("cf:%u\n", draw_buf.header.cf);

#else
    // /*Create a buffer for the canvas*/
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
#endif

    /*Create a canvas and initialize its palette*/
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
    lv_obj_center(canvas);


    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            touch_indev = indev;
            break;
        }
        indev = lv_indev_get_next(indev);
    }
    lv_indev_type_t type = lv_indev_get_type(touch_indev);
    if (type != LV_INDEV_TYPE_POINTER) {
        return;
    }

    lv_timer_create([](lv_timer_t *t) {

#undef lv_point_t
        lv_point_t  point;
        lv_indev_state_t state =  lv_indev_get_state(touch_indev);
        if ( state == LV_INDEV_STATE_PRESSED ) {
            lv_indev_get_point(touch_indev, &point);
            printf("%d %d\n", point.x, point.y);

            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);

            lv_draw_arc_dsc_t dsc;
            lv_draw_arc_dsc_init(&dsc);
            dsc.color = lv_palette_main(LV_PALETTE_RED);
            dsc.width = 2;
            dsc.center.x =  point.x;
            dsc.center.y = point.y;
            dsc.width = 10;
            dsc.radius = 6;
            dsc.start_angle = 0;
            dsc.end_angle = 360;
            lv_draw_arc(&layer, &dsc);
            lv_canvas_finish_layer(canvas, &layer);
        }
    }, 30, NULL);

}




















