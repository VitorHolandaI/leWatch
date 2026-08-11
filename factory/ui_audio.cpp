/**
 * @file      ui_audio.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#ifdef USING_AUDIO_CODEC
#define HAS_VOLUME_SLIDER
#endif

static vector<AudioParams_t> music_list;
static lv_timer_t *timer = NULL;
static lv_obj_t *last_play_obj = NULL;
static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *s_music_list = NULL;   // the list widget, for rebuild after delete

static void populate_music_list(void);

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        hw_set_play_stop();
        if (timer) { lv_timer_del(timer); timer = NULL; }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        last_play_obj = NULL;
        s_music_list = NULL;   // a pending music_rebuild_async must become a no-op

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}


static void audio_play_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *symbol = (lv_obj_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *list = lv_obj_get_parent(obj);
        const char *item = lv_list_get_button_text(list, obj);
        char *text = lv_label_get_text(symbol);

        if (strcmp(text, LV_SYMBOL_PLAY) == 0) {
            if (last_play_obj != NULL) {
                lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
            }
            if (last_play_obj == symbol) {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                hw_set_sd_music_resume();

            } else {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                last_play_obj = symbol;

                AudioParams_t param = *(AudioParams_t *)lv_obj_get_user_data(obj);
                hw_set_sd_music_play(param.source_type, param.file_name.c_str());

                if (item) {
                    printf("Click %s source :%d  obj:%p \n", item, param.source_type, obj);
                }

                if (timer) {
                    lv_timer_del(timer);
                }
                timer =  lv_timer_create([](lv_timer_t *t) {
                    if (!hw_player_running()) {
                        if (last_play_obj) {
                            lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
                            lv_timer_del(t);
                            timer = NULL;
                            last_play_obj = NULL;
                        }
                    }
                }, 500, NULL);
            }
        } else {
            lv_label_set_text(symbol, LV_SYMBOL_PLAY);
            hw_set_sd_music_pause();
        }
    }
}

// Rebuild the list outside the button's own event context (deleting the row that
// fired the event inline would be a use-after-free).
static void music_rebuild_async(void *unused)
{
    LV_UNUSED(unused);
    populate_music_list();
}

static void music_delete_event(lv_event_t *e)
{
    AudioParams_t *p = (AudioParams_t *)lv_event_get_user_data(e);
    if (!p) return;
    printf("[music] delete cb: %s src=%d\n", p->file_name.c_str(), p->source_type);
    // Stop any playback first so we never delete a file being read.
    hw_set_play_stop();
    if (timer) { lv_timer_del(timer); timer = NULL; }
    last_play_obj = NULL;
    hw_delete_audio_file(p->source_type, p->file_name.c_str());
    lv_async_call(music_rebuild_async, NULL);
}

// (Re)fill s_music_list: one row per audio file, with a play toggle and a trash
// button. Rows carry &music_list[i]; safe because we rebuild the whole list on
// any change, so no stale pointer is ever used.
static void populate_music_list(void)
{
    if (!s_music_list) return;
    lv_obj_clean(s_music_list);
    music_list.clear();
    hw_get_filesystem_music(music_list);
    int index = 0;
    for (const auto &file_info : music_list) {
        string name = file_info.source_type == AUDIO_SOURCE_SDCARD ? "[SD]" : "[FFat]";
        name += file_info.file_name;
        lv_obj_t *row = lv_list_add_button(s_music_list, LV_SYMBOL_AUDIO, name.c_str());
        lv_obj_set_user_data(row, &(music_list[index]));

        lv_obj_t *play = lv_label_create(row);
        lv_label_set_text(play, LV_SYMBOL_PLAY);
        lv_obj_add_event_cb(row, audio_play_event, LV_EVENT_CLICKED, play);

        lv_obj_t *del = lv_btn_create(row);
        lv_obj_add_event_cb(del, music_delete_event, LV_EVENT_CLICKED, &(music_list[index]));
        lv_obj_t *del_lbl = lv_label_create(del);
        lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
        lv_obj_center(del_lbl);

        index++;
    }
}

static void volume_slider_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        int volume = lv_slider_get_value(obj);
        printf("Set volume to %d\n", volume);
        hw_set_volume(volume);
    }
}

#ifdef HAS_EFFECT_BUTTONS
void effect_button_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
        lv_obj_t *label = lv_obj_get_child(obj, 0);
        const char *text = lv_label_get_text(label);
        if (strcmp(text, "3D") == 0) {
            // printf("3D Effect: %s\n", checked ? "ON" : "OFF");
            hw_set_audio_effect_3d(checked);
        } else if (strcmp(text, "A/B") == 0) {
            // printf("A/B Effect: %s\n", checked ? "ON" : "OFF");
            hw_set_audio_effect_ab_class(checked);
        }
    }
}
#endif /*HAS_EFFECT_BUTTONS*/

