/**
 * @file      ui_nextcloud.cpp
 * @brief     Agenda app: syncs Nextcloud events + tasks (next 7 days) from the
 *            ServitorAssistant MCP server and notifies before each one.
 *
 * The watch calls the MCP tool `sync_nextcloud_agenda` directly (JSON-RPC over
 * Streamable HTTP), parses the structured JSON snapshot, stores it on flash, and
 * a persistent timer raises a full-screen reminder before an item is due. It
 * never parses the LLM agent's text. Contract: docs/mcp/nextcloud-appliance-sync.md.
 *
 * HTTP runs in a FreeRTOS task (blocking); an LVGL timer polls a flag and
 * refreshes the list from the LVGL thread, mirroring ui_weather.cpp.
 */
#include "ui_define.h"
#include "nextcloud_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(ARDUINO)
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define AGENDA_PATH   "/nc_agenda.json"
#define MAX_ITEMS     200
#define MAX_TASK_LISTS 32   // distinct Nextcloud task lists shown as groups
#define TITLE_LEN     48
#define RAW_BUF_SIZE  131072  // outer SSE/JSON-RPC envelope (inner JSON is escaped)
#define JSON_BUF_SIZE 98304   // inner snapshot JSON after unescape (~53KB observed)
#define LIST_SHOWN    12

// A busy week can produce tens of KB of snapshot; allocate the scratch buffers
// from PSRAM on device (8 MB) so they never squeeze the internal heap.
#if defined(ARDUINO)
#define NC_ALLOC(n) ps_malloc(n)
#else
#define NC_ALLOC(n) malloc(n)
#endif

typedef struct {
    char title[TITLE_LEN];
    char list[24];        // task list name (empty for events)
    long long when_utc;   // epoch seconds (UTC); -1 when the task has no due date
    int lead_min;         // reminder lead in minutes before when_utc
    bool is_task;
    bool notified;
} AgendaItem;

static AgendaItem s_items[MAX_ITEMS];
static int s_item_count = 0;

static lv_obj_t  *s_menu = NULL;
static lv_obj_t  *s_status_label = NULL;
static lv_obj_t  *s_events_cont = NULL;   // today's calendar events (root page)
static lv_obj_t  *s_tasks_page = NULL;    // tasks grouped by list (sub-page)
static lv_timer_t *s_poll_timer = NULL;
static lv_timer_t *s_reminder_timer = NULL;   // persists across screens
static lv_obj_t  *s_notify_overlay = NULL;

static volatile bool s_fetching = false;
static volatile bool s_ready    = false;
static bool s_last_ok = false;

// --- time helpers (civil <-> epoch, no libc timezone dependency) -----------

static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static void civil_from_days(long long z, int &y, int &m, int &d)
{
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp + (mp < 10 ? 3 : -9));
    y = (int)yoe + (int)era * 400 + (m <= 2);
}

static long long civil_to_epoch(int y, int mo, int d, int h, int mi, int s)
{
    return days_from_civil(y, mo, d) * 86400LL + (long long)h * 3600 + mi * 60 + s;
}

static long long parse_iso_utc(const char *s)
{
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return -1;
    return civil_to_epoch(y, mo, d, h, mi, se);
}

static long long now_utc_epoch()
{
    struct tm now = {};
    hw_get_date_time(now);
    long long local = civil_to_epoch(now.tm_year + 1900, now.tm_mon + 1,
                                     now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
    return local - (long long)NEXTCLOUD_TZ_OFFSET_MIN * 60;
}

static void utc_to_local_wall(long long utc, int &y, int &mo, int &d, int &h, int &mi)
{
    long long local = utc + (long long)NEXTCLOUD_TZ_OFFSET_MIN * 60;
    long long days = local / 86400, rem = local % 86400;
    if (rem < 0) { rem += 86400; days--; }
    civil_from_days(days, y, mo, d);
    h = (int)(rem / 3600);
    mi = (int)((rem % 3600) / 60);
}

// --- text helpers ----------------------------------------------------------

// Fold common Latin-1 (UTF-8 C3 range) accents to ASCII so titles render in the
// default font instead of boxes; drop anything else non-ASCII.
static void nc_fold_ascii(char *s)
{
    static const char map[64] = {
        'A','A','A','A','A','A','?','C','E','E','E','E','I','I','I','I',
        '?','N','O','O','O','O','O','?','?','U','U','U','U','?','?','?',
        'a','a','a','a','a','a','?','c','e','e','e','e','i','i','i','i',
        '?','n','o','o','o','o','o','?','?','u','u','u','u','?','?','?',
    };
    char *o = s;
    for (unsigned char *p = (unsigned char *)s; *p; ) {
        if (*p < 0x80) { *o++ = (char)*p++; }
        else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) { *o++ = map[p[1] - 0x80]; p += 2; }
        else if (p[1] >= 0x80 && p[1] <= 0xBF) { *o++ = '?'; p += 2; }
        else { p++; }
    }
    *o = 0;
}

