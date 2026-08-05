/**
 * @file      ui_ir_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-05-15
 *
 */
#include "ui_define.h"
#include "ir_control.h"

#if defined(USING_IR_REMOTE)
static lv_obj_t *menu = NULL;

// ---------------------------------------------------------------------------
// TVs: registry separado de AC. Objetos estaticos, sem new/delete.
// ---------------------------------------------------------------------------
static TclRokuTV tcl_roku_tv;
static TclAndroidTV tcl_android_tv;
static AocRokuTV aoc_roku_tv;
static SempRokuTV semp_roku_tv;
static LgTV lg_tv;
static UniversalTVRemote tv_remote;

static ElectraAC electra_ac;
static CoolixAC coolix_ac;
static MideaAC midea_ac;
static KomecoMideaAC komeco_midea_ac;
static SpringerMideaAC springer_midea_ac;
static LgAC lg_ac;
static SamsungAC samsung_ac;
static ToshibaAC toshiba_ac;
static UniversalACRemote ac_remote;

static uint8_t selected_tv_index = 0;
static uint8_t selected_ac_index = 0;
static char tv_options[192];
static char ac_options[192];

// "Super Turn On": step through every AC protocol, one per tick, showing which
// is firing so the user can spot the one that drives their unit.
static lv_obj_t  *ac_blast_label = NULL;
static lv_timer_t *ac_blast_timer = NULL;
static uint8_t     ac_blast_idx = 0;
#define AC_BLAST_STEP_MS 1300

static void setup_tv_remote()
{
    if (tv_remote.size() != 0) return;
    tv_remote.add(&tcl_roku_tv);
    tv_remote.add(&tcl_android_tv);
    tv_remote.add(&aoc_roku_tv);
    tv_remote.add(&semp_roku_tv);
    tv_remote.add(&lg_tv);
}

static void setup_ac_remote()
{
    if (ac_remote.size() != 0) return;
    ac_remote.add(&electra_ac);
    ac_remote.add(&coolix_ac);
    ac_remote.add(&midea_ac);
    ac_remote.add(&komeco_midea_ac);
    ac_remote.add(&springer_midea_ac);
    ac_remote.add(&lg_ac);
    ac_remote.add(&samsung_ac);
    ac_remote.add(&toshiba_ac);
}

static void append_option(char *buf, size_t len, const char *text)
{
    size_t used = strlen(buf);
    if (used != 0 && used + 1 < len) {
        buf[used++] = '\n';
        buf[used] = '\0';
    }
    if (used < len - 1) {
        strncat(buf, text, len - used - 1);
    }
}

static void build_tv_options()
{
    tv_options[0] = '\0';
    for (uint8_t i = 0; i < tv_remote.size(); ++i) {
        IrDeviceTV *tv = tv_remote.device(i);
        if (tv != nullptr) append_option(tv_options, sizeof(tv_options), tv->name());
    }
}

static void build_ac_options()
{
    ac_options[0] = '\0';
    for (uint8_t i = 0; i < ac_remote.size(); ++i) {
        IrDeviceAC *ac = ac_remote.device(i);
        if (ac != nullptr) append_option(ac_options, sizeof(ac_options), ac->name());
    }
}

static void tv_select_event_handler(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    selected_tv_index = (uint8_t)lv_dropdown_get_selected(dd);
}

static void ac_select_event_handler(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    selected_ac_index = (uint8_t)lv_dropdown_get_selected(dd);
}

static void tv_command_event_handler(lv_event_t *e)
{
    hw_feedback();
    TvCommand cmd = (TvCommand)(intptr_t)lv_event_get_user_data(e);
    tv_remote.execute(selected_tv_index, cmd);
}

static void ac_command_event_handler(lv_event_t *e)
{
    hw_feedback();
    AcCommand cmd = (AcCommand)(intptr_t)lv_event_get_user_data(e);
    ac_remote.execute(selected_ac_index, cmd);
}

