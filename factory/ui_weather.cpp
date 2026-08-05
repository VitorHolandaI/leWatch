/**
 * @file      ui_weather.cpp
 * @brief     Weather + news block shown inside the WiFi menu when connected.
 *
 * Free, keyless sources:
 *   - Weather: Open-Meteo (http, no key) for Campina Grande - PB.
 *   - News:    Tom's Hardware RSS (https, no key), first headlines from <item><title>.
 *
 * HTTP runs in a FreeRTOS task (blocking) and writes static buffers; an LVGL
 * timer polls a flag and updates the labels from the LVGL thread.
 */
#include "ui_define.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(ARDUINO)
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Campina Grande - PB
#define WEATHER_URL "http://api.open-meteo.com/v1/forecast?latitude=-7.23&longitude=-35.88&current=temperature_2m,relative_humidity_2m,weather_code&timezone=America/Fortaleza"
#define NEWS_URL    "https://www.tomshardware.com/feeds.xml"

#define NEWS_MAX       5
#define HTTP_BUF_SIZE  16384
#define TITLE_LEN      100
#define FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)

static lv_obj_t  *s_weather_label = NULL;
static lv_obj_t  *s_news_labels[NEWS_MAX];
static lv_timer_t *s_poll_timer = NULL;

static volatile bool s_fetching = false;
static volatile bool s_ready    = false;
static char s_weather_text[180];
static char s_news_text[NEWS_MAX][TITLE_LEN];
static int  s_news_count = 0;
static bool s_has_weather = false;
static uint32_t s_last_fetch_ms = 0;

// --- helpers --------------------------------------------------------------

static const char *wmo_text(int c)
{
    if (c < 0)  return "--";
    if (c == 0) return "Limpo";
    if (c <= 3)  return "Nuvens";
    if (c <= 48) return "Nevoa";
    if (c <= 67) return "Chuva";
    if (c <= 77) return "Neve";
    if (c <= 82) return "Pancadas";
    if (c <= 99) return "Trovoada";
    return "--";
}

// Fold UTF-8/Latin-1 accents and common punctuation to ASCII so titles render
// in the default font instead of boxes.
static char latin1_c3_to_ascii(unsigned char d)
{
    static const char map[64] = {
        'A', 'A', 'A', 'A', 'A', 'A', '?', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
        '?', 'N', 'O', 'O', 'O', 'O', 'O', '?', '?', 'U', 'U', 'U', 'U', '?', '?', '?',
        'a', 'a', 'a', 'a', 'a', 'a', '?', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
        '?', 'n', 'o', 'o', 'o', 'o', 'o', '?', '?', 'u', 'u', 'u', 'u', '?', '?', '?',
    };
    return (d >= 0x80 && d <= 0xBF) ? map[d - 0x80] : '?';
}

static char latin1_to_ascii(unsigned char d)
{
    switch (d) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'A';
        case 0xC7: return 'C';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'E';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
        case 0xD1: return 'N';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'O';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'U';
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
        case 0xE7: return 'c';
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
        case 0xF1: return 'n';
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
        default: return '?';
    }
}

static void to_ascii(char *s)
{
    char *o = s;
    for (unsigned char *p = (unsigned char *)s; *p; ) {
        if (*p < 0x80) {
            *o++ = (char)*p++;
        } else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            *o++ = latin1_c3_to_ascii(p[1]);
            p += 2;
        } else if (*p == 0xC2 && p[1] >= 0x80 && p[1] <= 0xBF) {
            *o++ = (p[1] == 0xA0) ? ' ' : '?';
            p += 2;
        } else if (*p == 0xE2 && p[1] == 0x80 && p[2]) {
            unsigned char d = p[2];
            if (d == 0x93 || d == 0x94) {
                *o++ = '-';
            } else if (d == 0x98 || d == 0x99) {
                *o++ = '\'';
            } else if (d == 0x9C || d == 0x9D) {
                *o++ = '"';
            } else {
                *o++ = '?';
            }
            p += 3;
        } else if (*p >= 0xC0 && p[1] >= 0x80 && p[1] <= 0xBF) {
            // Other UTF-8 sequence: one placeholder, skip the continuation.
            *o++ = '?';
            p += 2;
        } else if (*p >= 0xA0) {
            *o++ = latin1_to_ascii(*p++);
        } else {
            p++;
        }
    }
    *o = 0;
}