// Copy a JSON string value for @p key within [obj,objend) into @p out, undoing
// basic escapes. @p key must include the quotes and colon, e.g. "\"title\":".
static bool json_get_string(const char *obj, const char *objend, const char *key,
                            char *out, int outsize)
{
    const char *p = strstr(obj, key);
    if (!p || p >= objend) return false;
    p += strlen(key);
    while (p < objend && (*p == ' ' || *p == ':')) p++;
    if (p >= objend || *p != '"') return false;
    p++;
    int n = 0;
    while (p < objend && *p != '"' && n < outsize - 1) {
        char c = *p++;
        if (c == '\\' && p < objend) {
            char e = *p++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'u') { p += (p + 4 <= objend) ? 4 : 0; c = '?'; }
            else c = e;   // \" \\ \/
        }
        out[n++] = c;
    }
    out[n] = 0;
    return true;
}

static long long json_get_int(const char *obj, const char *objend,
                              const char *key, long long def)
{
    const char *p = strstr(obj, key);
    if (!p || p >= objend) return def;
    p += strlen(key);
    while (p < objend && (*p == ' ' || *p == ':')) p++;
    return (p < objend) ? atoll(p) : def;
}

// --- snapshot parsing ------------------------------------------------------

static void add_item(const char *obj, const char *objend, bool is_task)
{
    if (s_item_count >= MAX_ITEMS) return;
    AgendaItem &it = s_items[s_item_count];
    if (!json_get_string(obj, objend, "\"title\":", it.title, sizeof(it.title))) return;
    nc_fold_ascii(it.title);
    it.list[0] = 0;
    if (is_task && json_get_string(obj, objend, "\"list\":", it.list, sizeof(it.list))) {
        nc_fold_ascii(it.list);
    }
    char when[32] = "";
    json_get_string(obj, objend, is_task ? "\"due\":" : "\"start\":", when, sizeof(when));
    it.when_utc = when[0] ? parse_iso_utc(when) : -1;
    long long lead = json_get_int(obj, objend, "\"reminder_minutes_before\":", 0);
    it.lead_min = lead > 0 ? (int)lead : NEXTCLOUD_DEFAULT_LEAD_MIN;
    it.is_task = is_task;
    it.notified = false;
    s_item_count++;
}

// Walk a JSON array of objects (string-aware brace matching) and add each one.
static void parse_array(const char *json, const char *key, bool is_task)
{
    const char *p = strstr(json, key);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    while (*p) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;
        const char *start = p;
        int depth = 0;
        bool in_str = false;
        while (*p) {
            char c = *p;
            if (in_str) {
                if (c == '\\' && p[1]) { p += 2; continue; }
                if (c == '"') in_str = false;
            } else if (c == '"') {
                in_str = true;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                if (--depth == 0) { p++; break; }
            }
            p++;
        }
        add_item(start, p, is_task);
    }
}

static void sort_items()
{
    for (int i = 1; i < s_item_count; i++) {
        AgendaItem key = s_items[i];
        long long kv = key.when_utc < 0 ? INT64_MAX : key.when_utc;
        int j = i - 1;
        while (j >= 0) {
            long long jv = s_items[j].when_utc < 0 ? INT64_MAX : s_items[j].when_utc;
            if (jv <= kv) break;
            s_items[j + 1] = s_items[j];
            j--;
        }
        s_items[j + 1] = key;
    }
}