static lv_obj_t *add_control_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 98, 50);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
    if (cb != nullptr) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_control_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return grid;
}

// Botao do D-pad: quadrado, posicionado por alinhamento dentro do cross.
static void add_dpad_button(lv_obj_t *cross, const char *text, lv_align_t align,
                            lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(cross);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, 0);
    lv_obj_align(btn, align, 0, 0);
    if (cb != nullptr) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

// Cruz de navegacao: setas (cima/baixo/esq/dir) + OK no centro.
static void create_dpad(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *cross = lv_obj_create(parent);
    lv_obj_set_size(cross, 200, 200);
    lv_obj_clear_flag(cross, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cross, 0, 0);
    lv_obj_set_style_border_width(cross, 0, 0);
    lv_obj_set_style_bg_opa(cross, LV_OPA_TRANSP, 0);

    add_dpad_button(cross, LV_SYMBOL_UP,    LV_ALIGN_TOP_MID,    cb, (void *)(intptr_t)TV_UP);
    add_dpad_button(cross, LV_SYMBOL_DOWN,  LV_ALIGN_BOTTOM_MID, cb, (void *)(intptr_t)TV_DOWN);
    add_dpad_button(cross, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,   cb, (void *)(intptr_t)TV_LEFT);
    add_dpad_button(cross, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID,  cb, (void *)(intptr_t)TV_RIGHT);
    add_dpad_button(cross, "OK",            LV_ALIGN_CENTER,     cb, (void *)(intptr_t)TV_OK);
}

// Botao largo (ocupa a largura) usado pro Home abaixo do D-pad.
static lv_obj_t *add_wide_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(92), 54);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_18, 0);
    if (cb != nullptr) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_family_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(88), 82);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_24, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void style_remote_page(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 10, 0);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void build_tv_page(lv_obj_t *page)
{
    style_remote_page(page);
    build_tv_options();

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "TV Remote");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(92));
    lv_dropdown_set_options(dd, tv_options);
    lv_dropdown_set_selected(dd, selected_tv_index);
    lv_obj_add_event_cb(dd, tv_select_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // Modo controle: cruz de navegacao + OK no centro, e Home logo abaixo.
    create_dpad(page, tv_command_event_handler);
    add_wide_button(page, LV_SYMBOL_HOME " Home", tv_command_event_handler, (void *)(intptr_t)TV_HOME);

    // Scroll pra baixo: demais botoes (Power/Vol/Ch/Mute/Input).
    lv_obj_t *grid = create_control_grid(page);
    add_control_button(grid, "Power", tv_command_event_handler, (void *)(intptr_t)TV_POWER);
    add_control_button(grid, "Vol +", tv_command_event_handler, (void *)(intptr_t)TV_VOL_UP);
    add_control_button(grid, "Vol -", tv_command_event_handler, (void *)(intptr_t)TV_VOL_DOWN);
    add_control_button(grid, "Ch +", tv_command_event_handler, (void *)(intptr_t)TV_CH_UP);
    add_control_button(grid, "Ch -", tv_command_event_handler, (void *)(intptr_t)TV_CH_DOWN);
    add_control_button(grid, "Mute", tv_command_event_handler, (void *)(intptr_t)TV_MUTE);
    add_control_button(grid, "Input", tv_command_event_handler, (void *)(intptr_t)TV_INPUT);
}

static void ac_blast_stop(void)
{
    if (ac_blast_timer) {
        lv_timer_del(ac_blast_timer);
        ac_blast_timer = NULL;
    }
}

static void ac_blast_step_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t n = hw_ac_blast_count();
    if (ac_blast_idx >= n) {
        ac_blast_stop();
        if (ac_blast_label) {
            lv_label_set_text(ac_blast_label, "Fim. Qual ligou?");
        }
        return;
    }
    if (ac_blast_label) {
        lv_label_set_text_fmt(ac_blast_label, "Testando: %s (%u/%u)",
                              hw_ac_blast_name(ac_blast_idx),
                              (unsigned)(ac_blast_idx + 1), (unsigned)n);
    }
    hw_ac_blast_send_on(ac_blast_idx);
    ac_blast_idx++;
}

