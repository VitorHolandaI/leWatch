/**
 * @file      ui_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#ifdef ARDUINO
#include <Arduino.h>
#include <LilyGoLib.h>
#include <WiFi.h>
#include <esp_mac.h>
#else
#define RTC_DATA_ATTR
#endif
#include <lvgl.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <time.h>
#include <string.h>
#include "hal_interface.h"

using namespace std;

#define DEFAULT_OPA          100

/** @brief Callback signature for an app's setup/exit hook; @p parent is the host tile. */
typedef void (*app_func_t)(lv_obj_t *parent);

/**
 * @brief Descriptor for a launcher mini-app.
 *
 * Each grid cell points at one of these. The launcher calls @ref setup_func_cb
 * when the app opens and @ref exit_func_cb when it closes.
 */
typedef struct {
    app_func_t setup_func_cb;   /**< Builds the app UI on the parent tile. */
    app_func_t exit_func_cb;    /**< Tears the app down / frees resources. */
    void *user_data;            /**< Optional per-app context, may be NULL. */
} app_t;


/** @brief Visual variants for menu item builders (text density / layout). */
enum : uint8_t {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

/**
 * @name LVGL message-bus IDs
 * Subjects published/subscribed across screens (lv_msg / lv_subject).
 * @{
 */
#define MSG_MENU_NAME_CHANGED    100  /**< Active menu title changed. */
#define MSG_LABEL_PARAM_CHANGE_1 200  /**< Generic label param slot 1 updated. */
#define MSG_LABEL_PARAM_CHANGE_2 201  /**< Generic label param slot 2 updated. */
#define MSG_TITLE_NAME_CHANGE    203  /**< Screen title text changed. */
#define MSG_BLE_SEND_DATA_1      204  /**< BLE payload slot 1 ready to send. */
#define MSG_BLE_SEND_DATA_2      205  /**< BLE payload slot 2 ready to send. */
#define MSG_MUSIC_TIME_ID        300  /**< Music playback elapsed-time tick. */
#define MSG_MUSIC_TIME_END_ID    301  /**< Music track reached its end. */
#define MSG_FFT_ID               400  /**< New FFT/spectrum frame available. */
/** @} */

/** @brief Root screen object that hosts the tileview (HOME / grid / app tiles). */
extern lv_obj_t *main_screen;

/**
 * @name Settings-row / widget factories
 * Helpers that build a labelled row (icon + text + control) inside a menu page.
 * All return the created LVGL object.
 * @{
 */
/** @brief Build a row whose control is produced by @p widget_create, wired to @p btn_event_cb. */
lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb);
/** @brief Build a text row (@p icon + @p txt) in the given builder variant. */
lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant);
/** @brief Build a labelled slider over [@p min, @p max] starting at @p val. */
lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter);
/** @brief Build a labelled on/off switch (initial state @p chk). */
lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb);
/** @brief Build a labelled push button. */
lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb);
/** @brief Build a row with a read-only value label (seeded with @p default_text). */
lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text);
/** @brief Build a labelled dropdown from newline-separated @p options. */
lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb);
/** @brief Create a modal message box with @p btns; caller frees via @ref destroy_msgbox. */
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data);
/** @brief Destroy a message box created by @ref create_msgbox. */
void destroy_msgbox(lv_obj_t *msgbox);
/** @} */

/** @brief Get the encoder (rotary/trackball) input device. */
lv_indev_t *lv_get_encoder_indev();
/** @brief Get the keyboard input device. */
lv_indev_t *lv_get_keyboard_indev();
/** @brief Reveal the app grid (return from an app to the launcher). */
void menu_show();
/** @brief Hide the app grid. */
void menu_hidden();
/** @brief Set the LVGL group that receives encoder/keyboard focus by default. */
void set_default_group(lv_group_t *group);

/** @brief Create a titled progress bar widget. */
lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title);

/** @brief Initialise the custom theme (colours, fonts, styles). */
void theme_init();

/** @brief Suspend processing of touch/encoder/keyboard input. */
void disable_input_devices();
/** @brief Resume processing of input devices. */
void enable_input_devices();

/** @brief Flag whether the UI may enter the idle low-power (screensaver) path. */
void set_low_power_mode_flag(bool enable);

/** @brief Disable the physical keyboard (and its backlight). */
void disable_keyboard();
/** @brief Enable the physical keyboard. */
void enable_keyboard();

/** @brief Create the floating action button used to leave an app. */
lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data);
/**
 * @brief Create a menu page with a standardised back button.
 * @param back_size  >0 overrides the floating back-button size (default FLOAT_BUTTON).
 */
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb, int back_size = 0);
/** @brief Create a circular image button. */
lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data);

// Weather (Open-Meteo) and News (Tom's Hardware RSS) sub-page content for the WiFi menu.
void ui_weather_attach(lv_obj_t *parent);
void ui_news_attach(lv_obj_t *parent);

#if LVGL_VERSION_MAJOR == 9
#define LV_MENU_ROOT_BACK_BTN_ENABLED   LV_MENU_ROOT_BACK_BUTTON_ENABLED
#define lv_menu_back_btn_is_root        lv_menu_back_button_is_root
#define lv_menu_set_mode_root_back_btn  lv_menu_set_mode_root_back_button
#define lv_mem_alloc                    lv_malloc
#define lv_mem_free                     lv_free
#define LV_IMG_CF_ALPHA_8BIT            LV_COLOR_FORMAT_L8
#define lv_point_t                      lv_point_precise_t
#else
#define lv_timer_get_user_data(x)       (x->user_data)
#define lv_indev_get_type(x)            (x->driver->type)
#endif

#if LVGL_VERSION_MAJOR == 8

#endif

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif
