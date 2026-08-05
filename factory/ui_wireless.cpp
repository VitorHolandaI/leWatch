/**
 * @file      ui_wireless.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"


#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif

static lv_obj_t *menu = NULL;
static char wifi_ssid[64];
static char wifi_password[128];
static lv_obj_t *wifi_dd;
static lv_obj_t *password_ta;
static bool scanning = false;

extern void ui_show_wifi_process_bar();

static lv_obj_t *saved_page = NULL;
static void build_saved_list(lv_obj_t *page);

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
#ifdef USING_TOUCHPAD
        if (keyboard) {
            lv_obj_del(keyboard);
            keyboard = NULL;
        }
#endif
        lv_obj_clean(menu);
        lv_obj_del(menu);
        disable_keyboard();
        menu_show();
    }
}

static bool is_textarea_draw_event(lv_event_code_t code)
{
    return code == LV_EVENT_DRAW_MAIN_BEGIN ||
           code == LV_EVENT_DRAW_MAIN ||
           code == LV_EVENT_DRAW_MAIN_END ||
           code == LV_EVENT_DRAW_POST_BEGIN ||
           code == LV_EVENT_DRAW_POST ||
           code == LV_EVENT_DRAW_POST_END;
}

static void copy_password_to_buffer(lv_obj_t *ta)
{
    const char *password = lv_textarea_get_text(ta);
    if (!password) {
        printf("PWD IS NULL!\n");
        return;
    }

    if (lv_strlen(password) > 0) {
        lv_snprintf(wifi_password, sizeof(wifi_password), "%s", password);
        printf("PWD:%s\n", wifi_password);
    } else {
        wifi_password[0] = '\0';
        printf("PWD IS EMPTY!\n");
    }
}

static void copy_current_wifi_form()
{
    if (wifi_dd) {
        lv_dropdown_get_selected_str(wifi_dd, wifi_ssid, sizeof(wifi_ssid));
    }
    if (password_ta) {
        copy_password_to_buffer(password_ta);
    }
}

#ifdef USING_TOUCHPAD
static void hide_password_keyboard()
{
    lv_keyboard_set_textarea(keyboard, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void show_password_keyboard(lv_obj_t *ta)
{
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
#endif

static bool handle_password_click(lv_obj_t *ta, bool edited)
{
#if defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)
    enable_keyboard();
    return false;
#else
    if (edited) {
        lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
        printf("disable keyboard\n");
        disable_keyboard();
        return true;
    }
#ifdef USING_TOUCHPAD
    show_password_keyboard(ta);
#endif
    return false;
#endif  /*defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)*/
}

static bool handle_password_defocus(lv_obj_t *ta, bool state)
{
    (void)ta;
    (void)state;
#if defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)
    printf("disable keyboard\n");
    disable_keyboard();
#endif

#ifdef USING_TOUCHPAD
    if (!state) {
        hide_password_keyboard();
        return true;
    }
#endif
    return false;
}

static void password_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);

    bool copyToBuffer = false;

    if (!is_textarea_draw_event(code))
        printf("ta event code:%d state:%d edited:%d\n", code, state, edited);

#ifdef USING_TOUCHPAD
    if (code == LV_EVENT_READY) {
        hide_password_keyboard();
        copyToBuffer = true;
    }
#endif

    if (code == LV_EVENT_CLICKED) {
        copyToBuffer = handle_password_click(ta, edited);
    }

    else if (code == LV_EVENT_DEFOCUSED) {
        copyToBuffer = handle_password_defocus(ta, state);
    }

    else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            printf("enable input keyboard \n");
            enable_keyboard();
        }
    }

    if (copyToBuffer) {
        copy_password_to_buffer(ta);
    }
}