static void parse_snapshot(const char *json)
{
    s_item_count = 0;
    parse_array(json, "\"events\"", false);
    parse_array(json, "\"tasks\"", true);
    sort_items();
}

// Pull the tool result payload out of the MCP envelope (result.content[].text).
static bool extract_snapshot(const char *raw, char *json, int json_size)
{
    return json_get_string(raw, raw + strlen(raw), "\"text\":", json, json_size);
}

static void load_from_flash()
{
    char *buf = (char *)NC_ALLOC(JSON_BUF_SIZE);
    if (!buf) return;
    if (hw_fs_read_file(AGENDA_PATH, buf, JSON_BUF_SIZE) > 0) {
        parse_snapshot(buf);
    }
    free(buf);
}

// --- network fetch ---------------------------------------------------------

#if defined(ARDUINO)
static void agenda_task(void *arg)
{
    LV_UNUSED(arg);
    char body[176];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":"
             "{\"name\":\"sync_nextcloud_agenda\",\"arguments\":"
             "{\"days\":%d,\"all_tasks\":true}}}",
             NEXTCLOUD_SYNC_DAYS);
    char *raw = (char *)NC_ALLOC(RAW_BUF_SIZE);
    char *json = (char *)NC_ALLOC(JSON_BUF_SIZE);
    s_last_ok = false;
    // Try LAN first, then the VPN fallback (see nextcloud_config.h).
    static const char *const mcp_urls[] = { NEXTCLOUD_MCP_URL, NEXTCLOUD_MCP_URL_FALLBACK };
    if (raw && json) {
        for (const char *url : mcp_urls) {
            int n = hw_mcp_post(url, body, raw, RAW_BUF_SIZE);
            if (n <= 0 || !extract_snapshot(raw, json, JSON_BUF_SIZE)) {
                Serial.printf("[agenda] %s failed, trying next\n", url);
                continue;
            }
            // Only persist to flash here. Parsing into s_items[] is deliberately
            // NOT done on this task thread: s_items[]/s_item_count are also read
            // by reminder_cb (a persistent LVGL timer) and load_from_flash, both
            // on the LVGL thread. poll_cb reparses from flash on the LVGL thread
            // so every s_items[] access stays single-threaded (no lock needed).
            hw_fs_write_file(AGENDA_PATH, json, strlen(json));
            s_last_ok = true;
            break;
        }
    }
    free(raw);
    free(json);
    s_ready = true;
    s_fetching = false;
    vTaskDelete(NULL);
}
#endif

static void start_fetch()
{
#if defined(ARDUINO)
    if (s_fetching) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (s_status_label) lv_label_set_text(s_status_label, "WiFi desligado");
        return;
    }
    s_fetching = true;
    s_ready = false;
    if (s_status_label) lv_label_set_text(s_status_label, "Sincronizando...");
    // 16 KB stack: raw/json buffers (128+96 KB) are in PSRAM (NC_ALLOC), so the
    // task frame is small. 24 KB failed once draw buffers moved to internal SRAM.
    if (xTaskCreate(agenda_task, "agenda", 16384, NULL, 1, NULL) != pdPASS) {
        s_fetching = false;
        if (s_status_label) lv_label_set_text(s_status_label, "Sem memoria");
    }
#else
    if (s_status_label) lv_label_set_text(s_status_label, "Sync indisponivel no sim");
#endif
}

// --- reminder notification (works over any screen) -------------------------

// Clear the guard whenever the overlay dies by ANY path (OK button, screen
// rebuild, sleep/wake, layer_top clean). Without this, a teardown that bypassed
// notify_dismiss_cb left s_notify_overlay dangling non-NULL, so reminder_cb /
// show_notify returned early forever -> reminders "worked once, then never".
static void notify_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_notify_overlay = NULL;
}

static void notify_dismiss_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_notify_overlay) {
        lv_obj_del(s_notify_overlay);   // triggers notify_deleted_cb -> NULLs it
    }
}