void ui_audio_enter(lv_obj_t *parent)
{
    music_list.clear();
    menu = create_menu(parent, back_event_handler);


    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(main_page, lv_pct(100), lv_pct(100));

    hw_get_filesystem_music(music_list);

    if (!music_list.size()) {
        lv_obj_t *cont = lv_obj_create(main_page);
        lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
        lv_obj_center(cont);
        lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);

        LV_IMG_DECLARE(img_cry);
        lv_obj_t *img = lv_img_create(cont);
        lv_img_set_src(img, &img_cry);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, lv_pct(10));

        lv_obj_t *label = lv_label_create(cont);
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(label, LV_PCT(80));

#ifdef HAS_SD_CARD_SOCKET
        lv_label_set_text(label, "No MP3 files found.\nPlease put the audio files into the SD card.");
#else
        lv_label_set_text(label, "No MP3 file found in the file system");
#endif

        lv_menu_set_page(menu, main_page);
        lv_obj_align_to(label, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

#ifdef USING_TOUCHPAD
#endif

        return;
    }

    /*Create a list*/
    lv_obj_t *list1 = lv_list_create(main_page);
    lv_obj_set_style_border_width(list1, 0, LV_PART_MAIN);

#ifdef HAS_VOLUME_SLIDER
    lv_obj_set_size(list1, lv_pct(100), lv_pct(80));
#else
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
#endif
    lv_obj_center(list1);

    /*Add buttons to the list (play toggle + trash per row)*/
    s_music_list = list1;
    populate_music_list();

#ifdef HAS_VOLUME_SLIDER
    lv_obj_t *obj = lv_menu_cont_create(main_page);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(20));
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    lv_obj_t *sub_cont = lv_obj_create(obj);
    lv_obj_set_size(sub_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(sub_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_align(sub_cont, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_border_width(sub_cont, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(sub_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(sub_cont, LV_DIR_NONE);

    lv_obj_t *label_vol = lv_label_create(sub_cont);
    lv_label_set_text(label_vol, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_margin_right(label_vol, 20, LV_PART_MAIN);
    lv_obj_set_style_margin_top(label_vol, -4, LV_PART_MAIN);

    lv_obj_t *slider = lv_slider_create(sub_cont);
#ifdef HAS_EFFECT_BUTTONS
    lv_obj_set_width(slider, lv_pct(35));
#else
    lv_obj_set_width(slider, lv_pct(80));
#endif
    lv_slider_set_value(slider, hw_get_volume(), LV_ANIM_OFF);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_add_event_cb(slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_height(slider, 10, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_size(slider, 20, 20, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_margin_left(slider, -18, LV_PART_MAIN);

#ifdef HAS_EFFECT_BUTTONS
        lv_obj_t *ab_btn = lv_button_create(sub_cont);
        lv_obj_set_size(ab_btn, lv_pct(18), lv_pct(100));
        lv_obj_add_flag(ab_btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_align(ab_btn, LV_ALIGN_CENTER);
        lv_obj_t *ab_label = lv_label_create(ab_btn);
        lv_label_set_text(ab_label, "3D");
        lv_obj_center(ab_label);
        lv_obj_set_style_margin_left(ab_btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ab_btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(ab_btn, effect_button_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *eff_btn = lv_button_create(sub_cont);
        lv_obj_set_size(eff_btn, lv_pct(18), lv_pct(100));
        lv_obj_add_flag(eff_btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_align(eff_btn, LV_ALIGN_CENTER);
        lv_obj_t *eff_label = lv_label_create(eff_btn);
        lv_label_set_text(eff_label, "A/B");
        lv_obj_center(eff_label);
        lv_obj_set_style_pad_all(eff_btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(eff_btn, effect_button_event, LV_EVENT_CLICKED, NULL);
#endif /*HAS_EFFECT_BUTTONS*/
#endif /*HAS_VOLUME_SLIDER*/

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
#endif

}

void ui_audio_exit(lv_obj_t *parent)
{

}

app_t ui_audio_main = {
    .setup_func_cb = ui_audio_enter,
    .exit_func_cb = ui_audio_exit,
    .user_data = nullptr,
};


