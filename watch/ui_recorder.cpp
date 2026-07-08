/**
 * @file      ui_recorder.cpp
 * @license   MIT
 *
 * Voice recorder. Captures 16 kHz mono WAV memos into FFat (newest
 * REC_MAX_FILES kept) and replays them through the shared audio player.
 * While recording, a power-key press only blanks the backlight (handled in
 * the HAL) so capture continues with the screen off.
 */
#include "ui_define.h"
#include <string>
#include <vector>

LV_FONT_DECLARE(font_alibaba_40);

// Poll fast enough that the elapsed clock looks live without busy-spinning.
#define RECORDER_TICK_MS   500

static lv_obj_t *menu = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *rec_btn_label = NULL;
static lv_obj_t *list = NULL;
static lv_timer_t *timer = NULL;
static std::vector<std::string> recordings;
static bool record_was_active = false;
static bool show_last_result = false;

// Strip the leading '/' for the player, which prepends one itself.
static const char *bare_name(const std::string &path)
{
    const char *c = path.c_str();
    return (c[0] == '/') ? c + 1 : c;
}

static void rebuild_list(void);

static void play_event_handler(lv_event_t *e)
{
    if (hw_record_active()) {
        return;   // no playback while recording
    }
    std::string *path = (std::string *)lv_event_get_user_data(e);
    if (path) {
        hw_set_sd_music_play(AUDIO_SOURCE_FATFS, bare_name(*path));
    }
}

static void delete_event_handler(lv_event_t *e)
{
    if (hw_record_active()) {
        return;
    }
    std::string *path = (std::string *)lv_event_get_user_data(e);
    if (path) {
        if (hw_player_running()) hw_set_play_stop();
        hw_record_delete(path->c_str());
        rebuild_list();
    }
}

static void rebuild_list(void)
{
    if (!list) {
        return;
    }
    lv_obj_clean(list);
    hw_record_list(recordings);
    // Show newest first. recordings stays put until the next rebuild, so the
    // element pointers handed to the callbacks remain valid.
    for (size_t n = recordings.size(); n > 0; n--) {
        std::string *path = &recordings[n - 1];
        lv_obj_t *row = lv_list_add_button(list, LV_SYMBOL_AUDIO, bare_name(*path));
        lv_obj_add_event_cb(row, play_event_handler, LV_EVENT_CLICKED, path);

        lv_obj_t *del = lv_btn_create(row);
        lv_obj_add_event_cb(del, delete_event_handler, LV_EVENT_CLICKED, path);
        lv_obj_t *del_lbl = lv_label_create(del);
        lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
        lv_obj_center(del_lbl);
    }
}

static void update_status(void)
{
    if (!status_label) {
        return;
    }
    if (hw_record_active()) {
        uint32_t s = hw_record_elapsed_ms() / 1000;
        lv_label_set_text_fmt(status_label, LV_SYMBOL_AUDIO " %02u:%02u",
                              (unsigned)(s / 60), (unsigned)(s % 60));
        if (rec_btn_label) {
            lv_label_set_text(rec_btn_label, "Stop");
        }
    } else {
        const char *status = "Ready";
        if (show_last_result) {
            switch (hw_record_last_result()) {
            case HW_RECORD_RESULT_OK:            status = "Saved"; break;
            case HW_RECORD_RESULT_LIMIT_REACHED: status = "Storage full"; break;
            case HW_RECORD_RESULT_NO_SPACE:      status = "No space"; break;
            case HW_RECORD_RESULT_CODEC_ERROR:   status = "Mic error"; break;
            case HW_RECORD_RESULT_FILE_ERROR:    status = "File error"; break;
            case HW_RECORD_RESULT_IO_ERROR:      status = "Write error"; break;
            default: break;
            }
        }
        lv_label_set_text(status_label, status);
        if (rec_btn_label) {
            lv_label_set_text(rec_btn_label, "Record");
        }
    }
}

static void record_event_handler(lv_event_t *e)
{
    (void)e;
    if (hw_record_active()) {
        hw_record_stop();
        show_last_result = true;
        rebuild_list();
    } else {
        if (hw_player_running()) {
            hw_set_play_stop();
        }
        show_last_result = true;
        hw_record_start();
    }
    record_was_active = hw_record_active();
    update_status();
}

static void recorder_tick(lv_timer_t *t)
{
    (void)t;
    bool active = hw_record_active();
    if (record_was_active && !active) {
        show_last_result = true;
        rebuild_list();
    }
    record_was_active = active;
    update_status();
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (hw_record_active()) {
            hw_record_stop();
        }
        if (hw_player_running()) {
            hw_set_play_stop();
        }
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
        }
        status_label = NULL;
        rec_btn_label = NULL;
        list = NULL;
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

void ui_recorder_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_OFF);

    status_label = lv_label_create(main_page);
    lv_obj_set_style_text_font(status_label, &font_alibaba_40, 0);
    lv_label_set_text(status_label, "Ready");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *rec_btn = lv_btn_create(main_page);
    lv_obj_align(rec_btn, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_add_event_cb(rec_btn, record_event_handler, LV_EVENT_CLICKED, NULL);
    rec_btn_label = lv_label_create(rec_btn);
    lv_label_set_text(rec_btn_label, "Record");
    lv_obj_center(rec_btn_label);

    list = lv_list_create(main_page);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_size(list, lv_pct(100), lv_pct(55));
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    rebuild_list();

    record_was_active = hw_record_active();
    show_last_result = false;
    timer = lv_timer_create(recorder_tick, RECORDER_TICK_MS, NULL);
    lv_menu_set_page(menu, main_page);
}

void ui_recorder_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_recorder_main = {
    .setup_func_cb = ui_recorder_enter,
    .exit_func_cb = ui_recorder_exit,
    .user_data = nullptr,
};
