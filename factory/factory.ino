/**
 * @file      factory.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#include <esp_bt.h>
#include "hal_interface.h"
#include <WiFi.h>
#include "event_define.h"

extern void setupGui();
extern void ui_alarm_ring();
extern void ui_nextcloud_reminders_init();

static const char *ntpServer1 = "a.st1.ntp.br";     // Observatorio Nacional (BR)
static const char *ntpServer2 = "br.pool.ntp.org";  // Brazilian NTP pool
static const long  gmtOffset_sec = GMT_OFFSET_SECOND;
static const int   daylightOffset_sec = 0;
static SemaphoreHandle_t xSemaphore = NULL;


void instanceLockTake()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) != pdTRUE) {
            log_e("Failed to take semaphore");
            assert(0);
        }
    }
}

void instanceLockGive()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreGive(xSemaphore) != pdTRUE) {
            log_e("Failed to give semaphore");
            assert(0);
        }
    }
}
//#

// Callback function (gets called when time adjusts via NTP)
static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    // printLocalTime();
    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        instance.rtc.hwClockWrite();
    }
    // NOTE: VPN is NOT auto-started at boot. A crash inside WireGuard begin()
    // while the pref was ON caused a reboot LOOP (boot -> NTP -> VPN -> crash ->
    // boot...). Until that crash is root-caused, the tunnel only comes up when the
    // user taps "Ligar VPN" in Wireless during the session (hw_vpn_start_async).
}

// WARNING: This function is called from a separate FreeRTOS task (thread)!
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

// WARNING: This function is called from a separate FreeRTOS task (thread)!
// Tear the tunnel down when WiFi drops so WireGuard does not burn radio energy
// retrying handshakes over a dead link; time_available re-arms it on reconnect.
void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (hw_vpn_enabled_get()) {
        Serial.println("WiFi disconnected -> VPN down");
        hw_vpn_end();
    }
}

void setup()
{
    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    // Crash breadcrumb from the previous boot: if the watch rebooted during VPN
    // bring-up, the S3 USB-CDC panic dump is lost, but this RTC value survives.
    // stage 2 = crashed inside WireGuard lib begin (crypto in the lwIP task).
    delay(80);   // let USB-CDC reattach so this line is actually seen
    Serial.printf("[wg-crash] prev VPN stage=%u (0=clean,2=died in lib begin/crypto)\n",
                  hw_vpn_last_stage());
    hw_vpn_stage_reset();

    // BLE is unused (Bluetooth + BLE-Keyboard launcher apps removed). Release the
    // BT controller's reserved INTERNAL SRAM back to the heap so LVGL draw
    // buffers can live in fast internal RAM instead of PSRAM. Must run before any
    // BT init; bleKeyboard.begin() is never reached now. Harmless if already freed.
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    xSemaphore = xSemaphoreCreateMutex();
    if (xSemaphore == NULL) {
        log_e("Failed to create mutex");
        assert(0);
    }

    sntp_set_time_sync_notification_cb(time_available);

    // Examples of different ways to register wifi events;
    // these handlers will be called from another thread.
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiLostIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

    instance.begin(/*NO_HW_LORA|NO_HW_RTC|NO_HW_GPS|NO_HW_LORA*/);

    // Set the RTC from the PC build time on the first boot of each new upload.
    hw_rtc_sync_build(__DATE__, __TIME__);

    // Power key short-press enters light sleep.
    hw_enable_power_key_toggle();

    beginLvglHelper(instance);

    hw_init();

    // Break any stuck VPN boot-loop: clear the persisted "VPN on" flag at every
    // boot so a crash inside WireGuard begin() can't relaunch itself. The user
    // re-enables the tunnel via the Wireless button each session (opt-in).
    hw_vpn_enabled_set(false);

    // Ensure the alarm always has a playable sound, even if the FAT partition was
    // wiped by a full flash erase or filled by recordings.
    hw_ensure_default_alarm();

    setupGui();

    // Arm agenda reminders in the background (don't need to open the app first).
    ui_nextcloud_reminders_init();

    // Woke from the normal Clock alarm -> show the ringing screen.
    if (hw_woke_from_alarm()) {
        // Disarm first so the normal Clock alarm stays one-shot.
        hw_rtc_clear_alarm();
        ui_alarm_ring();
    }

    // UI-speed work: measuring free INTERNAL SRAM to decide if LVGL draw buffers
    // can move from PSRAM into fast internal RAM. Largest DMA-capable contiguous
    // block is the real constraint for a framebuffer. Compare this number before
    // vs after dropping BLE. (Temporary instrumentation.)
    Serial.printf("[mem] internal free=%u largest=%u dma_largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));

    Serial.println("Start done. run main loop");
}

extern void loopNFCReader();

void loop()
{
    instanceLockTake();
    instance.loop();
#if defined(USING_ST25R3916)
    loopNFCReader();
#endif
    // lv_timer_handler() returns ms until LVGL next needs to run. When an
    // animation/refresh is pending it returns ~0, so sleep just 1 ms and render
    // the next frame promptly instead of a flat 5 ms; when idle, cap the sleep
    // at 5 ms to keep power/scheduler behaviour. This cuts per-frame latency
    // during animations without busy-spinning when nothing is happening.
    uint32_t next_ms = lv_timer_handler();
    instanceLockGive();
    if (next_ms > 5) next_ms = 5;
    if (next_ms < 1) next_ms = 1;
    delay(next_ms);
}

#endif