static lv_obj_t *password_text_crate(lv_obj_t *parent)
{
    lv_obj_t *pwd_ta = lv_textarea_create(parent);
    password_ta = pwd_ta;
    lv_textarea_set_placeholder_text(pwd_ta, "password");
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_obj_set_scrollbar_mode(pwd_ta, LV_SCROLLBAR_MODE_OFF);

#ifdef WIFI_PASSWORD
    lv_textarea_set_text(pwd_ta, WIFI_PASSWORD);
    lv_snprintf(wifi_password, sizeof(wifi_password), "%s", WIFI_PASSWORD);
#endif

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_add_event_cb(pwd_ta, password_ta_event_cb, LV_EVENT_ALL, NULL);

    return pwd_ta;
}

// Salva a rede atual (SSID do dropdown + senha digitada) no NVS pro WiFiMulti.
static void wifi_save_event(lv_event_t *e)
{
    copy_current_wifi_form();

    if (lv_strlen(wifi_ssid) == 0) {
        ui_msg_pop_up("WiFi", "SSID is null");
    } else if (lv_strlen(wifi_password) < 8) {
        ui_msg_pop_up("WiFi", "Password too short");
    } else if (hw_wifi_saved_add(wifi_ssid, wifi_password)) {
        if (saved_page) build_saved_list(saved_page);
        ui_msg_pop_up("WiFi", "Rede salva");
    } else {
        ui_msg_pop_up("WiFi", "Falha ao salvar");
    }
}

// Desliga o radio WiFi (volta ao repouso/economia).
static void wifi_off_event(lv_event_t *e)
{
    hw_wifi_off();
    ui_msg_pop_up("WiFi", "WiFi desligado");
}

// Conecta: WiFiMulti varre e conecta na rede salva de melhor sinal disponivel.
// (WiFi fica off ate aqui.) Bloqueia ate conectar/timeout.
static void wifi_connect_event(lv_event_t *e)
{
    if (hw_wifi_saved_count() == 0) {
        ui_msg_pop_up("WiFi", "Nenhuma rede salva");
        return;
    }
    if (hw_wifi_multi_connect()) {
        ui_show_wifi_process_bar();
    } else {
        ui_msg_pop_up("WiFi", "Nao conectou");
    }
}

static void dropdown_event(lv_event_t *e)
{
    char buf[128];
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_dropdown_get_selected_str(dd, buf, 128);
        printf("select option = %s\n", buf);
        lv_strcpy(wifi_ssid, buf);
#ifdef WIFI_SSID
        if (lv_strcmp(wifi_ssid, WIFI_SSID) == 0) {
            lv_snprintf(wifi_password, sizeof(wifi_password), "%s", WIFI_PASSWORD);
            if (password_ta) lv_textarea_set_text(password_ta, wifi_password);
            printf("Copy ssid:%s password to buffer:%s\n", wifi_ssid, wifi_password);
        }
#endif
#ifdef WIFI_SSID2
        if (lv_strcmp(wifi_ssid, WIFI_SSID2) == 0) {
            lv_snprintf(wifi_password, sizeof(wifi_password), "%s", WIFI_PASSWORD2);
            if (password_ta) lv_textarea_set_text(password_ta, wifi_password);
            printf("Copy ssid:%s password to buffer:%s\n", wifi_ssid, wifi_password);
        }
#endif
    }
}