static void ac_blast_event_handler(lv_event_t *e)
{
    (void)e;
    if (ac_blast_timer) {
        return;   // already running
    }
    ac_blast_idx = 0;
    ac_blast_step_cb(NULL);   // fire the first one immediately
    ac_blast_timer = lv_timer_create(ac_blast_step_cb, AC_BLAST_STEP_MS, NULL);
}

static void build_ac_page(lv_obj_t *page)
{
    style_remote_page(page);
    build_ac_options();

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "AC Remote");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(92));
    lv_dropdown_set_options(dd, ac_options);
    lv_dropdown_set_selected(dd, selected_ac_index);
    lv_obj_add_event_cb(dd, ac_select_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *grid = create_control_grid(page);
    add_control_button(grid, "Power", ac_command_event_handler, (void *)(intptr_t)AC_POWER);
    add_control_button(grid, "Temp +", ac_command_event_handler, (void *)(intptr_t)AC_TEMP_UP);
    add_control_button(grid, "Temp -", ac_command_event_handler, (void *)(intptr_t)AC_TEMP_DOWN);
    add_control_button(grid, "Cool", ac_command_event_handler, (void *)(intptr_t)AC_MODE_COOL);
    add_control_button(grid, "Heat", ac_command_event_handler, (void *)(intptr_t)AC_MODE_HEAT);
    add_control_button(grid, "Fan", ac_command_event_handler, (void *)(intptr_t)AC_MODE_FAN);
    add_control_button(grid, "Fan +", ac_command_event_handler, (void *)(intptr_t)AC_FAN_UP);
    add_control_button(grid, "Fan -", ac_command_event_handler, (void *)(intptr_t)AC_FAN_DOWN);
    add_control_button(grid, "Swing", ac_command_event_handler, (void *)(intptr_t)AC_SWING);

    // Super Turn On: fire every known AC protocol's power-on in sequence.
    add_wide_button(page, "Super Turn On", ac_blast_event_handler, NULL);
    ac_blast_label = lv_label_create(page);
    lv_label_set_long_mode(ac_blast_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ac_blast_label, lv_pct(92));
    lv_label_set_text(ac_blast_label, "Liga o AC procurando qual protocolo responde.");
}

static void build_main_page(lv_obj_t *page, lv_obj_t *tv_page, lv_obj_t *ac_page)
{
    lv_obj_set_style_pad_all(page, 12, 0);
    lv_obj_set_style_pad_row(page, 14, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *tv_btn = create_family_button(page, "TV");
    lv_menu_set_load_page_event(menu, tv_btn, tv_page);

    lv_obj_t *ac_btn = create_family_button(page, "AC");
    lv_menu_set_load_page_event(menu, ac_btn, ac_page);
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        ac_blast_stop();
        ac_blast_label = NULL;
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

void ui_ir_remote_enter(lv_obj_t *parent)
{
    setup_tv_remote();
    setup_ac_remote();

    menu = create_menu(parent, back_event_handler, 44);  // back um pouco menor

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *tv_page = lv_menu_page_create(menu, NULL);
    build_tv_page(tv_page);

    lv_obj_t *ac_page = lv_menu_page_create(menu, NULL);
    build_ac_page(ac_page);

    build_main_page(main_page, tv_page, ac_page);

    lv_menu_set_page(menu, main_page);
}

void ui_ir_remote_exit(lv_obj_t *parent)
{
    (void)parent;
    ac_blast_stop();
    ac_blast_label = NULL;
}

app_t ui_ir_remote_main = {
    .setup_func_cb = ui_ir_remote_enter,
    .exit_func_cb = ui_ir_remote_exit,
    .user_data = nullptr,
};

#endif
