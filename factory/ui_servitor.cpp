/**
 * @file      ui_servitor.cpp
 * @brief     Voice assistant app: records a question, sends it to the
 *            ServitorAssistant /file_recorded endpoint, shows the text answer or
 *            plays the synthesized audio reply.
 *
 * Uses the existing 16 kHz mono WAV recorder (hw_record_*) and the WAV player
 * (hw_set_sd_music_play). The multipart upload runs in a FreeRTOS task; an LVGL
 * timer polls a flag and updates the UI from the LVGL thread.
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

#define SERVITOR_RESP_PATH "/servitor_resp.wav"
#define SERVITOR_GAIN 8        // Piper TTS peaks sit ~-20 dBFS; 8x (+18 dB) is
                               // safe from clipping on this quiet source

static lv_obj_t  *s_menu = NULL;
static lv_obj_t  *s_status = NULL;
static lv_obj_t  *s_answer = NULL;
static lv_obj_t  *s_rec_label = NULL;
static lv_timer_t *s_poll = NULL;

static volatile bool s_recording = false;
static volatile bool s_sending   = false;
static volatile bool s_ready     = false;
static bool s_want_audio  = false;
static bool s_resp_audio  = false;
static bool s_resp_ok     = false;
static bool s_playing     = false;   // boosted-gain playback in progress
static int  s_play_grace  = 0;       // poll ticks before checking end-of-playback
static uint32_t s_send_t0  = 0;      // when the upload task started, for elapsed ticks
static char s_answer_text[512];
static char s_wav_path[80];

// --- response parsing ------------------------------------------------------

// Copy JSON string value for @p key into @p out, undoing basic escapes.
static bool json_str_value(const char *buf, const char *key, char *out, int outsize)
{
    const char *p = strstr(buf, key);
    if (!p) return false;
    p += strlen(key);
    while (*p && *p != '"') p++;   // skip ": whitespace to opening quote
    if (*p != '"') return false;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < outsize - 1) {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            c = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
        }
        out[n++] = c;
    }
    out[n] = 0;
    return true;
}

static void parse_answer(const char *buf)
{
    if (json_str_value(buf, "\"response\"", s_answer_text, sizeof(s_answer_text))) return;
    if (strstr(buf, "ignored")) {
        snprintf(s_answer_text, sizeof(s_answer_text), "Ignorado: audio curto ou ruido.");
        return;
    }
    snprintf(s_answer_text, sizeof(s_answer_text), "Resposta invalida do servidor.");
}

// --- network task ----------------------------------------------------------

#if defined(ARDUINO)
static void servitor_task(void *arg)
{
    LV_UNUSED(arg);
    s_resp_ok = false;
    s_resp_audio = false;
    bool is_audio = false;
    Serial.printf("[srv] task send path=%s want_audio=%d\n", s_wav_path, s_want_audio);
    // Try LAN first, then the VPN fallback (see nextcloud_config.h).
    const char *const srv_urls[] = { SERVITOR_API_URL, SERVITOR_API_URL_FALLBACK };
    int n = 0;
    for (const char *url : srv_urls) {
        n = hw_http_post_wav(url, s_wav_path, s_want_audio ? "audio" : "text",
                             SERVITOR_RESP_PATH, &is_audio);
        if (n > 0) break;
        Serial.printf("[srv] %s failed, trying next\n", url);
    }
    Serial.printf("[srv] post ret=%d is_audio=%d\n", n, is_audio);
    // The question recording is transient: hw_http_post_wav already read it into
    // PSRAM, so delete it now (sent or failed) instead of leaving /rec_*.wav to
    // pile up on FFat. Leftovers from before this fix are removable in Recorder.
    hw_record_delete(s_wav_path);
    Serial.printf("[srv] deleted rec %s\n", s_wav_path);
    if (n > 0) {
        s_resp_audio = is_audio;
        if (!is_audio) {
            char *buf = (char *)malloc(2048);
            if (buf) {
                if (hw_fs_read_file(SERVITOR_RESP_PATH, buf, 2048) > 0) parse_answer(buf);
                free(buf);
            }
        }
        s_resp_ok = true;
    }
    s_ready = true;
    s_sending = false;
    vTaskDelete(NULL);
}
#endif

static void start_send()
{
#if defined(ARDUINO)
    if (s_sending) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (s_status) lv_label_set_text(s_status, "WiFi desligado");
        return;
    }
    s_sending = true;
    s_send_t0 = millis();
    s_ready = false;
    set_low_power_mode_flag(false);   // the LLM reply can take a minute; stay awake
    if (s_status) lv_label_set_text(s_status, "Enviando...");
    // 16 KB stack: the heavy buffers (WAV, multipart body, response) live in
    // PSRAM/heap, so the task frame is small. 24 KB failed to allocate once the
    // LVGL draw buffers moved into internal SRAM ("Sem memoria" on record).
    if (xTaskCreate(servitor_task, "servitor", 16384, NULL, 1, NULL) != pdPASS) {
        s_sending = false;
        set_low_power_mode_flag(true);
        if (s_status) lv_label_set_text(s_status, "Sem memoria");
    }
#else
    if (s_status) lv_label_set_text(s_status, "Voz indisponivel no sim");
#endif
}

// --- UI --------------------------------------------------------------------

static void record_toggle_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_recording) {
        if (hw_record_start()) {
            s_recording = true;
            if (s_rec_label) lv_label_set_text(s_rec_label, LV_SYMBOL_STOP " Parar");
            if (s_status) lv_label_set_text(s_status, "Gravando... (toque p/ parar)");
        } else if (s_status) {
            lv_label_set_text(s_status, "Falha ao iniciar gravacao");
        }
        return;
    }
    hw_record_stop();
    s_recording = false;
    if (s_rec_label) lv_label_set_text(s_rec_label, LV_SYMBOL_AUDIO " Falar");
    // A previous upload is still reading s_wav_path on its task thread; do NOT
    // overwrite it (torn path). start_send() would bail on the same flag anyway.
    if (s_sending) {
        if (s_status) lv_label_set_text(s_status, "Aguarde a resposta anterior");
        return;
    }
    vector<string> recs;
    hw_record_list(recs);
    if (recs.empty()) {
        if (s_status) lv_label_set_text(s_status, "Sem gravacao");
        return;
    }
    strncpy(s_wav_path, recs.back().c_str(), sizeof(s_wav_path) - 1);
    s_wav_path[sizeof(s_wav_path) - 1] = 0;
    start_send();
}

static void mode_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    s_want_audio = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

// Play the saved response WAV with boosted gain (TTS is quiet).
static void play_response_audio()
{
    hw_set_audio_gain(SERVITOR_GAIN);
    hw_set_sd_music_play(AUDIO_SOURCE_FATFS, SERVITOR_RESP_PATH);
    s_playing = true;
    s_play_grace = 5;   // ~1.5 s before we start checking for end-of-play
}

static void replay_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_resp_audio) {
        if (s_status) lv_label_set_text(s_status, "Ultima resposta foi texto");
        return;
    }
    play_response_audio();
    if (s_status) lv_label_set_text(s_status, "Replay...");
}

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    // Restore normal gain once the boosted TTS playback has finished.
    if (s_playing) {
        if (s_play_grace > 0) s_play_grace--;
        else if (!hw_player_running()) { hw_set_audio_gain(1); s_playing = false; }
    }
    // STT + LLM + TTS can take a minute; show live elapsed time so the
    // "Enviando..." status does not look frozen. (millis is ARDUINO-only.)
#if defined(ARDUINO)
    if (s_sending) {
        if (s_status) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Enviando... %lus",
                     (unsigned long)((millis() - s_send_t0) / 1000));
            lv_label_set_text(s_status, buf);
        }
        return;
    }
#endif
    if (!s_ready) return;
    s_ready = false;
    set_low_power_mode_flag(true);   // reply arrived; allow idle sleep again
    if (s_resp_ok && s_resp_audio) {
        play_response_audio();
        if (s_answer) lv_label_set_text(s_answer, "[audio] tocando resposta...");
        if (s_status) lv_label_set_text(s_status, "Resposta em audio");
    } else if (s_resp_ok) {
        if (s_answer) lv_label_set_text(s_answer, s_answer_text);
        if (s_status) lv_label_set_text(s_status, "Pronto");
    } else if (s_status) {
        lv_label_set_text(s_status, "Falha ao enviar");
    }
}

static void cleanup_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_poll) {
        lv_timer_del(s_poll);
        s_poll = NULL;
    }
    set_low_power_mode_flag(true);   // in case we left during a pending send
    if (s_playing) { hw_set_play_stop(); hw_set_audio_gain(1); s_playing = false; }
    s_status = NULL;
    s_answer = NULL;
    s_rec_label = NULL;
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(s_menu, obj)) {
        if (s_recording) { hw_record_stop(); s_recording = false; }
        lv_obj_clean(s_menu);
        lv_obj_del(s_menu);
        s_menu = NULL;
        menu_show();
    }
}

void ui_servitor_enter(lv_obj_t *parent)
{
    s_menu = create_menu(parent, back_event_handler);
    lv_obj_t *page = lv_menu_page_create(s_menu, NULL);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_add_event_cb(page, cleanup_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, LV_SYMBOL_AUDIO " Servitor (voz)");

    lv_obj_t *rec_btn = lv_btn_create(page);
    lv_obj_set_width(rec_btn, lv_pct(100));
    lv_obj_add_event_cb(rec_btn, record_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_rec_label = lv_label_create(rec_btn);
    lv_label_set_text(s_rec_label, LV_SYMBOL_AUDIO " Falar");

    create_switch(page, LV_SYMBOL_VOLUME_MAX, "Resposta em audio", s_want_audio, mode_switch_cb);

    lv_obj_t *replay_btn = lv_btn_create(page);
    lv_obj_set_width(replay_btn, lv_pct(100));
    lv_obj_add_event_cb(replay_btn, replay_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(replay_btn), LV_SYMBOL_REFRESH " Replay audio");

    s_status = lv_label_create(page);
    lv_label_set_text(s_status, "Pronto");

    s_answer = lv_label_create(page);
    lv_obj_set_width(s_answer, lv_pct(100));
    lv_label_set_long_mode(s_answer, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_answer, "Toque em Falar, faca a pergunta, toque p/ parar.");

    if (!s_poll) s_poll = lv_timer_create(poll_cb, 300, NULL);

    lv_menu_set_page(s_menu, page);
}

app_t ui_servitor_main = {
    .setup_func_cb = ui_servitor_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};
