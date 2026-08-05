/**
 * @file      ui_clock.cpp
 * @brief     Clock tools app: Stopwatch, Countdown and normal one-shot Alarm.
 */
#include "ui_define.h"
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

LV_FONT_DECLARE(font_alibaba_40);

static lv_obj_t  *menu = NULL;
static lv_timer_t *tick = NULL;

// Stopwatch
static lv_obj_t *sw_label = NULL;
static bool      sw_running = false;
static uint32_t  sw_base = 0;
static uint32_t  sw_accum = 0;

// Countdown
static lv_obj_t *cd_label = NULL;
static lv_obj_t *cd_roll_min = NULL;
static lv_obj_t *cd_roll_sec = NULL;
static bool      cd_running = false;
static uint32_t  cd_deadline = 0;

// Alarm
static lv_obj_t *al_roll_hour = NULL;
static lv_obj_t *al_roll_min = NULL;

// Alarm ring overlay
static lv_obj_t  *ring_overlay = NULL;
static lv_timer_t *ring_timer = NULL;

// --- helpers --------------------------------------------------------------

static void fill_opts(char *buf, size_t cap, int n)
{
    buf[0] = 0;
    size_t len = 0;
    for (int i = 0; i < n && len + 4 < cap; i++) {
        len += snprintf(buf + len, cap - len, i ? "\n%02d" : "%02d", i);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_min_height(btn, 44, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

static lv_obj_t *make_roller(lv_obj_t *parent, const char *opts)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 2);
    return r;
}

// --- stopwatch ------------------------------------------------------------

static void sw_startstop_cb(lv_event_t *e)
{
    hw_feedback();
    if (sw_running) {
        sw_accum += lv_tick_get() - sw_base;
        sw_running = false;
    } else {
        sw_base = lv_tick_get();
        sw_running = true;
    }
}

static void sw_reset_cb(lv_event_t *e)
{
    hw_feedback();
    sw_running = false;
    sw_accum = 0;
}

// --- countdown ------------------------------------------------------------

static void cd_startstop_cb(lv_event_t *e)
{
    hw_feedback();
    if (cd_running) {
        cd_running = false;
        return;
    }
    int m = lv_roller_get_selected(cd_roll_min);
    int s = lv_roller_get_selected(cd_roll_sec);
    uint32_t total = (uint32_t)(m * 60 + s) * 1000U;
    if (total == 0) return;
    cd_deadline = lv_tick_get() + total;
    cd_running = true;
}

// --- alarm ----------------------------------------------------------------

static std::vector<AudioParams_t> alarm_music;

static void alarm_sound_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    uint32_t i = lv_dropdown_get_selected(dd);
    if (i < alarm_music.size()) {
        hw_alarm_sound_set((uint8_t)alarm_music[i].source_type, alarm_music[i].file_name.c_str());
    }
}

static void build_alarm_sound_picker(lv_obj_t *parent)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, lv_pct(80));
    lv_dropdown_clear_options(dd);

    alarm_music.clear();
    hw_get_filesystem_music(alarm_music);
    if (alarm_music.empty()) {
        lv_dropdown_add_option(dd, "(sem audios)", 0);
        return;
    }

    for (uint32_t i = 0; i < alarm_music.size(); i++) {
        std::string label = (alarm_music[i].source_type == AUDIO_SOURCE_SDCARD ? "[SD]" : "[FFat]");
        label += alarm_music[i].file_name;
        lv_dropdown_add_option(dd, label.c_str(), i);
    }
    lv_obj_add_event_cb(dd, alarm_sound_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    uint8_t src;
    std::string saved;
    if (hw_alarm_sound_get(src, saved)) {
        for (uint32_t i = 0; i < alarm_music.size(); i++) {
            if (alarm_music[i].source_type == src && alarm_music[i].file_name == saved) {
                lv_dropdown_set_selected(dd, i);
                return;
            }
        }
    }
    hw_alarm_sound_set((uint8_t)alarm_music[0].source_type, alarm_music[0].file_name.c_str());
}

static void alarm_mode_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int h = lv_roller_get_selected(al_roll_hour);
    int m = lv_roller_get_selected(al_roll_min);
    hw_feedback();
#if defined(ARDUINO)
    hw_rtc_set_alarm((uint8_t)h, (uint8_t)m);
    lv_refr_now(NULL);
    delay(400);
    hw_enter_alarm_sleep();
#else
    (void)h; (void)m;
#endif
}

// --- periodic update ------------------------------------------------------

static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (sw_label) {
        uint32_t ms = sw_accum + (sw_running ? lv_tick_get() - sw_base : 0);
        uint32_t tenths = (ms / 100) % 10, sec = (ms / 1000) % 60, min = ms / 60000;
        char b[24];
        snprintf(b, sizeof(b), "%02u:%02u.%u", min, sec, tenths);
        lv_label_set_text(sw_label, b);
    }
    if (cd_label) {
        uint32_t rem = 0;
        if (cd_running) {
            int32_t d = (int32_t)(cd_deadline - lv_tick_get());
            if (d <= 0) {
                cd_running = false;
                hw_feedback();
            } else {
                rem = (uint32_t)d;
            }
        }
        uint32_t sec = (rem + 999) / 1000;
        char b[24];
        snprintf(b, sizeof(b), "%02u:%02u", sec / 60, sec % 60);
        lv_label_set_text(cd_label, b);
    }
}

