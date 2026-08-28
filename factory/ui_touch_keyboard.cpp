#include "ui_define.h"

enum class WatchKeyboardPage : uint8_t {
    LOWER_LEFT,
    LOWER_RIGHT,
    UPPER_LEFT,
    UPPER_RIGHT,
    SYMBOL_LEFT,
    SYMBOL_RIGHT,
};

static const char *const lower_left_map[] = {
    "q", "w", "e", "r", "t", "\n",
    "a", "s", "d", "f", "g", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "\n",
    "123", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const lower_right_map[] = {
    "y", "u", "i", "o", "p", "\n",
    "h", "j", "k", "l", "@", "\n",
    LV_SYMBOL_UP, "b", "n", "m", ".", ",", "\n",
    "123", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const upper_left_map[] = {
    "Q", "W", "E", "R", "T", "\n",
    "A", "S", "D", "F", "G", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "\n",
    "123", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const upper_right_map[] = {
    "Y", "U", "I", "O", "P", "\n",
    "H", "J", "K", "L", "@", "\n",
    LV_SYMBOL_UP, "B", "N", "M", ".", ",", "\n",
    "123", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const symbol_left_map[] = {
    "1", "2", "3", "4", "5", "\n",
    "6", "7", "8", "9", "0", "\n",
    "!", "?", "@", "#", "$", "%", "\n",
    "ABC", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const symbol_right_map[] = {
    "&", "*", "+", "-", "=", "\n",
    "_", "/", "\\", ":", ";", "\n",
    "(", ")", "[", "]", "{", "}", "\n",
    "ABC", "Space", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const char *const *const keyboard_maps[] = {
    lower_left_map,
    lower_right_map,
    upper_left_map,
    upper_right_map,
    symbol_left_map,
    symbol_right_map,
};

static WatchKeyboardPage keyboard_page(lv_obj_t *keyboard)
{
    const char *const *active_map = lv_buttonmatrix_get_map(lv_obj_get_child(keyboard, 0));
    for (size_t page = 0; page < sizeof(keyboard_maps) / sizeof(keyboard_maps[0]); ++page) {
        if (active_map == keyboard_maps[page]) {
            return static_cast<WatchKeyboardPage>(page);
        }
    }
    return WatchKeyboardPage::LOWER_LEFT;
}

static bool keyboard_page_is_right(WatchKeyboardPage page)
{
    return static_cast<uint8_t>(page) % 2U == 1U;
}

static void set_keyboard_page(lv_obj_t *keyboard, WatchKeyboardPage page)
{
    lv_obj_t *matrix = lv_obj_get_child(keyboard, 0);
    lv_obj_t *page_button = lv_obj_get_child(keyboard, 1);
    lv_obj_t *page_label = lv_obj_get_child(page_button, 0);
    lv_buttonmatrix_set_map(matrix, keyboard_maps[static_cast<uint8_t>(page)]);
    lv_label_set_text(page_label, keyboard_page_is_right(page) ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
}

static void change_keyboard_side(lv_event_t *event)
{
    lv_obj_t *page_button = static_cast<lv_obj_t *>(lv_event_get_target(event));
    lv_obj_t *keyboard = lv_obj_get_parent(page_button);
    uint8_t next_page = static_cast<uint8_t>(keyboard_page(keyboard)) ^ 1U;
    set_keyboard_page(keyboard, static_cast<WatchKeyboardPage>(next_page));
    hw_feedback();
}

static void set_keyboard_mode(lv_obj_t *keyboard, const char *key)
{
    bool right = keyboard_page_is_right(keyboard_page(keyboard));
    if (lv_strcmp(key, "123") == 0) {
        set_keyboard_page(keyboard, right ? WatchKeyboardPage::SYMBOL_RIGHT : WatchKeyboardPage::SYMBOL_LEFT);
    } else if (lv_strcmp(key, "ABC") == 0) {
        set_keyboard_page(keyboard, right ? WatchKeyboardPage::LOWER_RIGHT : WatchKeyboardPage::LOWER_LEFT);
    } else {
        bool upper = keyboard_page(keyboard) == WatchKeyboardPage::UPPER_LEFT ||
                     keyboard_page(keyboard) == WatchKeyboardPage::UPPER_RIGHT;
        uint8_t next_page = (upper ? 0U : 2U) + (right ? 1U : 0U);
        set_keyboard_page(keyboard, static_cast<WatchKeyboardPage>(next_page));
    }
}

static void submit_keyboard_text(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    if (lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr) != LV_RESULT_OK) {
        return;
    }
    lv_obj_send_event(textarea, LV_EVENT_READY, nullptr);
}

static void insert_keyboard_key(lv_obj_t *keyboard, const char *key)
{
    lv_obj_t *textarea = static_cast<lv_obj_t *>(lv_obj_get_user_data(keyboard));
    if (textarea == nullptr) {
        return;
    }
    if (lv_strcmp(key, "Space") == 0) {
        lv_textarea_add_char(textarea, ' ');
    } else if (lv_strcmp(key, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(textarea);
    } else if (lv_strcmp(key, LV_SYMBOL_OK) == 0) {
        submit_keyboard_text(keyboard, textarea);
    } else {
        lv_textarea_add_text(textarea, key);
    }
}

static void keyboard_matrix_changed(lv_event_t *event)
{
    lv_obj_t *matrix = static_cast<lv_obj_t *>(lv_event_get_target(event));
    uint32_t button_id = lv_buttonmatrix_get_selected_button(matrix);
    const char *key = lv_buttonmatrix_get_button_text(matrix, button_id);
    if (key == nullptr) {
        return;
    }
    lv_obj_t *keyboard = lv_obj_get_parent(matrix);
    if (lv_strcmp(key, "123") == 0 || lv_strcmp(key, "ABC") == 0 || lv_strcmp(key, LV_SYMBOL_UP) == 0) {
        set_keyboard_mode(keyboard, key);
    } else {
        insert_keyboard_key(keyboard, key);
    }
    hw_feedback();
}

static void style_keyboard_root(lv_obj_t *keyboard)
{
    int32_t keyboard_width = lv_display_get_horizontal_resolution(NULL) - WATCH_BACK_RAIL_WIDTH;
    lv_obj_set_size(keyboard, keyboard_width, lv_pct(52));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

static lv_obj_t *create_keyboard_matrix(lv_obj_t *keyboard)
{
    lv_obj_t *matrix = lv_buttonmatrix_create(keyboard);
    lv_obj_set_size(matrix, lv_pct(82), lv_pct(100));
    lv_obj_align(matrix, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_pad_all(matrix, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(matrix, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(matrix, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_bg_color(matrix, lv_color_hex(0x263244), LV_PART_ITEMS);
    lv_style_selector_t pressed_items = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_PRESSED));
    lv_obj_set_style_bg_color(matrix, lv_color_hex(0x2563EB), pressed_items);
    lv_obj_set_style_text_color(matrix, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(matrix, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 0, LV_PART_MAIN);
    lv_obj_clear_flag(matrix, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(matrix, keyboard_matrix_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    return matrix;
}

static lv_obj_t *create_keyboard_page_button(lv_obj_t *keyboard)
{
    lv_obj_t *button = lv_button_create(keyboard);
    lv_obj_set_size(button, lv_pct(15), 42);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_style_selector_t pressed_main = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_MAIN) | static_cast<uint32_t>(LV_STATE_PRESSED));
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1D4ED8), pressed_main);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(button, change_keyboard_side, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

lv_obj_t *watch_touch_keyboard_create(lv_obj_t *parent)
{
    lv_obj_t *keyboard = lv_obj_create(parent);
    style_keyboard_root(keyboard);
    create_keyboard_matrix(keyboard);
    create_keyboard_page_button(keyboard);
    set_keyboard_page(keyboard, WatchKeyboardPage::LOWER_LEFT);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    return keyboard;
}

void watch_touch_keyboard_show(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    lv_obj_set_user_data(keyboard, textarea);
    set_keyboard_page(keyboard, WatchKeyboardPage::LOWER_LEFT);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
}

void watch_touch_keyboard_hide(lv_obj_t *keyboard)
{
    lv_obj_set_user_data(keyboard, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