static void show_notify(const AgendaItem &it)
{
    if (s_notify_overlay) return;   // one reminder at a time
    hw_vibrate_max();
    s_notify_overlay = lv_obj_create(lv_layer_top());
    lv_obj_add_event_cb(s_notify_overlay, notify_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(s_notify_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_notify_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_notify_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *head = lv_label_create(s_notify_overlay);
    lv_label_set_text(head, it.is_task ? LV_SYMBOL_LIST " Tarefa" : LV_SYMBOL_BELL " Evento");

    int y, mo, d, h, mi;
    char line[TITLE_LEN + 24];
    if (it.when_utc >= 0) {
        utc_to_local_wall(it.when_utc, y, mo, d, h, mi);
        snprintf(line, sizeof(line), "%s\n%02d:%02d %02d/%02d", it.title, h, mi, d, mo);
    } else {
        snprintf(line, sizeof(line), "%s", it.title);
    }
    lv_obj_t *body = lv_label_create(s_notify_overlay);
    lv_obj_set_width(body, lv_pct(90));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, line);

    lv_obj_t *ok = lv_btn_create(s_notify_overlay);
    lv_obj_add_event_cb(ok, notify_dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(ok), "OK");
}

static void reminder_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_notify_overlay) return;
    long long now = now_utc_epoch();
    for (int i = 0; i < s_item_count; i++) {
        AgendaItem &it = s_items[i];
        if (it.notified || it.when_utc < 0) continue;
        long long fire = it.when_utc - (long long)it.lead_min * 60;
        if (now >= fire && now <= it.when_utc + 60) {
            it.notified = true;
            show_notify(it);
            return;
        }
    }
}

// --- list UI ---------------------------------------------------------------

static bool same_local_day(long long when_utc, int ny, int nmo, int nd)
{
    if (when_utc < 0) return false;
    int y, mo, d, h, mi;
    utc_to_local_wall(when_utc, y, mo, d, h, mi);
    return y == ny && mo == nmo && d == nd;
}

// First page: only today's calendar events (already time-sorted).
static void build_today_events()
{
    if (!s_events_cont) return;
    lv_obj_clean(s_events_cont);
    struct tm now = {};
    hw_get_date_time(now);
    int ny = now.tm_year + 1900, nmo = now.tm_mon + 1, nd = now.tm_mday;
    int shown = 0;
    for (int i = 0; i < s_item_count; i++) {
        AgendaItem &it = s_items[i];
        if (it.is_task || !same_local_day(it.when_utc, ny, nmo, nd)) continue;
        int y, mo, d, h, mi;
        utc_to_local_wall(it.when_utc, y, mo, d, h, mi);
        char line[TITLE_LEN + 16];
        snprintf(line, sizeof(line), "%02d:%02d  %s", h, mi, it.title);
        lv_obj_t *lbl = lv_label_create(s_events_cont);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl, line);
        shown++;
    }
    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(s_events_cont);
        lv_label_set_text(empty, "Sem eventos hoje.");
    }
}

// Tasks sub-page: grouped by Nextcloud list, mirroring the server's task lists.
static void build_tasks_page()
{
    if (!s_tasks_page) return;
    lv_obj_clean(s_tasks_page);
    // Distinct Nextcloud task lists are few (~a handful); 32 is ample. Sized well
    // below MAX_ITEMS to avoid reserving 4.8 KB of .bss for a transient dedup.
    static char seen[MAX_TASK_LISTS][24];
    int seen_n = 0;
    bool any = false;
    for (int i = 0; i < s_item_count; i++) {
        if (!s_items[i].is_task) continue;
        const char *lst = s_items[i].list[0] ? s_items[i].list : "Tasks";
        bool done = false;
        for (int k = 0; k < seen_n; k++) {
            if (strcmp(seen[k], lst) == 0) { done = true; break; }
        }
        if (done) continue;
        if (seen_n >= MAX_TASK_LISTS) break;   // dedup table full: stop adding new groups
        strncpy(seen[seen_n], lst, sizeof(seen[0]) - 1);
        seen[seen_n][sizeof(seen[0]) - 1] = 0;
        seen_n++;

        lv_obj_t *hdr = lv_label_create(s_tasks_page);
        lv_obj_set_width(hdr, lv_pct(100));
        char htext[40];
        snprintf(htext, sizeof(htext), LV_SYMBOL_DIRECTORY " %s", lst);
        lv_label_set_text(hdr, htext);

        for (int j = 0; j < s_item_count; j++) {
            AgendaItem &it = s_items[j];
            if (!it.is_task) continue;
            const char *jl = it.list[0] ? it.list : "Tasks";
            if (strcmp(jl, lst) != 0) continue;
            char line[TITLE_LEN + 24];
            if (it.when_utc >= 0) {
                int y, mo, d, h, mi;
                utc_to_local_wall(it.when_utc, y, mo, d, h, mi);
                snprintf(line, sizeof(line), "  %02d/%02d  %s", d, mo, it.title);
            } else {
                snprintf(line, sizeof(line), "  --/--  %s", it.title);
            }
            lv_obj_t *lbl = lv_label_create(s_tasks_page);
            lv_obj_set_width(lbl, lv_pct(100));
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            lv_label_set_text(lbl, line);
            any = true;
        }
    }
    if (!any) {
        lv_obj_t *empty = lv_label_create(s_tasks_page);
        lv_label_set_text(empty, "Sem tarefas.");
    }
}