// --- screen ---------------------------------------------------------------

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

static void cleanup_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (tick) {
        lv_timer_del(tick);
        tick = NULL;
    }
    sw_label = cd_label = NULL;
    cd_roll_min = cd_roll_sec = al_roll_hour = al_roll_min = NULL;
}

static lv_obj_t *section_title(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    return l;
}

void ui_clock_enter(lv_obj_t *parent)
{
    static char opts60[64 * 3];
    static char opts24[32 * 3];
    fill_opts(opts60, sizeof(opts60), 60);
    fill_opts(opts24, sizeof(opts24), 24);

    sw_running = false;
    sw_accum = 0;
    cd_running = false;

    menu = create_menu(parent, back_event_handler);
    lv_obj_t *page = lv_obj_create(menu);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_add_event_cb(page, cleanup_cb, LV_EVENT_DELETE, NULL);

    // Stopwatch
    section_title(page, "Cronometro");
    sw_label = lv_label_create(page);
    lv_obj_set_style_text_font(sw_label, &font_alibaba_40, 0);
    lv_label_set_text(sw_label, "00:00.0");
    {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_button(row, "Start/Stop", sw_startstop_cb, NULL);
        make_button(row, "Reset", sw_reset_cb, NULL);
    }

    // Countdown
    section_title(page, "Countdown");
    cd_label = lv_label_create(page);
    lv_obj_set_style_text_font(cd_label, &font_alibaba_40, 0);
    lv_label_set_text(cd_label, "00:00");
    {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        cd_roll_min = make_roller(row, opts60);
        cd_roll_sec = make_roller(row, opts60);
        make_button(row, "Start/Stop", cd_startstop_cb, NULL);
    }

    // Alarm
    section_title(page, "Alarme");
    {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        al_roll_hour = make_roller(row, opts24);
        al_roll_min = make_roller(row, opts60);
    }
    struct tm now = {};
    hw_get_date_time(now);
    lv_roller_set_selected(al_roll_hour, (uint16_t)(now.tm_hour % 24), LV_ANIM_OFF);
    lv_roller_set_selected(al_roll_min, (uint16_t)(now.tm_min % 60), LV_ANIM_OFF);
    build_alarm_sound_picker(page);
    make_button(page, "Ativar alarme", alarm_mode_cb, NULL);

    tick = lv_timer_create(tick_cb, 100, NULL);
}

void ui_clock_exit(lv_obj_t *parent)
{
}

app_t ui_clock_main = {
    .setup_func_cb = ui_clock_enter,
    .exit_func_cb = ui_clock_exit,
    .user_data = nullptr,
};

// --- alarm ring overlay (called from boot path when alarm fired) ----------

// Software gain for the alarm sound. This board's amplifier has no volume
// control, so louder = scaling samples (clips a bit). Tune + reflash to taste.
#define ALARM_GAIN 4

static void ring_vibrate_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    hw_vibrate_max();   // motor no maximo
#if defined(ARDUINO)
    // Mantem o som tocando em loop enquanto o alarme nao for dispensado.
    if (!hw_player_running()) {
        uint8_t src;
        std::string file;
        if (hw_alarm_sound_get(src, file)) {
            hw_set_sd_music_play((audio_source_type_t)src, file.c_str());
        }
    }
#endif
}

static void ring_dismiss_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (ring_timer) {
        lv_timer_del(ring_timer);
        ring_timer = NULL;
    }
    hw_set_alarm_ringing(false);
#if defined(ARDUINO)
    hw_set_play_stop();       // para o som do alarme
    hw_set_audio_gain(1);     // volta o ganho pro normal
#endif
    if (ring_overlay) {
        lv_obj_del(ring_overlay);
        ring_overlay = NULL;
    }
}

void ui_alarm_ring()
{
    if (ring_overlay) return;
    hw_set_alarm_ringing(true);   // keep the watch from light-sleeping mid-ring
    ring_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ring_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(ring_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ring_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *l = lv_label_create(ring_overlay);
    lv_obj_set_style_text_font(l, &font_alibaba_40, 0);
    lv_label_set_text(l, LV_SYMBOL_BELL " ALARME");

    make_button(ring_overlay, "OK", ring_dismiss_cb, NULL);

#if defined(ARDUINO)
    // Toca o som escolhido (se houver) ao disparar, amplificado.
    hw_set_audio_gain(ALARM_GAIN);
    uint8_t src;
    std::string file;
    if (hw_alarm_sound_get(src, file)) {
        hw_set_sd_music_play((audio_source_type_t)src, file.c_str());
    }
#endif
    // Vibracao maxima repetida (mais agressiva que o feedback comum).
    ring_timer = lv_timer_create(ring_vibrate_cb, 600, NULL);
    hw_vibrate_max();
}