static void unescape_basic(char *s)
{
    struct { const char *e; char c; } map[] = {
        {"&amp;", '&'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}, {"&lt;", '<'}, {"&gt;", '>'},
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        char *pos;
        size_t elen = strlen(map[i].e);
        while ((pos = strstr(s, map[i].e)) != NULL) {
            *pos = map[i].c;
            memmove(pos + 1, pos + elen, strlen(pos + elen) + 1);
        }
    }
}

static void parse_weather(const char *buf)
{
    const char *cur = strstr(buf, "\"current\":");
    if (!cur) return;
    const char *t = strstr(cur, "\"temperature_2m\":");
    const char *h = strstr(cur, "\"relative_humidity_2m\":");
    const char *c = strstr(cur, "\"weather_code\":");
    const char *time = strstr(cur, "\"time\":\"");
    float temp = t ? atof(t + strlen("\"temperature_2m\":")) : 0;
    int humidity = h ? atoi(h + strlen("\"relative_humidity_2m\":")) : -1;
    int code = c ? atoi(c + strlen("\"weather_code\":")) : -1;
    char hhmm[6] = "--:--";
    if (time) {
        time += strlen("\"time\":\"");
        const char *clock = strchr(time, 'T');
        if (clock && clock[1] && clock[2] && clock[3] && clock[4]) {
            memcpy(hhmm, clock + 1, 5);
            hhmm[5] = 0;
        }
    }
    snprintf(s_weather_text, sizeof(s_weather_text),
             "%d C  %s\nUmidade: %d%%\nAtualizado: %s",
             (int)(temp + (temp < 0 ? -0.5f : 0.5f)), wmo_text(code), humidity, hhmm);
    s_has_weather = true;
}

static void parse_news(const char *buf)
{
    s_news_count = 0;
    const char *p = strstr(buf, "<item>");
    if (!p) return;
    while (s_news_count < NEWS_MAX) {
        p = strstr(p, "<title>");
        if (!p) break;
        p += 7;
        const char *end = strstr(p, "</title>");
        if (!end) break;
        if (strncmp(p, "<![CDATA[", 9) == 0) {
            p += 9;
            const char *cdata_end = strstr(p, "]]>");
            if (cdata_end && cdata_end < end) {
                end = cdata_end;
            }
        }
        size_t len = end - p;
        if (len >= TITLE_LEN) len = TITLE_LEN - 1;
        char *dst = s_news_text[s_news_count];
        memcpy(dst, p, len);
        dst[len] = 0;
        unescape_basic(dst);
        to_ascii(dst);
        s_news_count++;
        p = end + 8;
    }
}

// --- network task ---------------------------------------------------------

#if defined(ARDUINO)
static void weather_task(void *arg)
{
    char *buf = (char *)malloc(HTTP_BUF_SIZE);
    if (buf) {
        if (hw_http_get(WEATHER_URL, buf, HTTP_BUF_SIZE) > 0) parse_weather(buf);
        if (hw_http_get(NEWS_URL, buf, HTTP_BUF_SIZE) > 0) parse_news(buf);
        free(buf);
    }
    if (!s_has_weather && !s_weather_text[0]) {
        snprintf(s_weather_text, sizeof(s_weather_text), "Falha ao atualizar\nNova tentativa em 10 min");
    }
    s_last_fetch_ms = millis();
    s_ready = true;
    s_fetching = false;
    vTaskDelete(NULL);
}
#endif

static bool cache_is_fresh()
{
#if defined(ARDUINO)
    return s_last_fetch_ms != 0 && (uint32_t)(millis() - s_last_fetch_ms) < FETCH_INTERVAL_MS;
#else
    return s_has_weather || s_weather_text[0];
#endif
}