static void refresh_views()
{
    build_today_events();
    build_tasks_page();
}

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!s_ready) return;
    s_ready = false;
    // Reparse on the LVGL thread (the task only wrote flash) so s_items[] is
    // never touched from two threads. See agenda_task for the rationale.
    if (s_last_ok) load_from_flash();
    refresh_views();
    if (s_status_label) {
        lv_label_set_text(s_status_label,
                          s_last_ok ? "Atualizado" : "Falha ao sincronizar");
    }
}

static void sync_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    start_fetch();
}

static void cleanup_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_poll_timer) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    s_status_label = NULL;
    s_events_cont = NULL;
    s_tasks_page = NULL;
    // s_reminder_timer intentionally kept alive so reminders fire from any screen.
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(s_menu, obj)) {
        lv_obj_clean(s_menu);
        lv_obj_del(s_menu);
        s_menu = NULL;
        menu_show();
    }
}

void ui_nextcloud_enter(lv_obj_t *parent)
{
    s_menu = create_menu(parent, back_event_handler);
    lv_obj_t *root = lv_menu_page_create(s_menu, NULL);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 6, 0);
    lv_obj_add_event_cb(root, cleanup_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, LV_SYMBOL_CALL " Agenda (Nextcloud)");

    lv_obj_t *sync_btn = lv_btn_create(root);
    lv_obj_add_event_cb(sync_btn, sync_click_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(sync_btn), LV_SYMBOL_REFRESH " Sync");

    s_status_label = lv_label_create(root);
    lv_label_set_text(s_status_label, "Pronto");

    lv_obj_t *ev_hdr = lv_label_create(root);
    lv_label_set_text(ev_hdr, LV_SYMBOL_BELL " Eventos de hoje");

    s_events_cont = lv_obj_create(root);
    lv_obj_set_width(s_events_cont, lv_pct(100));
    lv_obj_set_height(s_events_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_events_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_events_cont, 4, 0);

    // Tasks sub-page (grouped by list) + button that navigates to it.
    s_tasks_page = lv_menu_page_create(s_menu, NULL);
    lv_obj_set_flex_flow(s_tasks_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_tasks_page, 4, 0);

    lv_obj_t *tasks_btn = lv_btn_create(root);
    lv_label_set_text(lv_label_create(tasks_btn), LV_SYMBOL_LIST " Tarefas");
    lv_menu_set_load_page_event(s_menu, tasks_btn, s_tasks_page);

    load_from_flash();
    refresh_views();

    if (!s_poll_timer) s_poll_timer = lv_timer_create(poll_cb, 500, NULL);
    if (!s_reminder_timer) s_reminder_timer = lv_timer_create(reminder_cb, 30000, NULL);

    lv_menu_set_page(s_menu, root);
}

// Start agenda reminders at boot so notifications fire in the background without
// having to open the Agenda app first. Loads the last synced snapshot from flash
// and arms the persistent 30 s reminder timer. Call once from setup (LVGL up).
void ui_nextcloud_reminders_init(void)
{
    load_from_flash();
    if (!s_reminder_timer) s_reminder_timer = lv_timer_create(reminder_cb, 30000, NULL);
}

app_t ui_nextcloud_main = {
    .setup_func_cb = ui_nextcloud_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};
