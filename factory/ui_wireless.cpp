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
static uint32_t scan_started_tick = 0;

extern void ui_show_wifi_process_bar();

static lv_obj_t *saved_panel = NULL;

// ---- VPN (WireGuard) toggle ----
static lv_obj_t  *s_vpn_btn_label = NULL;
static lv_obj_t  *s_vpn_status = NULL;
static lv_timer_t *s_vpn_timer = NULL;

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
        if (s_vpn_timer) { lv_timer_del(s_vpn_timer); s_vpn_timer = NULL; }
        s_vpn_btn_label = NULL;
        s_vpn_status = NULL;
        saved_panel = NULL;
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

// 1..7 caracteres nao existe em WPA (min 8) e tambem nao e rede aberta.
static bool password_length_rejected()
{
    size_t len = lv_strlen(wifi_password);
    return len > 0 && len < 8;
}

#ifdef USING_TOUCHPAD
static void hide_password_keyboard()
{
    watch_touch_keyboard_hide(keyboard);
}

static void show_password_keyboard(lv_obj_t *ta)
{
    watch_touch_keyboard_show(keyboard, ta);
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
    keyboard = watch_touch_keyboard_create(lv_scr_act());
#endif
    lv_obj_add_event_cb(pwd_ta, password_ta_event_cb, LV_EVENT_ALL, NULL);

    return pwd_ta;
}

static void fill_ssid_dropdown_from_scan()
{
    vector <wifi_scan_params_t> list;
    hw_get_wifi_scan_result(list);

    // Monta as opcoes inteiras antes de trocar: se a varredura voltar vazia a
    // lista anterior continua valida (antes o dropdown ficava sem opcao).
    std::string options;
    uint16_t count = 0;
    for (const auto &net : list) {
        if (net.ssid.empty()) continue;     // SSID oculto nao entra no dropdown
        if (count++ > 0) options += '\n';
        options += net.ssid;
    }
    if (count == 0) {
        ui_msg_pop_up("WiFi", "Nenhuma rede encontrada");
        return;
    }
    lv_dropdown_set_options(wifi_dd, options.c_str());
    lv_dropdown_set_selected(wifi_dd, 0);
    lv_dropdown_get_selected_str(wifi_dd, wifi_ssid, sizeof(wifi_ssid));
}

// Desliga o radio WiFi (volta ao repouso/economia).
static void wifi_off_event(lv_event_t *e)
{
    hw_wifi_off();
    ui_msg_pop_up("WiFi", "WiFi desligado");
}

// Sem SSID no formulario: WiFiMulti varre e conecta na salva de melhor sinal.
static void connect_best_saved()
{
    if (hw_wifi_saved_count() == 0) {
        ui_msg_pop_up("WiFi", "Escolha uma rede, digite a senha e conecte");
        return;
    }
    if (hw_wifi_multi_connect()) {
        ui_show_wifi_process_bar();
    } else {
        ui_msg_pop_up("WiFi", "Nao conectou");
    }
}

// Conecta na rede do formulario (scan + senha). O HAL grava no NVS quando a
// rede sobe (GOT_IP), entao nao precisa salvar antes de conectar.
static void wifi_connect_event(lv_event_t *e)
{
    (void)e;
    copy_current_wifi_form();

    if (lv_strlen(wifi_ssid) == 0) {
        connect_best_saved();
        return;
    }
    if (password_length_rejected()) {
        ui_msg_pop_up("WiFi", "Senha curta (min 8) ou vazio p/ rede aberta");
        return;
    }

    wifi_conn_params_t params;
    params.ssid = wifi_ssid;
    params.password = wifi_password;
    if (!hw_set_wifi_connect(params)) {
        ui_msg_pop_up("WiFi", "SSID is null");
        return;
    }
    ui_show_wifi_process_bar();
}

// Preenche o formulario com uma rede salva (lapis na lista de baixo).
static void load_saved_into_form(const char *ssid, const char *password)
{
    if (!ssid || lv_strlen(ssid) == 0) return;
    lv_snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
    lv_snprintf(wifi_password, sizeof(wifi_password), "%s", password ? password : "");
    if (password_ta) {
        lv_textarea_set_text(password_ta, wifi_password);
    }
    if (!wifi_dd) return;
    // A rede salva pode nao estar na ultima varredura: entra no fim da lista.
    int32_t index = lv_dropdown_get_option_index(wifi_dd, ssid);
    if (index < 0) {
        lv_dropdown_add_option(wifi_dd, ssid, lv_dropdown_get_option_count(wifi_dd));
        index = (int32_t)lv_dropdown_get_option_count(wifi_dd) - 1;
    }
    lv_dropdown_set_selected(wifi_dd, (uint32_t)index);
    printf("saved network loaded into form: %s\n", ssid);
}

static void set_password_text(const char *password)
{
    lv_snprintf(wifi_password, sizeof(wifi_password), "%s", password ? password : "");
    if (password_ta) {
        lv_textarea_set_text(password_ta, wifi_password);
    }
}

// Senha da propria NVS quando a rede escolhida ja esta salva. Sem isso o campo
// ficava com a senha da rede anterior (ou vazia) ao trocar de SSID.
static bool load_saved_password(const char *ssid)
{
    uint8_t count = hw_wifi_saved_count();
    for (uint8_t i = 0; i < count; i++) {
        std::string stored;
        std::string password;
        if (!hw_wifi_saved_get(i, stored) || stored != ssid) continue;
        if (!hw_wifi_saved_get_password(i, password)) return false;
        set_password_text(password.c_str());
        return true;
    }
    return false;
}

