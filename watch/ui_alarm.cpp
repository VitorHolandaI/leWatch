/**
 * @file      ui_alarm.cpp
 * @license   MIT
 *
 * Recurring daily alarm screen. Sets one alarm (hour:minute) that rings every
 * day, an on/off switch, the sound, and a "sleep after" hour: past that hour an
 * idle watch deep-sleeps until the alarm so it can wake to ring. Saved to NVS
 * (namespace "alarm"). The RTC (PCF8563, day/week = don't-care) repeats daily on
 * its own; the config just re-arms it on boot and drives the night deep-sleep.
 */
#include "ui_define.h"
#include <string>
#include <vector>

static lv_obj_t *menu = NULL;
static lv_obj_t *roll_hour = NULL;
static lv_obj_t *roll_min = NULL;
static lv_obj_t *roll_night = NULL;
static lv_obj_t *enable_sw = NULL;
static lv_obj_t *status_label = NULL;
static std::vector<AudioParams_t> alarm_music;

// Build "00\n01\n...\n(n-1)" zero-padded, for roller options.
static std::string range_opts(int n)
{
    std::string s;
    char b[4];
    for (int i = 0; i < n; i++) {
        snprintf(b, sizeof(b), "%02d", i);
        s += b;
        if (i + 1 < n) {
            s += "\n";
        }
    }
    return s;
}

static lv_obj_t *make_roller(lv_obj_t *parent, const std::string &opts)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    return r;
}

// --- sound picker (mesma lista do audio app; escolha salva no NVS) ----------
static void sound_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    uint32_t i = lv_dropdown_get_selected(dd);
    if (i < alarm_music.size()) {
        hw_alarm_sound_set((uint8_t)alarm_music[i].source_type,
                           alarm_music[i].file_name.c_str());
    }
}

static void build_sound_picker(lv_obj_t *parent)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, lv_pct(90));
    lv_dropdown_clear_options(dd);

    alarm_music.clear();
    hw_get_filesystem_music(alarm_music);
    if (alarm_music.empty()) {
        lv_dropdown_add_option(dd, "(sem audios)", 0);
        return;
    }
    int pos = 0;
    for (const auto &m : alarm_music) {
        std::string label = (m.source_type == AUDIO_SOURCE_SDCARD ? "[SD]" : "[FFat]");
        label += m.file_name;
        lv_dropdown_add_option(dd, label.c_str(), pos++);
    }
    lv_obj_add_event_cb(dd, sound_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    uint8_t src;
    std::string saved;
    if (hw_alarm_sound_get(src, saved)) {
        for (uint32_t i = 0; i < alarm_music.size(); i++) {
            if (alarm_music[i].file_name == saved) {
                lv_dropdown_set_selected(dd, i);
                break;
            }
        }
    } else {
        hw_alarm_sound_set((uint8_t)alarm_music[0].source_type,
                           alarm_music[0].file_name.c_str());
    }
}

static void save_event_handler(lv_event_t *e)
{
    (void)e;
    bool en = lv_obj_has_state(enable_sw, LV_STATE_CHECKED);
    uint8_t h  = (uint8_t)lv_roller_get_selected(roll_hour);
    uint8_t m  = (uint8_t)lv_roller_get_selected(roll_min);
    uint8_t nh = (uint8_t)lv_roller_get_selected(roll_night);
    hw_alarm_cfg_set(en, h, m, nh);
    if (status_label) {
        lv_label_set_text_fmt(status_label, en ? "Salvo: todo dia %02u:%02u" : "Alarme desligado",
                              (unsigned)h, (unsigned)m);
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        roll_hour = roll_min = roll_night = NULL;
        enable_sw = status_label = NULL;
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        menu_show();
    }
}

void ui_alarm_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Config atual (defaults se nunca salvo).
    bool en; uint8_t h, m, nh;
    hw_alarm_cfg_get(en, h, m, nh);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, LV_SYMBOL_BELL " Alarme diario");

    // Hora : minuto.
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    roll_hour = make_roller(row, range_opts(24));
    roll_min  = make_roller(row, range_opts(60));
    lv_roller_set_selected(roll_hour, h % 24, LV_ANIM_OFF);
    lv_roller_set_selected(roll_min, m % 60, LV_ANIM_OFF);

    // Ativado.
    lv_obj_t *sw_row = lv_obj_create(page);
    lv_obj_set_size(sw_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sw_lbl = lv_label_create(sw_row);
    lv_label_set_text(sw_lbl, "Ativado");
    enable_sw = lv_switch_create(sw_row);
    if (en) {
        lv_obj_add_state(enable_sw, LV_STATE_CHECKED);
    }

    // Dormir apos (hora): entra em deep-sleep pra garantir o toque de madrugada.
    lv_obj_t *night_lbl = lv_label_create(page);
    lv_label_set_text(night_lbl, "Dormir apos (h):");
    roll_night = make_roller(page, range_opts(24));
    lv_roller_set_selected(roll_night, nh % 24, LV_ANIM_OFF);

    // Som.
    build_sound_picker(page);

    lv_obj_t *save_btn = lv_btn_create(page);
    lv_obj_add_event_cb(save_btn, save_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Salvar");
    lv_obj_center(save_lbl);

    status_label = lv_label_create(page);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, lv_pct(90));
    lv_label_set_text(status_label, en ? "Alarme ativo" : "Alarme desligado");

    lv_menu_set_page(menu, page);
}

void ui_alarm_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_alarm_main = {
    .setup_func_cb = ui_alarm_enter,
    .exit_func_cb = ui_alarm_exit,
    .user_data = nullptr,
};