static void start_fetch()
{
#if defined(ARDUINO)
    if (s_fetching) return;
    if (cache_is_fresh()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    s_fetching = true;
    s_ready = false;
    if (xTaskCreate(weather_task, "weather", 24576, NULL, 1, NULL) != pdPASS) {
        snprintf(s_weather_text, sizeof(s_weather_text), "Sem memoria p/ atualizar");
        s_last_fetch_ms = millis();
        s_ready = true;
        s_fetching = false;
    }
#endif
}

// --- UI -------------------------------------------------------------------

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
#if defined(ARDUINO)
    if (!s_fetching && !s_ready && s_weather_label && !cache_is_fresh() && WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(s_weather_label, "Carregando...");
        start_fetch();
    }
#endif
    if (s_ready) {
        s_ready = false;
        if (s_weather_label && s_weather_text[0]) {
            lv_label_set_text(s_weather_label, s_weather_text);
        }
        for (int i = 0; i < NEWS_MAX; i++) {
            if (!s_news_labels[i]) continue;
            lv_label_set_text(s_news_labels[i], i < s_news_count ? s_news_text[i] : "");
        }
    }
}

static void weather_cleanup_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_poll_timer) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    s_weather_label = NULL;
    for (int i = 0; i < NEWS_MAX; i++) s_news_labels[i] = NULL;
}

static lv_obj_t *make_section(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

static void ensure_running(lv_obj_t *cont)
{
    lv_obj_add_event_cb(cont, weather_cleanup_cb, LV_EVENT_DELETE, NULL);
    if (!s_poll_timer) {
        s_poll_timer = lv_timer_create(poll_cb, 500, NULL);
    }
    start_fetch();
}

// Weather sub-page content.
void ui_weather_attach(lv_obj_t *parent)
{
    lv_obj_t *cont = make_section(parent);
    lv_obj_t *wtitle = lv_label_create(cont);
    lv_label_set_text(wtitle, LV_SYMBOL_GPS " Campina Grande");
    s_weather_label = lv_label_create(cont);
    lv_obj_set_width(s_weather_label, lv_pct(100));
    lv_label_set_long_mode(s_weather_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_weather_label, s_has_weather ? s_weather_text : "Carregando...");

    ensure_running(cont);
}

// News sub-page content.
void ui_news_attach(lv_obj_t *parent)
{
    s_news_count = 0;

    lv_obj_t *cont = make_section(parent);
    lv_obj_t *ntitle = lv_label_create(cont);
    lv_label_set_text(ntitle, LV_SYMBOL_LIST " Tech News (Tom's)");
    for (int i = 0; i < NEWS_MAX; i++) {
        s_news_labels[i] = lv_label_create(cont);
        lv_obj_set_width(s_news_labels[i], lv_pct(100));
        lv_label_set_long_mode(s_news_labels[i], LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_news_labels[i], "...");
    }

    ensure_running(cont);
}

// ---------------------------------------------------------------------------
// Apps standalone (grade): Weather e Noticias como widgets proprios, fora do
// app WiFi. Cada um = create_menu + pagina + attach + back padronizado.
// ---------------------------------------------------------------------------
static lv_obj_t *s_weather_menu = NULL;
static lv_obj_t *s_news_menu = NULL;

static void weather_back_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(s_weather_menu, obj)) {
        lv_obj_clean(s_weather_menu);
        lv_obj_del(s_weather_menu);
        s_weather_menu = NULL;
        menu_show();
    }
}

static void ui_weather_enter(lv_obj_t *parent)
{
    s_weather_menu = create_menu(parent, weather_back_cb);
    lv_obj_t *page = lv_menu_page_create(s_weather_menu, NULL);
    ui_weather_attach(page);
    lv_menu_set_page(s_weather_menu, page);
}

static void news_back_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(s_news_menu, obj)) {
        lv_obj_clean(s_news_menu);
        lv_obj_del(s_news_menu);
        s_news_menu = NULL;
        menu_show();
    }
}

static void ui_news_enter(lv_obj_t *parent)
{
    s_news_menu = create_menu(parent, news_back_cb);
    lv_obj_t *page = lv_menu_page_create(s_news_menu, NULL);
    ui_news_attach(page);
    lv_menu_set_page(s_news_menu, page);
}

app_t ui_weather_main = {
    .setup_func_cb = ui_weather_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};

app_t ui_news_main = {
    .setup_func_cb = ui_news_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};
