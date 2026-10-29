/**
 * @file      ui_wifi_saved.cpp
 * @brief     Painel "Redes salvas" embutido na pagina do app Wireless.
 *
 * Le a lista pelo HAL (hw_wifi_saved_*) e so reconstroi quando a revision muda,
 * entao pode ser chamado a cada tick sem reler o NVS. Cada linha tem duas
 * acoes: carregar a rede no formulario (lapis) e remover (lixeira).
 */
#include "ui_define.h"

#define SAVED_ROW_BTN_SIZE 36

static lv_obj_t *s_saved_panel = NULL;
static uint32_t s_saved_panel_revision = UINT32_MAX;
static wifi_saved_load_cb_t s_saved_load_cb = NULL;

static void saved_row_style(lv_obj_t *row)
{
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *saved_row_button(lv_obj_t *row, const char *symbol,
                                  lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, SAVED_ROW_BTN_SIZE, SAVED_ROW_BTN_SIZE);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

// Le o SSID da linha pela label antes de qualquer remocao: o texto continua
// valido e evita depender do indice, que muda quando a lista e reconstruida.
static const char *saved_row_ssid(lv_obj_t *row)
{
    lv_obj_t *label = lv_obj_get_child(row, 0);
    return label ? lv_label_get_text(label) : NULL;
}

static void saved_load_event(lv_event_t *e)
{
    lv_obj_t *row = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    const char *ssid = row ? saved_row_ssid(row) : NULL;
    if (!ssid || !s_saved_load_cb) {
        return;
    }
    uint8_t count = hw_wifi_saved_count();
    for (uint8_t i = 0; i < count; i++) {
        std::string stored;
        std::string password;
        if (!hw_wifi_saved_get(i, stored) || stored != ssid) {
            continue;
        }
        if (!hw_wifi_saved_get_password(i, password)) {
            password.clear();
        }
        s_saved_load_cb(ssid, password.c_str());
        return;
    }
    ui_msg_pop_up("WiFi", "Rede nao encontrada no NVS");
}

static void saved_remove_event(lv_event_t *e)
{
    lv_obj_t *row = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    const char *ssid = row ? saved_row_ssid(row) : NULL;
    if (!ssid) {
        return;
    }
    std::string target(ssid);   // a linha morre no proximo refresh do painel
    if (!hw_wifi_saved_remove(target.c_str())) {
        ui_msg_pop_up("WiFi", "Falha ao remover a rede");
    }
}

static void saved_row_create(lv_obj_t *panel, const char *ssid)
{
    lv_obj_t *row = lv_obj_create(panel);
    saved_row_style(row);
    lv_label_set_text(lv_label_create(row), ssid);
    saved_row_button(row, LV_SYMBOL_EDIT, saved_load_event, row);
    saved_row_button(row, LV_SYMBOL_TRASH, saved_remove_event, row);
}

static void saved_panel_render(lv_obj_t *panel)
{
    lv_obj_clean(panel);
    uint8_t count = hw_wifi_saved_count();
    uint8_t capacity = hw_wifi_saved_capacity();
    lv_obj_t *header = lv_label_create(panel);
    if (count == 0) {
        lv_label_set_text_fmt(header, LV_SYMBOL_WIFI " Nenhuma rede salva (0/%u)", capacity);
        return;
    }
    lv_label_set_text_fmt(header, LV_SYMBOL_WIFI " Redes salvas (%u/%u)", count, capacity);
    for (uint8_t i = 0; i < count; i++) {
        std::string ssid;
        if (!hw_wifi_saved_get(i, ssid) || ssid.empty()) {
            continue;
        }
        saved_row_create(panel, ssid.c_str());
    }
}

lv_obj_t *ui_wifi_saved_panel_create(lv_obj_t *parent, wifi_saved_load_cb_t load_cb)
{
    s_saved_load_cb = load_cb;
    s_saved_panel = lv_obj_create(parent);
    lv_obj_set_width(s_saved_panel, lv_pct(100));
    lv_obj_set_height(s_saved_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_saved_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_saved_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(s_saved_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_saved_panel, 0, 0);
    lv_obj_set_style_pad_all(s_saved_panel, 0, 0);
    lv_obj_remove_flag(s_saved_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_saved_panel_revision = UINT32_MAX;   // forca o primeiro render
    ui_wifi_saved_panel_refresh(s_saved_panel);
    return s_saved_panel;
}

void ui_wifi_saved_panel_refresh(lv_obj_t *panel)
{
    if (!panel || !lv_obj_is_valid(panel)) {
        return;
    }
    uint32_t revision = hw_wifi_saved_revision();
    if (revision == s_saved_panel_revision) {
        return;
    }
    s_saved_panel_revision = revision;
    saved_panel_render(panel);
}