static lv_obj_t *dropdown_create(lv_obj_t *parent)
{
    wifi_dd = lv_dropdown_create(parent);
    lv_dropdown_clear_options(wifi_dd);
    lv_obj_add_event_cb(wifi_dd, dropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
#ifdef WIFI_SSID
    lv_dropdown_add_option(wifi_dd, WIFI_SSID, 0);
    lv_snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", WIFI_SSID);
#endif
#ifdef WIFI_SSID2
    lv_dropdown_add_option(wifi_dd, WIFI_SSID2, 1);
#endif
    return wifi_dd;
}


static void set_angle(void *obj, int32_t val)
{
    char buf[128];
    lv_obj_t *bar = (lv_obj_t *)obj;
    lv_bar_set_value(bar, val, LV_ANIM_ON);

    bool running = hw_get_wifi_scanning();

    if (val == 100 || !running) {

        lv_obj_del(lv_obj_get_parent(bar));

        scanning = false;

        vector <wifi_scan_params_t> list;
        hw_get_wifi_scan_result(list);

        // wifi_dd
        lv_dropdown_clear_options(wifi_dd);
        int16_t pos = 0;
        for (const auto &i : list) {
            lv_dropdown_add_option(wifi_dd, i.ssid.c_str(), pos++);
        }
        lv_dropdown_set_selected(wifi_dd, 0);
        lv_dropdown_get_selected_str(wifi_dd, buf, 128);
        lv_strcpy(wifi_ssid, buf);
    }
}

static void scan_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {

        if (scanning) {
            printf("Scanning !...\n");
            return;
        }

        scanning = true;

        hw_set_wifi_scan();

        lv_obj_t *bar =  ui_create_process_bar(lv_scr_act(), "WiFi Scanning...");
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, set_angle);
        lv_anim_set_time(&a, 20000);
        lv_anim_set_playback_time(&a, 3000);
        lv_anim_set_var(&a, bar);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_start(&a);
    }
}

// Botao de acao largo (ocupa a largura), empilhado e rolavel na pagina.
static lv_obj_t *wide_action_button(lv_obj_t *page, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(page);
    lv_obj_set_width(b, lv_pct(90));
    lv_obj_set_style_text_font(b, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

// ---- Redes salvas: sub-pagina listando SSIDs com botao de remover ----
static void saved_remove_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    std::string ssid;
    if (hw_wifi_saved_get(idx, ssid)) hw_wifi_saved_remove(ssid.c_str());
    build_saved_list(saved_page);
}

static void build_saved_list(lv_obj_t *page)
{
    lv_obj_clean(page);
    uint8_t n = hw_wifi_saved_count();
    if (n == 0) {
        lv_label_set_text(lv_label_create(page), "Nenhuma rede salva");
        return;
    }
    for (uint8_t i = 0; i < n; i++) {
        std::string ssid;
        if (!hw_wifi_saved_get(i, ssid)) continue;
        lv_obj_t *row = lv_menu_cont_create(page);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_label_set_text(lv_label_create(row), ssid.c_str());
        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 40, 40);
        lv_obj_center(lv_label_create(del));
        lv_label_set_text(lv_obj_get_child(del, 0), LV_SYMBOL_TRASH);
        lv_obj_add_event_cb(del, saved_remove_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void ui_wireless_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *con2 = ui_create_option(main_page, LV_SYMBOL_WIFI" Password:", NULL, password_text_crate, NULL);
    lv_obj_t *con1 = ui_create_option(main_page, LV_SYMBOL_WIFI" SSID:", NULL, dropdown_create, NULL);




    // Acoes como botoes largos empilhados (rola a pagina) em vez de redondos amontoados.
    wide_action_button(main_page, LV_SYMBOL_REFRESH " Scan",          scan_btn_event);
    wide_action_button(main_page, LV_SYMBOL_SAVE    " Salvar rede",   wifi_save_event);
    wide_action_button(main_page, LV_SYMBOL_OK      " Conectar",      wifi_connect_event);
    wide_action_button(main_page, LV_SYMBOL_POWER   " Desligar WiFi", wifi_off_event);



    // Redes salvas como sub-pagina (listar/remover). WiFi cuida so de WiFi;
    // Weather/News viraram apps proprios na grade.
    lv_obj_t *saved_cont = lv_menu_cont_create(main_page);
    lv_label_set_text(lv_label_create(saved_cont), LV_SYMBOL_WIFI " Saved networks");
    saved_page = lv_menu_page_create(menu, NULL);
    build_saved_list(saved_page);
    lv_menu_set_load_page_event(menu, saved_cont, saved_page);

    lv_menu_set_page(menu, main_page);

}


void ui_wireless_exit(lv_obj_t *parent)
{

}

app_t ui_wireless_main = {
    .setup_func_cb = ui_wireless_enter,
    .exit_func_cb = ui_wireless_exit,
    .user_data = nullptr,
};
