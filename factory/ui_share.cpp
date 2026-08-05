/**
 * @file      ui_share.cpp
 * @license   MIT
 *
 * Share app. Brings up a WiFi access point + web page so a phone/PC can
 * download the voice recordings off FFat (this board has no SD/USB path).
 * Connect to the shown network, open the IP, tap a file to download. Leaving
 * the app tears the access point down.
 */
#include "ui_define.h"
#include <string>

static lv_obj_t *menu = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        hw_share_stop();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        menu_show();
    }
}

void ui_share_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Share");

    bool ok = hw_share_start();
    if (!ok) {
        lv_obj_t *err = lv_label_create(page);
        lv_label_set_text(err, "Falha ao abrir o ponto WiFi.");
        lv_menu_set_page(menu, page);
        return;
    }

    std::string ssid, wifi_pass, ip, http_user, http_pass;
    hw_share_info(ssid, wifi_pass, ip, http_user, http_pass);

    lv_obj_t *info = lv_label_create(page);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, lv_pct(92));
    lv_label_set_text_fmt(info,
                          "1) Conecte no WiFi:\n"
                          "   Rede: %s\n"
                          "   Senha: %s\n\n"
                          "2) Abra no navegador:\n"
                          "   http://%s\n"
                          "   Usuario: %s\n"
                          "   Senha HTTP: %s\n\n"
                          "3) Toque no audio p/ baixar.\n\n"
                          "Voltar encerra o ponto.",
                          ssid.c_str(), wifi_pass.c_str(), ip.c_str(),
                          http_user.c_str(), http_pass.c_str());

    lv_menu_set_page(menu, page);
}

void ui_share_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_share_main = {
    .setup_func_cb = ui_share_enter,
    .exit_func_cb = ui_share_exit,
    .user_data = nullptr,
};