static void fill_password_from_build_config()
{
#ifdef WIFI_SSID
    if (lv_strcmp(wifi_ssid, WIFI_SSID) == 0) {
        set_password_text(WIFI_PASSWORD);
        return;
    }
#endif
#ifdef WIFI_SSID2
    if (lv_strcmp(wifi_ssid, WIFI_SSID2) == 0) {
        set_password_text(WIFI_PASSWORD2);
        return;
    }
#endif
    set_password_text("");
}

static void dropdown_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    char buf[128];
    lv_dropdown_get_selected_str(dd, buf, 128);
    printf("select option = %s\n", buf);
    lv_strcpy(wifi_ssid, buf);
    if (!load_saved_password(wifi_ssid)) {
        fill_password_from_build_config();
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
    lv_obj_t *bar = (lv_obj_t *)obj;
    lv_bar_set_value(bar, val, LV_ANIM_ON);

    // No primeiro tick a varredura pode nem ter comecado: exigir 500ms evita
    // fechar o scan cedo e deixar o dropdown vazio.
    uint32_t elapsed = lv_tick_get() - scan_started_tick;
    bool scan_done = elapsed >= 500 && !hw_get_wifi_scanning();
    if (!scan_done && (val < 100 || elapsed < 500)) {
        return;
    }

    lv_obj_del(lv_obj_get_parent(bar));
    scanning = false;
    fill_ssid_dropdown_from_scan();
}

static void scan_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {

        if (scanning) {
            printf("Scanning !...\n");
            return;
        }

        scanning = true;
        scan_started_tick = lv_tick_get();

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

// Reflect real tunnel state: "conectada" once the netif is up, "conectando..."
// while enabled but not yet up, "desligada" when off.
static void vpn_refresh_status(void)
{
    if (!s_vpn_status) return;
    bool on = hw_vpn_enabled_get();
    const char *txt = !on ? "VPN desligada"
                      : (hw_vpn_up() ? LV_SYMBOL_OK " VPN conectada"
                                     : "VPN conectando...");
    lv_label_set_text(s_vpn_status, txt);
    if (s_vpn_btn_label) {
        lv_label_set_text(s_vpn_btn_label, on ? LV_SYMBOL_POWER " Desligar VPN"
                                              : LV_SYMBOL_POWER " Ligar VPN");
    }
}

// Erro do autosave chega aqui: o HAL grava no GOT_IP, fora da task da UI, e
// deixa a falha pendente pra ser mostrada quando der (uma vez so).
static void report_pending_save_failure()
{
    if (ui_msg_is_open()) {
        return;     // deixa pendente; ui_msg_pop_up descartaria a mensagem
    }
    wifi_saved_result_t failure = hw_wifi_saved_take_failure();
    if (failure == WIFI_SAVED_OK) {
        return;
    }
    char msg[96];
    if (failure == WIFI_SAVED_FULL) {
        lv_snprintf(msg, sizeof(msg), "Lista cheia (%u): toque na lixeira",
                    hw_wifi_saved_capacity());
    } else {
        lv_snprintf(msg, sizeof(msg), "Rede nao salva: %s",
                    hw_wifi_saved_result_name(failure));
    }
    ui_msg_pop_up("WiFi", msg);
}

static void vpn_status_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    vpn_refresh_status();
    // A rede conectada agora e salva pelo HAL no GOT_IP; o painel se atualiza
    // sozinho quando a revision muda.
    ui_wifi_saved_panel_refresh(saved_panel);
    report_pending_save_failure();
}

static void vpn_toggle_event(lv_event_t *e)
{
    LV_UNUSED(e);
    bool on = !hw_vpn_enabled_get();
    hw_vpn_enabled_set(on);
    if (on) {
        if (!hw_get_wifi_connected()) {
            ui_msg_pop_up("VPN", "Conecte o WiFi primeiro");
        }
        hw_vpn_start_async();   // safe: begin()+probe() run on their own task
    } else {
        hw_vpn_end();
    }
    vpn_refresh_status();
}

static lv_obj_t *vpn_control_create(lv_obj_t *parent)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_width(b, lv_pct(90));
    lv_obj_set_style_text_font(b, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(b, vpn_toggle_event, LV_EVENT_CLICKED, NULL);
    s_vpn_btn_label = lv_label_create(b);
    lv_obj_center(s_vpn_btn_label);

    s_vpn_status = lv_label_create(parent);
    vpn_refresh_status();
    return b;
}

void ui_wireless_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    ui_create_option(main_page, LV_SYMBOL_WIFI" Password:", NULL, password_text_crate, NULL);
    ui_create_option(main_page, LV_SYMBOL_WIFI" SSID:", NULL, dropdown_create, NULL);




    // Acoes como botoes largos empilhados (rola a pagina) em vez de redondos amontoados.
    // Nao existe botao "Salvar": conectar ja grava a rede no NVS quando ela sobe.
    wide_action_button(main_page, LV_SYMBOL_REFRESH " Scan",          scan_btn_event);
    wide_action_button(main_page, LV_SYMBOL_OK      " Conectar",      wifi_connect_event);
    wide_action_button(main_page, LV_SYMBOL_POWER   " Desligar WiFi", wifi_off_event);

    // VPN WireGuard: liga/desliga em runtime (persiste em NVS). Fica no fim pra
    // nao atrapalhar o fluxo de conectar WiFi (o tunel precisa do WiFi antes).
    vpn_control_create(main_page);
    if (!s_vpn_timer) s_vpn_timer = lv_timer_create(vpn_status_tick, 1000, NULL);

    // Redes salvas na propria pagina (o usuario ve o que ja esta no NVS sem
    // abrir sub-pagina). WiFi cuida so de WiFi; Weather/News sao apps proprios.
    saved_panel = ui_wifi_saved_panel_create(main_page, load_saved_into_form);

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
