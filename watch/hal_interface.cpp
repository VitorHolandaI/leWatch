/**
 * @file      hal_interface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co.,
 * Ltd
 * @date      2025-01-08
 *
 */
#ifdef ARDUINO
#include <LilyGoLib.h>
#endif
// gi
// Include IRremoteESP8266 before hal_interface.h. hal_interface.h imports the
// std namespace globally, which makes IRremoteESP8266's atomic_uint16_t typedef
// ambiguous with std::atomic_uint16_t if these headers are included later.
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
#include <IRac.h>
#include <IRsend.h>
#include <ir_Electra.h>
#include <ir_LG.h>
#include <ir_Midea.h>
#include <ir_Samsung.h>
#include <ir_Toshiba.h>
#endif

#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
#include <IRrecv.h>
#endif

#include "hal_interface.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <lvgl.h>
#include <math.h>

#define NVS_NAME "pager"
static user_setting_params_t user_setting;

typedef struct device_const_var {
  uint16_t max_brightness;
  uint16_t min_brightness;
  uint16_t max_charge_current;
  uint16_t min_charge_current;
  uint8_t charge_level_nums;
  uint8_t charge_steps;
} device_const_var_t;

#ifdef ARDUINO

#include "Esp.h"
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"

#define CONFIG_BLE_KEYBOARD
#include "app_nfc.h"
#include "audio/keyboard_audio.h"
#include "driver/rtc_io.h"
#include <FFat.h>
#include <LilyGoLib.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <cbuf.h>
#include <esp_mac.h>

static Preferences prefs;
static TaskHandle_t recTaskHandle;
static TaskHandle_t playerTaskHandler = NULL;
static QueueHandle_t playerQueue = NULL;
static EventGroupHandle_t playerEvent = NULL;
static EventGroupHandle_t recordEvent = NULL;
static SemaphoreHandle_t audioMutex = NULL;
static bool micHasAudioLock = false;
static std::atomic<uint32_t> playerRequestId{0};

#define PLAYER_PLAY _BV(0)
#define PLAYER_END _BV(1)
#define PLAYER_RUNNING _BV(2)
#define PLAYER_PENDING _BV(3)
#define RECORD_ACTIVE _BV(0)
#define RECORD_STOP _BV(1)
#define RECORD_DONE _BV(2)

#if defined(HAS_SD_CARD_SOCKET)
#define FILESYSTEM SD
#else
#define FILESYSTEM FFat
#endif

#if defined(USING_BLE_KEYBOARD)
#include <BleKeyboard.h>
BleKeyboard bleKeyboard;
#endif

#endif

static device_const_var_t dev_conts_var = {
    .max_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL,
    .min_brightness = DEVICE_MIN_BRIGHTNESS_LEVEL,
    .max_charge_current = DEVICE_MAX_CHARGE_CURRENT,
    .min_charge_current = DEVICE_MIN_CHARGE_CURRENT,
    .charge_level_nums = DEVICE_CHARGE_LEVEL_NUMS,
    .charge_steps = DEVICE_CHARGE_STEPS,
};

static const uint16_t k_pmu_charge_table[] = {100, 125, 150, 175, 200, 300, 400,
                                              500, 600, 700, 800, 900, 1000};

static uint16_t hw_safe_max_charger_current() {
  return USER_MAX_CHARGE_CURRENT_MA < dev_conts_var.max_charge_current
             ? USER_MAX_CHARGE_CURRENT_MA
             : dev_conts_var.max_charge_current;
}

static uint16_t hw_clamp_charger_current(uint16_t milliampere) {
  uint16_t max_current = hw_safe_max_charger_current();
  if (milliampere > max_current) {
    return max_current;
  }
  if (milliampere < dev_conts_var.min_charge_current) {
    return dev_conts_var.min_charge_current;
  }
  return milliampere;
}

static uint8_t hw_pmu_max_charge_level() {
  uint16_t max_current = hw_safe_max_charger_current();
  uint8_t max_level = 0;
  for (size_t i = 0;
       i < sizeof(k_pmu_charge_table) / sizeof(k_pmu_charge_table[0]); ++i) {
    if (k_pmu_charge_table[i] <= max_current) {
      max_level = (uint8_t)i;
    }
  }
  return max_level;
}

static const char *hw_devices[] = {
    USING_RADIO_NAME,

#ifdef USING_INPUT_DEV_TOUCHPAD
    "Touch Panel",
#else
    "",
#endif
    "Haptic Drive",
    "Power management",
    "Real-time clock",
    "PSRAM",
    "", // slot 6 was GPS (not present on T-Watch S3)
#ifdef HAS_SD_CARD_SOCKET
    "SD card",
#else
    "",
#endif
#ifdef USING_ST25R3916
    "NFC",
#else
    "",
#endif

#ifdef USING_BHI260_SENSOR
    "BHI260AP 6-Axis Sensor",
#else
    "",
#endif
#ifdef USING_INPUT_DEV_KEYBOARD
    "Keyboard",
#else
    "",
#endif

#ifdef USING_BQ_GAUGE
    "Gauge",
#else
    "",
#endif

#ifdef USING_XL9555_EXPANDS
    "Expands Control",
#else
    "",
#endif

#ifdef USING_AUDIO_CODEC
    "Audio codec",
#else
    "",
#endif

#ifdef USING_EXTERN_NRF2401
    "NRF2401 Sub 1G",
#else
    "",
#endif

#ifdef USING_SI473X_RADIO
    "SI4735 Radio",
#else
    "",
#endif

#ifdef USING_BME280
    "BME280 Pressure & Temperature",
#else
    "",
#endif

#ifdef USING_MAG_QMC5883
    "QMC5883P Magnetometer",
#else
    "",
#endif

#ifdef USING_BMA423_SENSOR
    "BMA423 Accelerometer",
#else
    "",
#endif

#ifdef USING_QMI8658_SENSOR
    "QMI8658 6-Axis Sensor",
#else
    "",
#endif

};

#if defined(USING_ST25R3916) && defined(ARDUINO)
static void nrf_notify_callback();
static void ndef_event_callback(ndefTypeId id, void *data);
#endif

extern void hw_nrf24_begin();
extern void hw_radio_begin();

#ifndef ARDUINO
int random(int min, int max) {
  if (min > max) {
    int temp = min;
    min = max;
    max = temp;
  }
  int range = max - min + 1;
  // sim-only Arduino random() shim; rand() randomness is adequate for the
  // simulator NOLINTNEXTLINE(cert-msc30-c,cert-msc50-cpp)
  return rand() % range + min;
}
#endif

#ifdef ARDUINO

size_t getArduinoLoopTaskStackSize(void) { return 30 * 1024; }

#include <mp3dec.h>

static size_t mp3_frame_bytes(const MP3FrameInfo &frameInfo) {
  return (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps);
}

static void mp3_start_output(const MP3FrameInfo &frameInfo) {
#if defined(USING_PCM_AMPLIFIER)
  instance.powerControl(POWER_SPEAK, true);
  log_d("Start PCM Play...");
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
  printf("sample rate:%d bitPs:%d ch:%d\n", frameInfo.samprate,
         frameInfo.bitsPerSample, (i2s_channel_t)frameInfo.nChans);
  instance.player.configureTX(frameInfo.samprate, frameInfo.bitsPerSample,
                              (i2s_channel_t)frameInfo.nChans);
#else
  instance.player.configureTX(frameInfo.samprate,
                              (i2s_data_bit_width_t)frameInfo.bitsPerSample,
                              (i2s_slot_mode_t)frameInfo.nChans);
#endif
#elif defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.open(frameInfo.bitsPerSample, frameInfo.nChans,
                        frameInfo.samprate);
  }
#else
  (void)frameInfo;
#endif
}

// Software playback gain. Stays 1 (unity) for music/key-clicks/recordings; the
// alarm bumps it (hw_set_audio_gain) because this board's MAX98357A amplifier
// has a fixed hardware gain and no codec volume register to turn up.
static uint8_t s_pcm_gain = 1;

// Scale a 16-bit PCM buffer in place, clamping to int16 range. Louder alarm at
// the cost of clipping/distortion when s_pcm_gain > 1.
static void apply_pcm_gain(int16_t *buf, size_t bytes) {
  if (s_pcm_gain <= 1) {
    return;
  }
  size_t n = bytes / 2;
  for (size_t i = 0; i < n; i++) {
    int32_t v = (int32_t)buf[i] * s_pcm_gain;
    if (v > 32767) {
      v = 32767;
    } else if (v < -32768) {
      v = -32768;
    }
    buf[i] = (int16_t)v;
  }
}

static void mp3_write_output(int16_t *outBuf, const MP3FrameInfo &frameInfo) {
#if defined(USING_PCM_AMPLIFIER)
  apply_pcm_gain(outBuf, mp3_frame_bytes(frameInfo));
  instance.player.write((uint8_t *)outBuf, mp3_frame_bytes(frameInfo));
#elif defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    int ret =
        instance.codec.write((uint8_t *)outBuf, mp3_frame_bytes(frameInfo));
    if (ret != 0) {
      Serial.printf("esp_codec_dev_write:0x%X\n", ret);
    }
  }
#else
  (void)outBuf;
  (void)frameInfo;
#endif
}

static void mp3_stop_output() {
#if defined(USING_PCM_AMPLIFIER)
  instance.powerControl(POWER_SPEAK, false);
#elif defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.close();
  }
#endif
}

static void mp3_finish_decoder(HMP3Decoder decoder, bool output_started) {
  MP3FreeDecoder(decoder);
  if (output_started) {
    mp3_stop_output();
  }
}

static void mp3_output_frame(int16_t *outBuf, const MP3FrameInfo &frameInfo,
                             bool &output_started) {
  if (!output_started) {
    output_started = true;
    mp3_start_output(frameInfo);
  }
  mp3_write_output(outBuf, frameInfo);
}

struct mp3_cursor_t {
  uint8_t *ptr;
  int bytes;
};

static bool mp3_recover_decode_error(int err, const mp3_cursor_t &before,
                                     mp3_cursor_t &current) {
  log_e("Decode ERROR: %d", err);
  if (err == ERR_MP3_INDATA_UNDERFLOW) {
    return false;
  }

  if (current.ptr != before.ptr || current.bytes != before.bytes) {
    return true;
  }
  if (current.bytes <= 0) {
    return false;
  }
  current.ptr++;
  current.bytes--;
  return true;
}

static bool mp3_wait_for_continue() {
  EventBits_t eventBits = xEventGroupWaitBits(
      playerEvent, PLAYER_PLAY | PLAYER_END, pdFALSE, pdFALSE, portMAX_DELAY);
  return !(eventBits & PLAYER_END);
}

static bool playMP3(uint8_t *src, size_t src_len) {
  int16_t outBuf[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
  uint8_t *readPtr = NULL;
  int bytesAvailable = 0, err = 0, offset = 0;
  MP3FrameInfo frameInfo;
  HMP3Decoder decoder = NULL;
  bool output_started = false;
  mp3_cursor_t cursor;

  cursor.bytes = src_len;
  cursor.ptr = src;

  decoder = MP3InitDecoder();
  if (decoder == NULL) {
    log_e("Could not allocate decoder");
    return false;
  }
  while (cursor.bytes > 1) {
    offset = MP3FindSyncWord(cursor.ptr, cursor.bytes);
    if (offset < 0) {
      break;
    }
    cursor.ptr += offset;
    cursor.bytes -= offset;

    mp3_cursor_t before = cursor;
    readPtr = cursor.ptr;
    bytesAvailable = cursor.bytes;
    err = MP3Decode(decoder, &readPtr, &bytesAvailable, outBuf, 0);
    cursor.ptr = readPtr;
    cursor.bytes = bytesAvailable;
    if (err) {
      if (mp3_recover_decode_error(err, before, cursor)) {
        continue;
      }
      break;
    }

    MP3GetLastFrameInfo(decoder, &frameInfo);
    mp3_output_frame(outBuf, frameInfo, output_started);

    if (!mp3_wait_for_continue()) {
      // printf("TASK END\n");
      break;
    }
  }

  mp3_finish_decoder(decoder, output_started);
  return true;
}

// Play a PCM WAV blob through the board's audio output. Honors the same
// PLAYER_RUNNING/PLAYER_END plumbing as playMP3 so hw_set_play_stop() and
// hw_player_running() behave identically for recordings.
static bool playWAV(uint8_t *src, size_t src_len) {
  if (src_len <= 44 || memcmp(src, "RIFF", 4) != 0 ||
      memcmp(src + 8, "WAVE", 4) != 0) {
    return false;
  }
  uint16_t channels = (uint16_t)(src[22] | (src[23] << 8));
  uint32_t rate = (uint32_t)src[24] | ((uint32_t)src[25] << 8) |
                  ((uint32_t)src[26] << 16) | ((uint32_t)src[27] << 24);
  uint16_t bits = (uint16_t)(src[34] | (src[35] << 8));
  uint32_t declaredDataLen = (uint32_t)src[40] | ((uint32_t)src[41] << 8) |
                             ((uint32_t)src[42] << 16) |
                             ((uint32_t)src[43] << 24);
  if ((src[20] | (src[21] << 8)) != 1 || channels < 1 || channels > 2 ||
      bits != 16 || rate == 0 || declaredDataLen > src_len - 44) {
    return false;
  }

  uint8_t *data = src + 44;
  size_t dataLen = declaredDataLen;

#if defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    if (instance.codec.open(bits, channels, rate) < 0) {
      return false;
    }
  }
#elif defined(USING_PCM_AMPLIFIER)
  instance.powerControl(POWER_SPEAK, true);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  if (!instance.player.configureTX(rate, (i2s_data_bit_width_t)bits,
                                   (i2s_slot_mode_t)channels)) {
#else
  if (!instance.player.configureTX(rate, bits, (i2s_channel_t)channels)) {
#endif
    instance.powerControl(POWER_SPEAK, false);
    return false;
  }
#endif

  const size_t CHUNK = 2048;
  size_t off = 0;
  bool ok = true;
  while (off < dataLen) {
    size_t n = (dataLen - off < CHUNK) ? (dataLen - off) : CHUNK;
#if defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
      if (instance.codec.write(data + off, n) != 0) {
        ok = false;
        break;
      }
    }
#elif defined(USING_PCM_AMPLIFIER)
    apply_pcm_gain((int16_t *)(data + off), n);
    if (instance.player.write(data + off, n) != n) {
      ok = false;
      break;
    }
#endif
    off += n;
    if (!mp3_wait_for_continue()) {
      break; // stop requested via PLAYER_END
    }
  }

#if defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.close();
  }
#elif defined(USING_PCM_AMPLIFIER)
  instance.powerControl(POWER_SPEAK, false);
#endif
  return ok;
}

static void hw_close_audio_file(File &f, bool locked) {
  f.close();
  if (locked) {
    instance.unlockSPI();
  }
}

static bool hw_open_audio_file(audio_source_type_t source, const String &path,
                               const char *filename, File &f, bool &locked) {
  locked = false;
  if (source == AUDIO_SOURCE_SDCARD) {
    Serial.printf("Open from SD: %s\n", path.c_str());
    // T-Watch-S3-Ultra or T-LoRa-Pager share the SPI bus with SD.
    instance.lockSPI();
    f = SD.open(path);
    if (f) {
      locked = true;
      return true;
    }
    Serial.printf("SD Open %s failed!\n", filename);
    instance.unlockSPI();
    return false;
  }

  Serial.printf("Open from FFat: %s\n", path.c_str());
  f = FFat.open(path);
  if (!f) {
    Serial.printf("FFat Open %s failed!\n", filename);
    return false;
  }
  return true;
}

static uint8_t *hw_read_audio_file(File &f, const char *filename,
                                   size_t &read_size) {
  read_size = 0;
  size_t file_size = f.size();
  if (file_size == 0) {
    Serial.printf("File %s size is 0!\n", filename);
    return NULL;
  }

  uint8_t *buf = (uint8_t *)ps_malloc(file_size);
  if (!buf) {
    Serial.println("ps malloc failed!");
    return NULL;
  }

  read_size = f.readBytes((char *)buf, file_size);
  if (read_size != file_size) {
    free(buf);
    return NULL;
  }
  return buf;
}

static void hw_sd_play(audio_source_type_t source, const char *filename) {
  bool isMP3 = String(filename).endsWith(".mp3");
  bool isWAV = String(filename).endsWith(".wav");
  bool lock = false;

  String str = "/" + String(filename);
  File f;

  // Serial.printf("Playing file: %s source:%d\n", str.c_str(), source);
  if (!hw_open_audio_file(source, str, filename, f, lock)) {
    return;
  }

  size_t read_size = 0;
  uint8_t *buf = hw_read_audio_file(f, filename, read_size);
  hw_close_audio_file(f, lock);
  if (!buf)
    return;

  Serial.print("Playing ");
  Serial.println(filename);
  if (isMP3) {
    playMP3(buf, read_size);
  } else if (isWAV) {
    playWAV(buf, read_size);
  }
  Serial.println("Play done..");
  free(buf);
}

static void playerTask(void *args) {
  audio_params_t params;
  while (1) {
    if (xQueueReceive(playerQueue, &params, portMAX_DELAY) != pdPASS) {
      continue;
    }
    xEventGroupClearBits(playerEvent, PLAYER_PENDING);
    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);
    if (params.request_id != playerRequestId.load() ||
        (xEventGroupGetBits(playerEvent) & PLAYER_END)) {
      xEventGroupClearBits(playerEvent,
                           PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
      continue;
    }
    if (xSemaphoreTake(audioMutex, portMAX_DELAY) != pdTRUE) {
      xEventGroupClearBits(playerEvent,
                           PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
      continue;
    }
    // A recorder may have claimed audio while this request was queued.
    if (params.request_id != playerRequestId.load() || hw_record_active() ||
        (xEventGroupGetBits(playerEvent) & PLAYER_END)) {
      xSemaphoreGive(audioMutex);
      xEventGroupClearBits(playerEvent,
                           PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
      continue;
    }
    switch (params.event) {
    case APP_EVENT_PLAY:
      Serial.printf("Event: filename:%s source:%d\n", params.filename,
                    params.source_type);
      hw_sd_play(params.source_type, params.filename);
      break;
    case APP_EVENT_PLAY_KEY:
      // Serial.println("APP_EVENT_PLAY_KEY");
      playMP3((uint8_t *)keyboard_audio, keyboard_audio_mp3_len);
      break;
    case APP_EVENT_RECOVER:
      break;
    default:
      break;
    }
    xSemaphoreGive(audioMutex);
    xEventGroupClearBits(playerEvent,
                         PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
  }
  playerTaskHandler = NULL;
  vTaskDelete(NULL);
}

#endif

#ifdef ARDUINO

static int16_t i2s_buffer[FFT_SIZE * 2];
static float fft_input[FFT_SIZE * 2] __attribute__((aligned(16)));
static float window[FFT_SIZE] __attribute__((aligned(16)));
static int16_t left_channel[FFT_SIZE];
static int16_t right_channel[FFT_SIZE];
static int read_count = 0;

static void process_channel_fft(int16_t *channel_data, float *bands,
                                float freq_per_bin) {
  for (int i = 0; i < FFT_SIZE; i++) {
    fft_input[2 * i] = (float)channel_data[i] * 3.0f / 32768.0f * window[i];
    fft_input[2 * i + 1] = 0;
  }

  dsps_fft2r_fc32_aes3(fft_input, FFT_SIZE);
  dsps_bit_rev_fc32(fft_input, FFT_SIZE);
  dsps_cplx2reC_fc32(fft_input, FFT_SIZE);

  float magnitudes[FFT_SIZE / 2];
  for (int i = 0; i < FFT_SIZE / 2; i++) {
    float real = fft_input[2 * i];
    float imag = fft_input[2 * i + 1];
    magnitudes[i] = sqrt(real * real + imag * imag);

    if (magnitudes[i] < 0.00001)
      magnitudes[i] = 0.00001;
    magnitudes[i] = 20 * log10(magnitudes[i]);
    magnitudes[i] = (magnitudes[i] + 40) / 40;
    magnitudes[i] = constrain(magnitudes[i], 0, 1);
  }

  int bin_count = (FFT_SIZE / 2) / FREQ_BANDS;
  memset(bands, 0, FREQ_BANDS * sizeof(float));

  for (int band = 0; band < FREQ_BANDS; band++) {
    int start_bin = band * bin_count;
    int end_bin = start_bin + bin_count;
    if (end_bin > FFT_SIZE / 2)
      end_bin = FFT_SIZE / 2;

    float sum = 0;
    int count = 0;
    for (int bin = start_bin; bin < end_bin; bin++) {
      sum += magnitudes[bin];
      count++;
    }

    if (count > 0) {
      bands[band] = sum / count;
    }
  }
}

#endif /*ARDUINO*/

void hw_audio_get_fft_data(FFTData *fft_data) {
#ifdef ARDUINO
  float freq_per_bin = (float)SAMPLE_RATE / FFT_SIZE;

#if defined(USING_PDM_MICROPHONE)
  int32_t pdm_sample;
  instance.mic.readBytes((char *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
#elif defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.read((uint8_t *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
  } else {
    return;
  }
#endif

  read_count++;
  if (read_count % 10 == 0) {
    Serial.printf("Left: %d, Right: %d\n", i2s_buffer[0], i2s_buffer[1]);
  }

  for (int i = 0; i < FFT_SIZE; i++) {
    left_channel[i] = i2s_buffer[2 * i];
    right_channel[i] = i2s_buffer[2 * i + 1];
  }

  process_channel_fft(left_channel, fft_data->left_bands, freq_per_bin);
  process_channel_fft(right_channel, fft_data->right_bands, freq_per_bin);
#endif /*ARDUINO*/
}

bool hw_set_mic_start() {
#ifdef ARDUINO
  int ret;
  if (hw_record_active() || hw_player_running() || !audioMutex ||
      xSemaphoreTake(audioMutex, 0) != pdTRUE) {
    return false;
  }
  micHasAudioLock = true;

#ifdef USING_AUDIO_CODEC
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    ret = instance.codec.open(16, instance.getCodecInputChannels(), 16000);
    if (ret < 0) {
      log_e("Audio codec open failed:0x%X", ret);
      xSemaphoreGive(audioMutex);
      micHasAudioLock = false;
      return false;
    }
  } else {
    xSemaphoreGive(audioMutex);
    micHasAudioLock = false;
    return false;
  }
#endif /*USING_AUDIO_CODEC*/

  ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
  if (ret != ESP_OK) {
    log_e("fft init failed = %i\n", ret);
#ifdef USING_AUDIO_CODEC
    instance.codec.close();
#endif
    xSemaphoreGive(audioMutex);
    micHasAudioLock = false;
    return false;
  }

  dsps_wind_hann_f32(window, FFT_SIZE);

#endif /*ARDUINO*/

  return true;
}

void hw_set_mic_stop() {
#ifdef ARDUINO
  if (!micHasAudioLock)
    return;
#ifdef USING_AUDIO_CODEC
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.close();
  }
#endif
  dsps_fft2r_deinit_fc32();
  xSemaphoreGive(audioMutex);
  micHasAudioLock = false;
#endif /*ARDUINO*/
}

// --- voice recorder -------------------------------------------------------
// Captures 16 kHz/16-bit mono WAV files into FFat. Replay reuses the MP3
// player pipeline (hw_set_sd_music_play -> hw_sd_play WAV branch), so the
// same play/stop/running plumbing applies. Up to REC_MAX_FILES are kept; the
// oldest is dropped when a new one is started.

#define REC_MAX_FILES 10
#define REC_SAMPLE_RATE 16000
#define REC_DIR_PREFIX "/rec_"
#define REC_SUFFIX ".wav"
#define REC_MIN_FREE_BYTES (64 * 1024) // stop before the partition is full

#if defined(ARDUINO)
static TaskHandle_t recTaskHandler = NULL;
static volatile uint32_t recStartMs = 0;
static volatile hw_record_result_t recResult = HW_RECORD_RESULT_NONE;
static String recPath;

// Build a 44-byte canonical PCM WAV header for a mono 16-bit stream.
static void hw_wav_fill_header(uint8_t *h, uint32_t dataLen) {
  const uint16_t channels = 1;
  const uint16_t bits = 16;
  const uint32_t rate = REC_SAMPLE_RATE;
  const uint32_t byteRate = rate * channels * (bits / 8);
  const uint16_t blockAlign = channels * (bits / 8);
  const uint32_t fmtLen = 16;
  const uint16_t pcm = 1;
  const uint32_t riffLen = 36 + dataLen;
  memcpy(h + 0, "RIFF", 4);
  memcpy(h + 4, &riffLen, 4);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  memcpy(h + 16, &fmtLen, 4);
  memcpy(h + 20, &pcm, 2);
  memcpy(h + 22, &channels, 2);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &byteRate, 4);
  memcpy(h + 32, &blockAlign, 2);
  memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &dataLen, 4);
}

static bool hw_is_recording_file(const char *name) {
  // Names look like "rec_<seq>.wav"; match the prefix without the leading '/'.
  return strncmp(name, REC_DIR_PREFIX + 1, strlen(REC_DIR_PREFIX) - 1) == 0;
}

// Collect recording filenames (with leading '/'), sorted ascending. The
// sequence number is zero-padded so lexical order == chronological order.
static void hw_record_collect(std::vector<std::string> &out) {
  out.clear();
  File dir = FFat.open("/");
  if (!dir)
    return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *n = f.name();
    // f.name() may or may not carry a leading '/'; normalise to the bare name.
    const char *bare = (n[0] == '/') ? n + 1 : n;
    if (!f.isDirectory() && hw_is_recording_file(bare)) {
      out.push_back(std::string("/") + bare);
    }
    f.close();
  }
  dir.close();
  std::sort(out.begin(), out.end());
}

// Next zero-padded sequence path, one past the highest existing recording.
static String hw_record_next_path() {
  std::vector<std::string> files;
  hw_record_collect(files);
  uint32_t seq = 0;
  if (!files.empty()) {
    const std::string &last = files.back();
    seq =
        (uint32_t)strtoul(last.c_str() + strlen(REC_DIR_PREFIX), NULL, 10) + 1;
  }
  char buf[40];
  snprintf(buf, sizeof(buf), REC_DIR_PREFIX "%010u" REC_SUFFIX, (unsigned)seq);
  return String(buf);
}

// Once a new recording contains audio, drop old files until the cap is met.
static bool hw_record_enforce_cap() {
  std::vector<std::string> files;
  hw_record_collect(files);
  for (size_t i = 0; i < files.size() && files.size() - i > REC_MAX_FILES;
       i++) {
    if (!FFat.remove(files[i].c_str()))
      return false;
  }
  return true;
}

static void recordTask(void *args) {
  (void)args;
#if defined(USING_AUDIO_CODEC)
  const int inCh = instance.getCodecInputChannels() < 1
                       ? 1
                       : instance.getCodecInputChannels();
#else
  const int inCh = 1;
#endif
  const size_t FRAMES = 512;
  int16_t *in = (int16_t *)malloc(FRAMES * inCh * sizeof(int16_t));
  int16_t *mono = (int16_t *)malloc(FRAMES * sizeof(int16_t));
  File f;
  bool opened = false;
  bool audioLocked = false;
  bool keepFile = false;
  hw_record_result_t result = HW_RECORD_RESULT_IO_ERROR;
  uint32_t dataLen = 0;

  if (!in || !mono) {
    goto cleanup;
  }
  if (!audioMutex || xSemaphoreTake(audioMutex, 0) != pdTRUE) {
    result = HW_RECORD_RESULT_CODEC_ERROR;
    goto cleanup;
  }
  audioLocked = true;
#ifdef USING_AUDIO_CODEC
  if (!(HW_CODEC_ONLINE & hw_get_device_online()) ||
      instance.codec.open(16, inCh, REC_SAMPLE_RATE) < 0) {
    result = HW_RECORD_RESULT_CODEC_ERROR;
    goto cleanup;
  }
  opened = true;
#endif
  f = FFat.open(recPath, FILE_WRITE);
  if (!f) {
    result = HW_RECORD_RESULT_FILE_ERROR;
    goto cleanup;
  }
  {
    uint8_t hdr[44] = {0};
    if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
      result = HW_RECORD_RESULT_FILE_ERROR;
      f.close();
      FFat.remove(recPath);
      goto cleanup;
    }

    result = HW_RECORD_RESULT_OK;
    bool capApplied = false;
    while (!(xEventGroupGetBits(recordEvent) & RECORD_STOP)) {
      size_t capturedBytes = 0;
#if defined(USING_PDM_MICROPHONE)
      capturedBytes =
          instance.mic.readBytes((char *)mono, FRAMES * sizeof(int16_t));
      if (capturedBytes == 0) {
        result = HW_RECORD_RESULT_CODEC_ERROR;
        break;
      }
#elif defined(USING_AUDIO_CODEC)
      if (HW_CODEC_ONLINE & hw_get_device_online()) {
        if (instance.codec.read((uint8_t *)in,
                                FRAMES * inCh * sizeof(int16_t)) != 0) {
          result = HW_RECORD_RESULT_CODEC_ERROR;
          break;
        }
        for (size_t i = 0; i < FRAMES; i++) {
          mono[i] = in[i * inCh];
        }
        capturedBytes = FRAMES * sizeof(int16_t);
      }
#else
      result = HW_RECORD_RESULT_CODEC_ERROR;
      break;
#endif
      size_t written = f.write((uint8_t *)mono, capturedBytes);
      dataLen += written;
      if (written != capturedBytes) {
        result = HW_RECORD_RESULT_IO_ERROR;
        break;
      }
      if (!capApplied) {
        if (!hw_record_enforce_cap()) {
          result = HW_RECORD_RESULT_IO_ERROR;
          break;
        }
        capApplied = true;
      }
      if (FFat.freeBytes() < REC_MIN_FREE_BYTES) {
        result = HW_RECORD_RESULT_LIMIT_REACHED;
        break; // out of space: finalise what we have
      }
    }

    keepFile = capApplied && dataLen > 0;
    if (keepFile) {
      uint8_t hdr2[44];
      hw_wav_fill_header(hdr2, dataLen);
      if (!f.seek(0) || f.write(hdr2, sizeof(hdr2)) != sizeof(hdr2)) {
        result = HW_RECORD_RESULT_IO_ERROR;
        keepFile = false;
      }
    }
    f.close();
    if (!keepFile)
      FFat.remove(recPath);
  }

cleanup:
  if (in)
    free(in);
  if (mono)
    free(mono);
#ifdef USING_AUDIO_CODEC
  if (opened && (HW_CODEC_ONLINE & hw_get_device_online())) {
    instance.codec.close();
  }
#else
  (void)opened;
#endif
  if (audioLocked)
    xSemaphoreGive(audioMutex);
  recResult = result;
  recTaskHandler = NULL;
  xEventGroupSetBits(recordEvent, RECORD_DONE);
  xEventGroupClearBits(recordEvent, RECORD_ACTIVE);
  vTaskDelete(NULL);
}
#endif /*ARDUINO*/

bool hw_record_start() {
#ifdef ARDUINO
  if ((xEventGroupGetBits(recordEvent) & RECORD_ACTIVE) || micHasAudioLock ||
      hw_player_running())
    return false;
  if (FFat.freeBytes() < REC_MIN_FREE_BYTES) {
    recResult = HW_RECORD_RESULT_NO_SPACE;
    return false;
  }
  recPath = hw_record_next_path();
  recResult = HW_RECORD_RESULT_RECORDING;
  xEventGroupClearBits(recordEvent, RECORD_STOP | RECORD_DONE);
  xEventGroupSetBits(recordEvent, RECORD_ACTIVE);
  recStartMs = millis();
  if (xTaskCreate(recordTask, "app/rec", 8 * 1024, NULL, 11, &recTaskHandler) !=
      pdPASS) {
    recResult = HW_RECORD_RESULT_IO_ERROR;
    xEventGroupClearBits(recordEvent, RECORD_ACTIVE);
    recTaskHandler = NULL;
    return false;
  }
  return true;
#else
  return false;
#endif
}

void hw_record_stop() {
#ifdef ARDUINO
  if (recordEvent && (xEventGroupGetBits(recordEvent) & RECORD_ACTIVE)) {
    xEventGroupSetBits(recordEvent, RECORD_STOP);
    xEventGroupWaitBits(recordEvent, RECORD_DONE, pdTRUE, pdTRUE,
                        portMAX_DELAY);
    while (xEventGroupGetBits(recordEvent) & RECORD_ACTIVE)
      delay(1);
  }
#endif
}

bool hw_record_active() {
#ifdef ARDUINO
  return recordEvent && (xEventGroupGetBits(recordEvent) & RECORD_ACTIVE);
#else
  return false;
#endif
}

hw_record_result_t hw_record_last_result() {
#ifdef ARDUINO
  return recResult;
#else
  return HW_RECORD_RESULT_NONE;
#endif
}

uint32_t hw_record_elapsed_ms() {
#ifdef ARDUINO
  return hw_record_active() ? (millis() - recStartMs) : 0;
#else
  return 0;
#endif
}

size_t hw_record_list(std::vector<std::string> &out) {
#ifdef ARDUINO
  hw_record_collect(out);
  return out.size();
#else
  out.clear();
  return 0;
#endif
}

bool hw_record_delete(const char *path) {
#ifdef ARDUINO
  return path && FFat.remove(path);
#else
  (void)path;
  return false;
#endif
}

#if defined(USING_ST25R3916) && defined(ARDUINO)

extern void ui_nfc_pop_up(wifi_conn_params_t &params);

static void nrf_notify_callback() {
  Serial.println("NDEF Detected.");
  hw_feedback();
}

static void ndef_event_callback(ndefTypeId id, void *data) {
  static ndefTypeRtdDeviceInfo devInfoData;
  static ndefConstBuffer bufAarString;
  static ndefRtdUri url;
  static ndefRtdText text;
  static String msg = "";
  static wifi_conn_params_t params;
  msg = "";
  switch (id) {
  case NDEF_TYPE_EMPTY:
    break;
  case NDEF_TYPE_RTD_DEVICE_INFO:
    memcpy(&devInfoData, data, sizeof(ndefTypeRtdDeviceInfo));
    break;
  case NDEF_TYPE_RTD_TEXT:
    memcpy(&text, data, sizeof(ndefRtdText));
    Serial.printf("LanguageCode: %s\nSentence: %s\n",
                  reinterpret_cast<const char *>(text.bufLanguageCode.buffer),
                  reinterpret_cast<const char *>(text.bufSentence.buffer));
    msg.concat("LanguageCode: ");
    msg.concat(reinterpret_cast<const char *>(text.bufLanguageCode.buffer));
    msg.concat("\nSentence: ");
    msg.concat(reinterpret_cast<const char *>(text.bufSentence.buffer));
    ui_msg_pop_up("NFC Text", msg.c_str());
    break;
  case NDEF_TYPE_RTD_URI:
    memcpy(&url, data, sizeof(ndefRtdUri));
    Serial.printf("PROTOCOL:%s URL:%s\n",
                  reinterpret_cast<const char *>(url.bufProtocol.buffer),
                  reinterpret_cast<const char *>(url.bufUriString.buffer));
    msg.concat("PROTOCOL:");
    msg.concat(reinterpret_cast<const char *>(url.bufProtocol.buffer));
    msg.concat("URL:");
    msg.concat(reinterpret_cast<const char *>(url.bufUriString.buffer));
    ui_msg_pop_up("NFC Url", msg.c_str());
    break;
  case NDEF_TYPE_RTD_AAR:
    memcpy(&bufAarString, data, sizeof(ndefConstBuffer));
    Serial.printf("NDEF_TYPE_RTD_AAR :%s\n", (char *)bufAarString.buffer);
    break;
  case NDEF_TYPE_MEDIA:
    break;
  case NDEF_TYPE_MEDIA_VCARD:
    break;
  case NDEF_TYPE_MEDIA_WIFI: {
    ndefTypeWifi *wifi = (ndefTypeWifi *)data;
    params.ssid =
        std::string(reinterpret_cast<const char *>(wifi->bufNetworkSSID.buffer),
                    wifi->bufNetworkSSID.length);
    params.password =
        std::string(reinterpret_cast<const char *>(wifi->bufNetworkKey.buffer),
                    wifi->bufNetworkKey.length);
    Serial.printf("ssid:<%s> password:<%s>\n", params.ssid.c_str(),
                  params.password.c_str());
    ui_nfc_pop_up(params);
  } break;
  default:
    break;
  }
}
#endif /*USING_ST25R3916*/

bool hw_start_nfc_discovery() {
#if defined(USING_ST25R3916) && defined(ARDUINO)
  instance.powerControl(POWER_NFC, true);
  return beginNFC(nrf_notify_callback, ndef_event_callback);
#else
  return false;
#endif
}

void hw_stop_nfc_discovery() {
#if defined(USING_ST25R3916) && defined(ARDUINO)
  deinitNFC();
  instance.powerControl(POWER_NFC, false);
#endif
}

#ifdef ARDUINO_T_LORA_PAGER
const uint8_t mic_gain = 10;
#else
const uint8_t mic_gain = 10;
#endif

#ifdef ARDUINO
static void hw_init_player() {
  playerQueue = xQueueCreate(2, sizeof(audio_params_t));
  playerEvent = xEventGroupCreate();
  recordEvent = xEventGroupCreate();
  audioMutex = xSemaphoreCreateMutex();
  xTaskCreate(playerTask, "app/play", 8 * 1024, NULL, 12, &playerTaskHandler);
}

static void hw_init_radios() {
  hw_radio_begin();
#ifdef USING_EXTERN_NRF2401
  hw_nrf24_begin();
#endif
}

static void hw_init_audio_codec() {
#ifdef USING_AUDIO_CODEC
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.setVolume(100);
    instance.codec.setGain(mic_gain);
  } else {
    log_w("Audio codec not online!");
  }
#endif // USING_AUDIO_CODEC
}

static void hw_play_keyboard_feedback() {
  if (hw_record_active())
    return;
  hw_set_play_stop();
  audio_params_t params = {};
  params.event = APP_EVENT_PLAY_KEY;
  params.request_id = ++playerRequestId;
  xEventGroupSetBits(playerEvent, PLAYER_PLAY | PLAYER_PENDING);
  if (xQueueSend(playerQueue, &params, portMAX_DELAY) != pdPASS) {
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_PENDING);
  }
}

static void hw_feedback_callback(void *args) {
  lv_indev_t *drv = (lv_indev_t *)args;
  instance.vibrator();

  if (lv_indev_get_type(drv) == LV_INDEV_TYPE_KEYPAD) {
    hw_play_keyboard_feedback();
  }
}

static void hw_init_keyboard_feedback() {
#ifdef USING_INPUT_DEV_KEYBOARD
  instance.attachKeyboardFeedback(true, 80);
  instance.setFeedbackCallback(hw_feedback_callback);
#endif // USING_INPUT_DEV_KEYBOARD
}

static void hw_set_default_user_settings(uint8_t brightness,
                                         uint8_t keyboard_bl, uint8_t timeout,
                                         uint16_t charger_current) {
  user_setting.brightness_level = brightness;
  user_setting.keyboard_bl_level = keyboard_bl;
  user_setting.disp_timeout_second = timeout;
  user_setting.charger_current = hw_clamp_charger_current(charger_current);
  user_setting.charger_enable = true;
}

static void hw_load_user_settings() {
  if (!prefs.begin(NVS_NAME)) {
    log_e("Failed to open user settings NVS");
    hw_set_default_user_settings(50, 80, 30, DEVICE_CHARGE_CURRENT_RECOMMEND);
    user_setting.charger_current =
        hw_clamp_charger_current(hw_get_charger_current());
    return;
  }
  if (prefs.getBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) !=
      sizeof(user_setting_params_t)) {
    log_e("Data is not correct size!,set default setting");
    hw_set_default_user_settings(50, 80, 30, DEVICE_CHARGE_CURRENT_RECOMMEND);
    if (prefs.putBytes(NVS_NAME, &user_setting,
                       sizeof(user_setting_params_t)) !=
        sizeof(user_setting_params_t)) {
      log_e("Failed to save default user settings");
    }
  }
  uint16_t saved_current = user_setting.charger_current;
  user_setting.charger_current = hw_clamp_charger_current(saved_current);
  if (user_setting.charger_current != saved_current &&
      prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) !=
          sizeof(user_setting_params_t)) {
    log_e("Failed to save clamped charger current");
  }
}

static void hw_apply_user_settings() {
  hw_set_disp_backlight(user_setting.brightness_level);
  hw_set_kb_backlight(user_setting.keyboard_bl_level);
  hw_set_charger_current(user_setting.charger_current);
  hw_set_charger(user_setting.charger_enable);
}

static void hw_register_power_event_log() {
  instance.onEvent(
      [](DeviceEvent_t event, void *params, void *user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
          log_d("ON EVENT PMU CLICK");
        }
      },
      POWER_EVENT, NULL);
}
#else
static void hw_set_default_user_settings(uint8_t brightness,
                                         uint8_t keyboard_bl, uint8_t timeout,
                                         uint16_t charger_current) {
  user_setting.brightness_level = brightness;
  user_setting.keyboard_bl_level = keyboard_bl;
  user_setting.disp_timeout_second = timeout;
  user_setting.charger_current = hw_clamp_charger_current(charger_current);
  user_setting.charger_enable = true;
}
#endif

void hw_init() {
#ifdef ARDUINO
  hw_init_player();
  hw_init_radios();
  hw_init_audio_codec();
  hw_init_keyboard_feedback();
  hw_load_user_settings();
  hw_apply_user_settings();
  hw_register_power_event_log();
#else
  hw_set_default_user_settings(10, 255, 30, 1000);
#endif

  // #if  defined(USING_ST25R3916) && defined(ARDUINO)
  //     beginNFC(nrf_notify_callback, ndef_event_callback);
  // #endif
}

void hw_get_user_setting(user_setting_params_t &param) {
  param = user_setting;
  printf("Get brightness_level    :%u\n", user_setting.brightness_level);
  printf("Get keyboard_bl_level   :%u\n", user_setting.keyboard_bl_level);
  printf("Get disp_timeout_second :%u\n", user_setting.disp_timeout_second);
  printf("Get charger_current     :%u\n", user_setting.charger_current);
  printf("Get charger_enable      :%u\n", user_setting.charger_enable);
}

void hw_set_user_setting(user_setting_params_t &param) {
  user_setting = param;
  user_setting.charger_current =
      hw_clamp_charger_current(user_setting.charger_current);
#ifdef ARDUINO
  if (prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) !=
      sizeof(user_setting_params_t)) {
    log_e("Failed to save user settings");
  }
#endif
  printf("set brightness_level    :%u\n", param.brightness_level);
  printf("set keyboard_bl_level   :%u\n", param.keyboard_bl_level);
  printf("set disp_timeout_second :%u\n", param.disp_timeout_second);
  printf("set charger_current     :%u\n", param.charger_current);
  printf("set charger_enable      :%u\n", param.charger_enable);
}

const uint32_t hw_get_disp_timeout_ms() {
  return user_setting.disp_timeout_second * 1000UL;
}

uint16_t hw_get_devices_nums() {
  return sizeof(hw_devices) / sizeof(hw_devices[0]);
}

const char *hw_get_devices_name(int index) {
  if (index < 0 || (uint16_t)index >= hw_get_devices_nums()) {
    return "NULL";
  }
  return hw_devices[index];
}

const char *hw_get_variant_name() {
#ifdef ARDUINO
  return instance.getName();
#else
  return "LilyGo T-LoRa-Pager (2025)";
#endif
}

bool hw_get_mac(uint8_t *mac) {
#ifdef ARDUINO
  esp_efuse_mac_get_default(mac);
  return true;
#endif
  return false;
}

void hw_get_wifi_ssid(string &param) {
#ifdef ARDUINO
  param = WiFi.isConnected() ? WiFi.SSID().c_str() : "N.A";
#else
  param = "NO CONFIG";
#endif
}

void hw_get_date_time(string &param) {
#ifdef ARDUINO
  struct tm timeinfo;
  if (hw_get_device_online() & HW_RTC_ONLINE) {
    instance.rtc.getDateTime(&timeinfo);
    char datetime[128] = {0};
    snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    param = datetime;
  } else {
    param = "2000/01/01 00:00:00";
  }
#else
  time_t now;
  struct tm *timeinfo;
  (void)time(&now);
  timeinfo = localtime(&now);
  char datetime[128] = {0};
  (void)snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d",
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                 timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
                 timeinfo->tm_sec);
  param = datetime;
#endif
}

void hw_get_date_time(struct tm &timeinfo) {
#ifdef ARDUINO
  if (hw_get_device_online() & HW_RTC_ONLINE) {
    instance.rtc.getDateTime(&timeinfo);
  } else {
    timeinfo = {0};
  }
#else
  time_t now;
  (void)time(&now);
  timeinfo = *localtime(&now);
#endif
}

wl_status_t hw_get_wifi_status() {
#ifdef ARDUINO
  return WiFi.status();
#else
  return WL_NO_SSID_AVAIL;
#endif
}

void hw_get_ip_address(string &param) {
#ifdef ARDUINO
  if (WiFi.isConnected()) {
    param = WiFi.localIP().toString().c_str();
    return;
  }
#endif
  param = "N.A";
}

int16_t hw_get_wifi_rssi() {
#ifdef ARDUINO
  if (WiFi.isConnected()) {
    return (WiFi.RSSI());
  }
#endif
  return -99;
}

int16_t hw_get_battery_voltage() {
#ifdef ARDUINO

#if defined(USING_BQ_GAUGE)
  if (HW_GAUGE_ONLINE & hw_get_device_online()) {
    instance.gauge.refresh();
    return instance.gauge.getVoltage();
  } else {
    printf("Gauge Not online !\n");
    return 0;
  }
#elif defined(USING_PMU_MANAGE)
  return instance.pmu.getBattVoltage();
#else
  return 0;
#endif

#else
  return 0;
#endif
}

float hw_get_sd_size() {
  float size = 0.0;
#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
  size = SD.cardSize() / 1024 / 1024 / 1024.0;

#elif defined(USING_FATFS)
  size = FFat.totalBytes() / 1024 / 1024;
#endif

#endif
  return size;
}

void hw_get_arduino_version(string &param) {
#ifdef ARDUINO
  param.clear();
  param.append("V");
  param.append(std::to_string(ESP_ARDUINO_VERSION_MAJOR));
  param.append(".");
  param.append(std::to_string(ESP_ARDUINO_VERSION_MINOR));
  param.append(".");
  param.append(std::to_string(ESP_ARDUINO_VERSION_PATCH));
#else
  param = "V2.0.17";
#endif
}

uint32_t hw_get_device_online() {
#ifdef ARDUINO
  return instance.getDeviceProbe();
#else
  uint32_t hw_online = HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
#ifdef USING_INPUT_DEV_KEYBOARD
  hw_online |= HW_KEYBOARD_ONLINE;
#endif
  return hw_online;
#endif
}

void hw_set_disp_backlight(uint8_t level) {
#ifdef ARDUINO
  instance.setBrightness(level);
#endif
}

uint8_t hw_get_disp_backlight() {
#ifdef ARDUINO
  return instance.getBrightness();
#else
  return 100;
#endif
}

bool hw_get_disp_is_on() {
#ifdef ARDUINO
  return instance.getBrightness() != 0;
#else
  return true;
#endif
}

#if defined(ARDUINO)
// Power key short-press enters light sleep and restores the display on wake.
static uint8_t s_power_toggle_saved = 0;
static void power_key_light_sleep() {
  s_power_toggle_saved = instance.getBrightness();
  instance.decrementBrightness(0, 5, false);
  delay(150);
  instance.lightSleep(WAKEUP_SRC_POWER_KEY); // power key only, no touch wake
  instance.wakeupDisplay();
  instance.setBrightness(s_power_toggle_saved ? s_power_toggle_saved : 80);
  lv_display_trigger_activity(NULL);
}

static void power_key_event_cb(DeviceEvent_t event, void *params,
                               void *user_data) {
  PMUEventType_t evt = instance.getPMUEventType(params);

  if (evt == PMU_EVENT_KEY_LONG_PRESSED) {
    // Long press: wake the screen and show the power-off confirmation.
    extern void ui_power_off_show();
    if (instance.getBrightness() == 0) {
      instance.wakeupDisplay();
      instance.setBrightness(s_power_toggle_saved ? s_power_toggle_saved : 80);
    }
    lv_display_trigger_activity(NULL);
    ui_power_off_show();
    return;
  }

  if (evt != PMU_EVENT_KEY_CLICKED)
    return;

  // While recording, never light-sleep (it would stall the capture task).
  // The short press only toggles the backlight so the mic keeps running.
  if (hw_record_active()) {
    if (instance.getBrightness() != 0) {
      s_power_toggle_saved = instance.getBrightness();
      instance.setBrightness(0);
    } else {
      instance.setBrightness(s_power_toggle_saved ? s_power_toggle_saved : 80);
      lv_display_trigger_activity(NULL);
    }
    return;
  }

  if (instance.getBrightness() != 0) {
    power_key_light_sleep();
  } else {
    instance.wakeupDisplay();
    instance.setBrightness(s_power_toggle_saved ? s_power_toggle_saved : 80);
    lv_display_trigger_activity(NULL);
  }
}
#endif

void hw_enable_power_key_toggle() {
#if defined(ARDUINO)
  instance.onEvent(power_key_event_cb, POWER_EVENT, NULL);
#endif
}

#if defined(ARDUINO)
#include "esp_sleep.h"
#endif

void hw_rtc_set_alarm(uint8_t hour, uint8_t minute) {
#ifdef ARDUINO
  instance.rtc.resetAlarm();
  // setAlarm writes all 4 fields at once; set hour AND minute together,
  // day/week = 0xFF (NO_ALARM = don't care). Calling setAlarmByHours then
  // setAlarmByMinutes is a bug: the 2nd call resets hour to don't-care.
  instance.rtc.setAlarm(hour, minute, 0xFF, 0xFF);
  instance.rtc.enableAlarm();
#endif
}

void hw_rtc_clear_alarm() {
#ifdef ARDUINO
  instance.rtc.resetAlarm();
  instance.rtc.disableAlarm();
#endif
}

bool hw_woke_from_alarm() {
#ifdef ARDUINO
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    return instance.rtc.isAlarmActive();
  }
  return false;
#else
  return false;
#endif
}

void hw_enter_alarm_sleep() {
#ifdef ARDUINO
  // Cut every rail except the RTC, then deep sleep until the RTC alarm pulls
  // RTC_INT low. Wakes via a reboot; hw_woke_from_alarm() reports it.
  instance.powerControl(POWER_DISPLAY_BACKLIGHT, false);
  instance.powerControl(POWER_HAPTIC_DRIVER, false);
  instance.powerControl(POWER_GPS, false);
  instance.powerControl(POWER_SPEAK, false);
  instance.powerControl(POWER_NFC, false);
  instance.sleepDisplay();

  rtc_gpio_pullup_en((gpio_num_t)RTC_INT);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_sleep_enable_ext1_wakeup_io((1ULL << RTC_INT), ESP_EXT1_WAKEUP_ANY_LOW);
#else
  esp_sleep_enable_ext1_wakeup((1ULL << RTC_INT), ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();
  // never returns
#endif
}

void hw_set_kb_backlight(uint8_t level) {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
  instance.kb.setBrightness(level);
#endif
}

void hw_set_led_backlight(uint8_t level) {
#if defined(ARDUINO) && defined(USING_LED_INDICATOR)
  instance.setLedIndicatorBrightness(level);
#endif
}

uint8_t hw_get_kb_backlight() {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
  return instance.kb.getBrightness();
#else
  return 100;
#endif
}

int16_t hw_set_wifi_scan() {
#ifdef ARDUINO
  printf("hw_set_wifi_scan\n");
  return WiFi.scanNetworks(true);
#endif
  return 0;
}

bool hw_get_wifi_scanning() {
#ifdef ARDUINO
  return WiFi.getStatusBits() & WIFI_SCANNING_BIT;
#endif
  return false;
}

void hw_get_wifi_scan_result(vector<wifi_scan_params_t> &list) {
  list.clear();
#ifdef ARDUINO
  int16_t nums = WiFi.scanComplete();
  if (nums < 0) {
    printf("Nothing network found. return code : %d\n", nums);
    return;
  } else {
    printf("find %d network\n", nums);
  }
  // uint8_t networkItem, String &ssid, uint8_t &encryptionType, int32_t &RSSI,
  // uint8_t *&BSSID, int32_t &channel
  wifi_scan_params_t param;
  for (int i = 0; i < nums; ++i) {
    String ssid;
    uint8_t encryptionType;
    int32_t rssi;
    uint8_t *BSSID;
    int32_t channel;
    WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, BSSID, channel);
    printf("SSID:%s RSSI:%d\n", ssid.c_str(), rssi);
    param.authmode = encryptionType;
    param.ssid = ssid.c_str();
    param.rssi = rssi;
    param.channel = channel;
    memcpy(param.bssid, BSSID, 6);
    list.push_back(param);
  }
#else
  wifi_scan_params_t param;
  param.authmode = 1;
  param.ssid = "LilyGo-AABB0";
  param.rssi = -10;
  param.channel = 0;
  list.push_back(param);
#endif
}

#ifdef ARDUINO
// Credentials of the last single-shot connect (e.g. an NFC WiFi tag). They are
// persisted to WIFI_NS only once that exact network actually comes online, so a
// wrong password is never saved and the WiFiMulti path (which already reads
// from flash) cannot overwrite a good saved password with a stale one.
static String s_pending_ssid;
static String s_pending_pw;
static bool s_wifi_autosave_hooked = false;

static void wifi_autosave_on_got_ip(WiFiEvent_t event) {
  (void)event;
  if (s_pending_ssid.isEmpty())
    return;
  if (WiFi.SSID() != s_pending_ssid)
    return;
  hw_wifi_saved_add(s_pending_ssid.c_str(), s_pending_pw.c_str());
  s_pending_ssid = "";
  s_pending_pw = "";
}
#endif

void hw_set_wifi_connect(wifi_conn_params_t &params) {
  printf("hw_set_wifi_connect:ssid:<%s> password <%s>\n", params.ssid.c_str(),
         params.password.c_str());
#ifdef ARDUINO
  String ssid = params.ssid.c_str();
  String password = params.password.c_str();
  Serial.print("SSID :");
  Serial.println(ssid);
  Serial.print("PWD :");
  Serial.println(password);
  s_pending_ssid = ssid;
  s_pending_pw = password;
  if (!s_wifi_autosave_hooked) {
    WiFi.onEvent(wifi_autosave_on_got_ip, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    s_wifi_autosave_hooked = true;
  }
  WiFi.begin(ssid, password);
#endif
}

bool hw_get_wifi_connected() {
#ifdef ARDUINO
  return WiFi.isConnected();
#endif
  return false;
}

// ---------------------------------------------------------------------------
// Redes WiFi salvas (NVS namespace "wifinets": "n" = qtd, "s%u"/"p%u" = par).
// No PC (sim) usa store em memoria pra a UI funcionar sem flash.
// ---------------------------------------------------------------------------
#define WIFI_NS "wifinets"
#define WIFI_MAX_SAVED 8

#ifdef ARDUINO
static bool prefs_put_string_checked(Preferences &p, const char *key,
                                     const char *value) {
  const char *saved_value = value ? value : "";
  size_t written = p.putString(key, saved_value);
  return written > 0 || p.getString(key, "\x01") == String(saved_value);
}

static int wifi_saved_find(Preferences &p, uint8_t n, const char *ssid) {
  for (uint8_t i = 0; i < n; i++) {
    char ks[8];
    snprintf(ks, sizeof(ks), "s%u", i);
    if (p.getString(ks, "") == String(ssid)) {
      return (int)i;
    }
  }
  return -1;
}
#endif

#ifndef ARDUINO
static std::vector<std::pair<std::string, std::string>> g_sim_nets;
#endif

bool hw_wifi_saved_add(const char *ssid, const char *password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  const char *pw = password ? password : "";
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(WIFI_NS, false))
    return false;
  uint8_t n = p.getUChar("n", 0);
  int found = wifi_saved_find(p, n, ssid);
  uint8_t idx = (found >= 0) ? (uint8_t)found : n;
  if (found < 0) {
    if (n >= WIFI_MAX_SAVED) {
      p.end();
      return false;
    }
    n++;
  }
  char ks[8], kp[8];
  snprintf(ks, sizeof(ks), "s%u", idx);
  snprintf(kp, sizeof(kp), "p%u", idx);
  bool ok = prefs_put_string_checked(p, ks, ssid);
  ok = prefs_put_string_checked(p, kp, pw) && ok;
  ok = (p.putUChar("n", n) == 1) && ok;
  p.end();
  return ok;
#else
  for (auto &e : g_sim_nets) {
    if (e.first == ssid) {
      e.second = pw;
      return true;
    }
  }
  if (g_sim_nets.size() >= WIFI_MAX_SAVED) {
    return false;
  }
  g_sim_nets.push_back({ssid, pw});
  return true;
#endif
}

uint8_t hw_wifi_saved_count() {
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(WIFI_NS, true))
    return 0;
  uint8_t n = p.getUChar("n", 0);
  p.end();
  return n;
#else
  return (uint8_t)g_sim_nets.size();
#endif
}

bool hw_wifi_saved_get(uint8_t idx, std::string &ssid) {
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(WIFI_NS, true))
    return false;
  uint8_t n = p.getUChar("n", 0);
  bool ok = idx < n;
  if (ok) {
    char ks[8];
    snprintf(ks, sizeof(ks), "s%u", idx);
    ssid = p.getString(ks, "").c_str();
  }
  p.end();
  return ok;
#else
  if (idx >= g_sim_nets.size()) {
    return false;
  }
  ssid = g_sim_nets[idx].first;
  return true;
#endif
}

bool hw_wifi_saved_remove(const char *ssid) {
  if (ssid == nullptr) {
    return false;
  }
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(WIFI_NS, false))
    return false;
  uint8_t n = p.getUChar("n", 0);
  int found = wifi_saved_find(p, n, ssid);
  if (found < 0) {
    p.end();
    return false;
  }
  // compacta: desliza os de cima pra baixo.
  for (uint8_t i = (uint8_t)found; i + 1 < n; i++) {
    char ks0[8], kp0[8], ks1[8], kp1[8];
    snprintf(ks0, sizeof(ks0), "s%u", i);
    snprintf(kp0, sizeof(kp0), "p%u", i);
    snprintf(ks1, sizeof(ks1), "s%u", i + 1);
    snprintf(kp1, sizeof(kp1), "p%u", i + 1);
    String next_ssid = p.getString(ks1, "");
    String next_pw = p.getString(kp1, "");
    if (!prefs_put_string_checked(p, ks0, next_ssid.c_str()) ||
        !prefs_put_string_checked(p, kp0, next_pw.c_str())) {
      p.end();
      return false;
    }
  }
  char ksl[8], kpl[8];
  snprintf(ksl, sizeof(ksl), "s%u", n - 1);
  snprintf(kpl, sizeof(kpl), "p%u", n - 1);
  if (!p.remove(ksl))
    log_w("Failed to remove saved WiFi SSID tail");
  if (!p.remove(kpl))
    log_w("Failed to remove saved WiFi password tail");
  bool ok = p.putUChar("n", n - 1) == 1;
  p.end();
  return ok;
#else
  for (auto it = g_sim_nets.begin(); it != g_sim_nets.end(); ++it) {
    if (it->first == ssid) {
      g_sim_nets.erase(it);
      return true;
    }
  }
  return false;
#endif
}

#ifdef ARDUINO
static volatile bool s_wifi_connecting = false;

// Task de fundo: carrega as salvas no WiFiMulti e gira ate conectar/timeout.
// wm e' local da task (vector de APs) e some quando a task termina.
static void wifi_connect_task(void *arg) {
  uint32_t timeout_ms = (uint32_t)(uintptr_t)arg;
  WiFiMulti wm;
  WiFi.mode(WIFI_STA);
  Preferences p;
  if (p.begin(WIFI_NS, true)) {
    uint8_t n = p.getUChar("n", 0);
    for (uint8_t i = 0; i < n; i++) {
      char ks[8], kp[8];
      snprintf(ks, sizeof(ks), "s%u", i);
      snprintf(kp, sizeof(kp), "p%u", i);
      String s = p.getString(ks, "");
      String pw = p.getString(kp, "");
      if (s.length())
        wm.addAP(s.c_str(), pw.c_str());
    }
    p.end();
  }
  // wm.run() varre e conecta na salva de melhor sinal disponivel.
  uint32_t start = millis();
  while (wm.run() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(200);
  }
  s_wifi_connecting = false;
  vTaskDelete(NULL);
}
#endif

bool hw_wifi_multi_connect(uint32_t timeout_ms) {
#ifdef ARDUINO
  if (s_wifi_connecting)
    return true; // ja em andamento
  if (hw_wifi_saved_count() == 0)
    return false;
  s_wifi_connecting = true;
  // Stack folgada: WiFiMulti guarda um vector de String (ssid/senha).
  if (xTaskCreate(wifi_connect_task, "wifi_conn", 6144,
                  (void *)(uintptr_t)timeout_ms, 1, NULL) != pdPASS) {
    s_wifi_connecting = false;
    return false;
  }
  return true;
#else
  (void)timeout_ms;
  return false;
#endif
}

void hw_wifi_off() {
#ifdef ARDUINO
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#endif
}

#ifdef ARDUINO
static void listDir(vector<AudioParams_t> &list, fs::FS &fs,
                    const char *dirname, uint8_t levels,
                    audio_source_type_t source_type) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        std::string next_dir = dirname;
        if (next_dir != "/")
          next_dir += "/";
        next_dir += file.name();
        listDir(list, fs, next_dir.c_str(), levels - 1, source_type);
      }
    } else {
      String filename = file.name();
      if (filename.endsWith(".mp3") || filename.endsWith(".wav")) {
        list.push_back({source_type, filename.c_str()});
      }

      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
}
#endif

void hw_fat_list(vector<AudioParams_t> &list, const char *dirname,
                 uint8_t levels) {
#if defined(ARDUINO)
  Serial.printf("FFAT Listing directory: %s\n", dirname);
  listDir(list, FFat, dirname, levels, AUDIO_SOURCE_FATFS);
#endif
}

bool hw_sd_list(vector<AudioParams_t> &list, const char *dirname,
                uint8_t levels) {
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
  instance.lockSPI();
  if (instance.installSD()) {
    Serial.println("SD Card mount success.");
  } else {
    Serial.println("SD Card mount failed.");
    instance.unlockSPI();
    return false;
  }
  listDir(list, SD, dirname, levels, AUDIO_SOURCE_SDCARD);
  instance.unlockSPI();
#endif
  return true;
}

void hw_mount_sd() {
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
  instance.installSD();
#endif
}

void hw_get_filesystem_music(vector<AudioParams_t> &list) {
  list.clear();

#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
  Serial.println("\n================== SD Music List ==================");
  hw_sd_list(list, "/", 0);
#endif

  Serial.println("\n================== FFat Music List ==================");
  hw_fat_list(list, "/", 0);

#else
  list.push_back({AUDIO_SOURCE_FATFS, "/abc.mp3"});
  list.push_back({AUDIO_SOURCE_FATFS, "/ccc.mp3"});
  list.push_back({AUDIO_SOURCE_FATFS, "/ddd.mp3"});
#endif
}

void hw_set_sd_music_play(audio_source_type_t source_type,
                          const char *filename) {
  if (!filename)
    return;
  printf("hw_set_sd_music_play : %s source_type:%d\n", filename, source_type);
#ifdef ARDUINO
  if (hw_record_active())
    return;
  hw_set_play_stop();
  if (hw_record_active())
    return;

  audio_params_t params = {};
  params.event = APP_EVENT_PLAY;
  params.source_type = source_type;
  params.request_id = ++playerRequestId;
  snprintf(params.filename, sizeof(params.filename), "%s", filename);
  xEventGroupSetBits(playerEvent, PLAYER_PLAY | PLAYER_PENDING);
  if (xQueueSend(playerQueue, &params, portMAX_DELAY) != pdPASS) {
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_PENDING);
  }
  Serial.println("hw_set_sd_music_play send done\n");
#endif
}

void hw_set_play_stop() {
#ifdef ARDUINO
  ++playerRequestId;
  xEventGroupClearBits(playerEvent, PLAYER_PLAY);
  xEventGroupSetBits(playerEvent, PLAYER_END);
  xQueueReset(playerQueue);
  xEventGroupClearBits(playerEvent, PLAYER_PENDING);
  while (xEventGroupGetBits(playerEvent) & PLAYER_RUNNING) {
    delay(2);
  }
  xEventGroupClearBits(playerEvent, PLAYER_END);
#endif
}

void hw_set_sd_music_pause() {
  printf("playerTaskHandler pause!\n");
#ifdef ARDUINO
  xEventGroupClearBits(playerEvent, PLAYER_PLAY);
#endif
}

void hw_set_sd_music_resume() {
  printf("playerTaskHandler resume!\n");
#ifdef ARDUINO
  xEventGroupSetBits(playerEvent, PLAYER_PLAY);
#endif
}

bool hw_player_running() {
#ifdef ARDUINO
  return xEventGroupGetBits(playerEvent) & (PLAYER_PENDING | PLAYER_RUNNING);
#endif
  return true;
}

void hw_set_volume(uint8_t volume) {
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    instance.codec.setVolume(volume);
  } else {
    printf("Audio codec not online!\n");
  }
#endif // USING_AUDIO_CODEC
}

uint8_t hw_get_volume() {
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
  if (HW_CODEC_ONLINE & hw_get_device_online()) {
    return instance.codec.getVolume();
  } else {
    return 0;
  }
#else
  return 100;
#endif // USING_AUDIO_CODEC
}

void hw_shutdown() {
#ifdef ARDUINO
  // Finish filesystem and codec work before cutting PMU power.
  hw_record_stop();
  hw_set_play_stop();
  instance.decrementBrightness(0, 5, false);
#if defined(USING_PPM_MANAGE)
  instance.ppm.shutdown();
#elif defined(USING_PMU_MANAGE)
  instance.pmu.shutdown();
#endif
#endif
}

void hw_sleep() {
#ifdef ARDUINO
  vTaskDelete(playerTaskHandler);

#ifdef USING_PDM_MICROPHONE
  instance.mic.end();
#endif

#ifdef USING_PCM_AMPLIFIER
  instance.player.end();
#endif

  instance.decrementBrightness(0, 5, false);
  instance.sleep();
#endif
}

bool hw_get_otg_enable() {
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
  return instance.ppm.isEnableOTG();
#else
  return false;
#endif
}

bool hw_set_otg(bool enable) {
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
  if (enable) {
    return instance.ppm.enableOTG();
  } else {
    instance.ppm.disableOTG();
  }
  return true;
#endif
  return false;
}

bool hw_get_charge_enable() {
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
  return instance.ppm.isEnableCharge();
#elif defined(USING_PMU_MANAGE)
  return instance.isEnableCharge();
#endif
#else
  return false;
#endif
}

void hw_set_charger(bool enable) {
  user_setting.charger_enable = enable;
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
  if (enable) {
    instance.ppm.setChargerConstantCurr(
        hw_clamp_charger_current(user_setting.charger_current));
    instance.ppm.enableCharge();
  } else {
    instance.ppm.disableCharge();
  }
#elif defined(USING_PMU_MANAGE)
  if (enable) {
    instance.enableCharge(
        hw_clamp_charger_current(user_setting.charger_current));
  } else {
    instance.disableCharge();
  }
#endif
#endif
}

uint16_t hw_get_charger_current() {
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
  return instance.ppm.getChargerConstantCurr();
#elif defined(USING_PMU_MANAGE)
  return instance.getChargeCurrent();
#endif
#else
  return 0;
#endif
}

void hw_set_charger_current(uint16_t milliampere) {
  // clamped value is consumed by the PMU calls below on hardware; sim has no
  // PMU NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  milliampere = hw_clamp_charger_current(milliampere);
  user_setting.charger_current = milliampere;
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
  instance.ppm.setChargerConstantCurr(milliampere);
#elif defined(USING_PMU_MANAGE)
  instance.setChargeCurrent(milliampere);
#endif
#endif
}

uint8_t hw_get_charger_current_level() {
#if defined(USING_PPM_MANAGE)
  uint16_t current = hw_clamp_charger_current(user_setting.charger_current);
  if (dev_conts_var.charge_steps == 0)
    return 0;
  return (current + dev_conts_var.charge_steps - 1) /
         dev_conts_var.charge_steps;
#elif defined(USING_PMU_MANAGE)
  uint16_t cur = hw_clamp_charger_current(instance.getChargeCurrent());
  for (int i = 0;
       i < sizeof(k_pmu_charge_table) / sizeof(k_pmu_charge_table[0]); ++i) {
    if (cur <= k_pmu_charge_table[i]) {
      return i;
    }
  }
  return hw_pmu_max_charge_level();
#else
  uint16_t cur = hw_clamp_charger_current(user_setting.charger_current);
  for (int i = 0;
       i < sizeof(k_pmu_charge_table) / sizeof(k_pmu_charge_table[0]); ++i) {
    if (cur <= k_pmu_charge_table[i]) {
      return i;
    }
  }
  return hw_pmu_max_charge_level();
#endif
}

uint16_t hw_set_charger_current_level(uint8_t level) {
#if defined(USING_PPM_MANAGE)
  uint16_t current =
      hw_clamp_charger_current(level * dev_conts_var.charge_steps);
#else
  uint8_t max_level = hw_pmu_max_charge_level();
  if (level > max_level) {
    level = max_level;
  }
  uint16_t current = k_pmu_charge_table[level];
#endif
  printf("set charge current:%u mA\n", current);
  hw_set_charger_current(current);
  return user_setting.charger_current;
}

void hw_get_monitor_params(monitor_params_t &params) {
  params = monitor_params_t{};
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
  params.type = MONITOR_PPM;
  params.charge_state = instance.ppm.getChargeStatusString();
  params.usb_voltage = instance.ppm.getVbusVoltage();
  params.sys_voltage = instance.ppm.getSystemVoltage();
  instance.ppm.getFaultStatus();
  if (instance.ppm.isNTCFault()) {
    params.ntc_state = instance.ppm.getNTCStatusString();
  } else {
    params.ntc_state = "Normal";
  }
#elif defined(USING_PMU_MANAGE)
  params.type = MONITOR_PMU;
  params.charge_state = instance.pmu.isCharging() ? "Charging" : "Not charging";
  params.usb_voltage = instance.pmu.getVbusVoltage();
  params.sys_voltage = instance.pmu.getSystemVoltage();
  params.battery_voltage = instance.pmu.getBattVoltage();
  params.battery_connected = instance.pmu.isBatteryConnect();
  params.vbus_present = instance.pmu.isVbusIn();
  params.battery_percent = instance.pmu.getBatteryPercent();
  params.temperature = instance.pmu.getTemperature();
  params.ntc_state = "Unavailable";
#endif

#ifdef USING_BQ_GAUGE
  if (hw_get_device_online() & HW_GAUGE_ONLINE) {
    instance.gauge.refresh();
    params.battery_percent = instance.gauge.getStateOfCharge();
    params.battery_voltage = instance.gauge.getVoltage();
    params.instantaneousCurrent = instance.gauge.getCurrent();
    params.remainingCapacity = instance.gauge.getRemainingCapacity();
    params.fullChargeCapacity = instance.gauge.getFullChargeCapacity();
    params.standbyCurrent = instance.gauge.getStandbyCurrent();
    params.temperature = instance.gauge.getTemperature();
    params.designCapacity = instance.gauge.getDesignCapacity();
    params.averagePower = instance.gauge.getAveragePower();
    params.maxLoadCurrent = instance.gauge.getMaxLoadCurrent();
    BatteryStatus batteryStatus = instance.gauge.getBatteryStatus();

    if (batteryStatus.isInDischargeMode()) {
      params.timeToEmpty = instance.gauge.getTimeToEmpty();
      params.timeToFull = 0;
    } else {
      if (batteryStatus.isFullChargeDetected()) {
        params.timeToFull = 0;
        params.timeToEmpty = 0;
      } else {
        params.timeToEmpty = 0;
        params.timeToFull = instance.gauge.getTimeToFull();
      }
    }
  }
#endif

#else
  params.type = MONITOR_PPM;
  // sim-only fake battery reading; rand() randomness is adequate for the
  // simulator NOLINTNEXTLINE(cert-msc30-c,cert-msc50-cpp)
  params.battery_percent = 30 + rand() % (100 - 30 + 1);
  params.battery_voltage = 4178;
  params.battery_connected = true;
  params.vbus_present = true;
  params.charge_state = "Fast charging";
  params.usb_voltage = 4998;
  params.ntc_state = "Normal";
#endif
}

static imu_params_t imu_params = {0, 0, 0, 0};

void hw_get_imu_params(imu_params_t &params) {
#ifdef ARDUINO
#if defined(USING_BHI260_SENSOR)
  if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
    params = imu_params;
  }
#elif defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    params.orientation = instance.sensor.direction();
    params.temperature = instance.sensor.getTemperature(SensorBMA423::TEMP_DEG);
  }
#endif // SENSOR
#else
  params = imu_params;
  params.temperature = 24.5f; // sim stub
#endif // ARDUINO
}

#if defined(ARDUINO) && defined(USING_BHI260_SENSOR)
void imu_data_process(uint8_t sensor_id, uint8_t *data_ptr, uint32_t len,
                      uint64_t *timestamp, void *user_data) {
  float roll, pitch, yaw;
  bhy2_quaternion_to_euler(data_ptr, &roll, &pitch, &yaw);
  imu_params.roll = roll;
  imu_params.pitch = pitch;
  imu_params.heading = yaw;
}
#endif // ARDUINO

void hw_register_imu_process() {
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
  if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
    float sample_rate = 100.0;      /* Read out data measured at 100Hz */
    uint32_t report_latency_ms = 0; /* Report immediately */
    // LilyGoLib has already processed it
    // instance.sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
    // Enable Quaternion function
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, sample_rate,
                              report_latency_ms);
    // Register event callback function
    instance.sensor.onResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR,
                                  imu_data_process);
  }
#elif defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();
  }
#endif // SENSOR
#endif // ARDUINO
}

void hw_unregister_imu_process() {
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
  if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 0, 0);
  }
#elif defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    instance.sensor.disableAccelerometer();
  }
#endif // SENSOR
#endif // ARDUINO
}

#ifndef ARDUINO
static uint32_t s_sim_steps = 0; // sim-only fake step counter
#endif

void hw_pedometer_start() {
#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    // Low-power config: 2G range is plenty for gait, 50Hz is the rate the
    // Bosch step-counter engine expects, CIC averaging trims current draw.
    instance.sensor.configAccelerometer(
        SensorBMA423::RANGE_2G, SensorBMA423::ODR_50HZ,
        SensorBMA423::BW_NORMAL_AVG4, SensorBMA423::PERF_CIC_AVG_MODE);
    instance.sensor.enableAccelerometer();
    instance.sensor.enableFeature(SensorBMA423::FEATURE_STEP_CNTR, true);
    instance.sensor.enablePedometer(true);
    // Advanced power save: lets the sensor idle the digital core between
    // samples while the feature engine keeps counting.
    instance.sensor.enablePowerSave();
  }
#endif
}

void hw_pedometer_stop() {
#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    instance.sensor.disablePedometer();
    instance.sensor.enableFeature(SensorBMA423::FEATURE_STEP_CNTR, false);
    instance.sensor.disablePowerSave();
    instance.sensor.disableAccelerometer();
  }
#endif
}

uint32_t hw_pedometer_get_steps() {
#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    return instance.sensor.getPedometerCounter();
  }
  return 0;
#else
  // sim stub: fake a steady walk so the screen shows a moving count
  s_sim_steps += 2;
  return s_sim_steps;
#endif
}

void hw_pedometer_reset() {
#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
  if (hw_get_device_online() & HW_BMA423_ONLINE) {
    instance.sensor.resetPedometer();
  }
#else
  s_sim_steps = 0;
#endif
}

//* ble //

void hw_enable_ble(const char *devName) {
#if defined(ARDUINO) && defined(USING_UART_BLE)
#endif
}

void hw_deinit_ble() {
#if defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

void hw_disable_ble() {
#if defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

size_t hw_get_ble_message(char *buffer, size_t buffer_size) {
#if defined(ARDUINO) && defined(USING_UART_BLE)
#endif
  return 0;
}

const char *hw_get_ble_kb_name() { return "Keyboard"; }

void hw_set_ble_kb_enable() {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  bleKeyboard.setName("Keyboard");
  bleKeyboard.begin();
#endif
#endif
}

void hw_set_ble_kb_disable() {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
  bleKeyboard.end();
  log_d("Disable ble devices");
#endif
}

void hw_set_ble_kb_char(const char *c) {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  if (bleKeyboard.isConnected()) {
    bleKeyboard.print(c);
  }
#endif
#endif
}

void hw_set_ble_kb_key(uint8_t key) {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  if (bleKeyboard.isConnected()) {
    bleKeyboard.press(key);
  }
#endif
#endif
}

void hw_set_ble_kb_release() {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  if (bleKeyboard.isConnected()) {
    bleKeyboard.releaseAll();
  }
#endif
#endif
}

bool hw_get_ble_kb_connected() {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  if (bleKeyboard.isConnected()) {
    return true;
  }
#endif
#endif
  return false;
}

void hw_set_ble_key(media_key_value_t key) {
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
  if (bleKeyboard.isConnected()) {
    switch (key) {
    case MEDIA_VOLUME_UP:
      bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
      break;
    case MEDIA_VOLUME_DOWN:
      bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
      break;
    case MEDIA_PLAY_PAUSE:
      bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      break;
    case MEDIA_NEXT:
      bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
      break;
    case MEDIA_PREVIOUS:
      bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
      break;
    default:
      return;
    }
  }
#endif
#endif
}

void hw_set_keyboard_read_callback(void (*read)(int state, char &c)) {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
  instance.kb.setCallback(read);
#endif
}

void hw_feedback() {
#ifdef ARDUINO
  instance.vibrator();
#endif
}

// Vibra no maximo: DRV2605 effect 47 = "Buzz 1 - 100%".
// Restaura o efeito anterior depois de disparar, senao hw_feedback()
// continuaria vibrando no maximo (vibrator() reusa _effects).
void hw_vibrate_max() {
#ifdef ARDUINO
  uint8_t prev = instance.getHapticEffects();
  instance.setHapticEffects(47);
  instance.vibrator();
  instance.setHapticEffects(prev);
#endif
}

// Som do alarme salvo no NVS (namespace "alarm": "src" uchar, "file" string).
#define ALARM_NS "alarm"

void hw_alarm_sound_set(uint8_t source_type, const char *file) {
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(ALARM_NS, false)) {
    log_e("Failed to open alarm NVS");
    return;
  }
  const char *saved_file = file ? file : "";
  bool ok = p.putUChar("src", source_type) == 1;
  ok = prefs_put_string_checked(p, "file", saved_file) && ok;
  if (!ok) {
    log_e("Failed to save alarm sound");
  }
  p.end();
#else
  (void)source_type;
  (void)file;
#endif
}

bool hw_alarm_sound_get(uint8_t &source_type, std::string &file) {
#ifdef ARDUINO
  Preferences p;
  if (!p.begin(ALARM_NS, true)) {
    log_e("Failed to open alarm NVS");
    return false;
  }
  String f = p.getString("file", "");
  source_type = p.getUChar("src", 0);
  p.end();
  if (f.length() == 0)
    return false;
  file = f.c_str();
  return true;
#else
  (void)source_type;
  (void)file;
  return false;
#endif
}

// --- Recurring daily alarm --------------------------------------------------
// Config lives in the same "alarm" NVS namespace as the sound. rec_on/rec_h/
// rec_m = the daily alarm; night_h = hour after which an idle watch deep-sleeps
// until the alarm (so it can wake to ring). See ui_alarm.cpp / ui_main idle
// path.
static bool s_rec_alarm_loaded = false;
static bool s_rec_alarm_nvs_ok = false;
static bool s_rec_alarm_enabled = false;
static uint8_t s_rec_alarm_hour = 7;
static uint8_t s_rec_alarm_min = 0;
static uint8_t s_rec_alarm_night_h = 23;

static bool hw_alarm_cfg_load_once() {
  if (s_rec_alarm_loaded) {
    return s_rec_alarm_nvs_ok;
  }
#ifdef ARDUINO
  Preferences p;
  if (p.begin(ALARM_NS, true)) {
    s_rec_alarm_enabled = p.getUChar("rec_on", 0) != 0;
    s_rec_alarm_hour = p.getUChar("rec_h", 7);
    s_rec_alarm_min = p.getUChar("rec_m", 0);
    s_rec_alarm_night_h = p.getUChar("night_h", 23);
    p.end();
    s_rec_alarm_nvs_ok = true;
  } else {
    s_rec_alarm_nvs_ok = false;
  }
#endif
  s_rec_alarm_loaded = true;
  return s_rec_alarm_nvs_ok;
}

void hw_alarm_cfg_set(bool enabled, uint8_t hour, uint8_t minute,
                      uint8_t night_h) {
  s_rec_alarm_enabled = enabled;
  s_rec_alarm_hour = hour;
  s_rec_alarm_min = minute;
  s_rec_alarm_night_h = night_h;
  s_rec_alarm_loaded = true;
#ifdef ARDUINO
  Preferences p;
  if (p.begin(ALARM_NS, false)) {
    p.putUChar("rec_on", enabled ? 1 : 0);
    p.putUChar("rec_h", hour);
    p.putUChar("rec_m", minute);
    p.putUChar("night_h", night_h);
    p.end();
    s_rec_alarm_nvs_ok = true;
  } else {
    s_rec_alarm_nvs_ok = false;
  }
  // Arm the RTC now so it is live for tonight (or disarm if turned off).
  if (enabled) {
    hw_rtc_set_alarm(hour, minute);
  } else {
    hw_rtc_clear_alarm();
  }
#else
  (void)enabled;
  (void)hour;
  (void)minute;
  (void)night_h;
#endif
}

bool hw_alarm_cfg_get(bool &enabled, uint8_t &hour, uint8_t &minute,
                      uint8_t &night_h) {
  bool ok = hw_alarm_cfg_load_once();
  enabled = s_rec_alarm_enabled;
  hour = s_rec_alarm_hour;
  minute = s_rec_alarm_min;
  night_h = s_rec_alarm_night_h;
  return ok;
}

void hw_alarm_arm_recurring() {
#ifdef ARDUINO
  bool en;
  uint8_t h, m, nh;
  hw_alarm_cfg_get(en, h, m, nh);
  if (en) {
    hw_rtc_set_alarm(h, m);
  }
#endif
}

void hw_rtc_alarm_ack() {
#ifdef ARDUINO
  // Clear the alarm flag but keep it enabled so it fires again tomorrow.
  instance.rtc.resetAlarm();
#endif
}

bool hw_alarm_recurring_fired() {
#ifdef ARDUINO
  bool en;
  uint8_t h, m, nh;
  hw_alarm_cfg_get(en, h, m, nh);
  return en && instance.rtc.isAlarmActive();
#else
  return false;
#endif
}

bool hw_alarm_should_deep_sleep() {
#ifdef ARDUINO
  bool en;
  uint8_t h, m, nh;
  hw_alarm_cfg_get(en, h, m, nh);
  if (!en) {
    return false;
  }
  struct tm now = {};
  hw_get_date_time(now);
  int nowMin = now.tm_hour * 60 + now.tm_min;
  int alarmMin = h * 60 + m;
  int nightMin = nh * 60;
  if (nightMin > alarmMin) {
    // Window crosses midnight (e.g. 23:00 -> 07:30).
    return (nowMin >= nightMin) || (nowMin < alarmMin);
  }
  // Same-day window (e.g. 01:00 -> 07:30).
  return (nowMin >= nightMin) && (nowMin < alarmMin);
#else
  return false;
#endif
}

void hw_low_power_loop() {
#ifdef ARDUINO
  // Power key only: a stray touch must NOT wake the watch from the idle
  // battery-saving sleep. (Vendor default also arms the touch panel.)
  instance.lightSleep(WAKEUP_SRC_POWER_KEY);
  // #ifdef USING_ST25R3916
  //     beginNFC(nrf_notify_callback, ndef_event_callback);
  // #endif
#endif
}

// Set true while the alarm is ringing so the idle timeout skips light sleep
// (which would freeze playerTask and cut the alarm sound). Plain bool: usable
// from the sim build too.
static bool s_alarm_ringing = false;

void hw_set_alarm_ringing(bool ringing) { s_alarm_ringing = ringing; }

bool hw_alarm_ringing() { return s_alarm_ringing; }

void hw_set_audio_gain(uint8_t gain) {
#ifdef ARDUINO
  s_pcm_gain = gain ? gain : 1;
#else
  (void)gain;
#endif
}

// --- Share over WiFi SoftAP -------------------------------------------------
// A tiny access point + web page that lists the recordings and lets a phone/PC
// download them. Extracts the WAV memos off the FFat flash (no SD, no USB
// path).
#ifdef ARDUINO
#include <WebServer.h>
#include <esp_random.h>

static WebServer *s_share_server = NULL;
static TaskHandle_t s_share_task = NULL;
static volatile bool s_share_running = false;
static const char *SHARE_HTTP_USER = "share";
static String s_share_ssid, s_share_pass, s_share_ip, s_share_http_pass;

static void share_random_numeric_pass(char *pass, size_t pass_size) {
  snprintf(pass, pass_size, "%08u", (unsigned)(esp_random() % 100000000u));
}

static bool share_require_auth() {
  if (s_share_server->authenticate(SHARE_HTTP_USER,
                                   s_share_http_pass.c_str())) {
    return true;
  }
  s_share_server->requestAuthentication();
  return false;
}

static void share_handle_index() {
  if (!share_require_auth()) {
    return;
  }
  std::vector<std::string> files;
  hw_record_list(files); // reuse the recorder's listing
  String html = "<!doctype html><meta name=viewport "
                "content='width=device-width,initial-scale=1'>"
                "<title>T-Watch Share</title>"
                "<h2>Gravacoes</h2>";
  if (files.empty()) {
    html += "<p>(vazio)</p>";
  } else {
    html += "<ul>";
    for (auto &f : files) {
      const char *name = f.c_str();
      if (name[0] == '/') {
        name++;
      }
      html += "<li><a href='/dl?f=";
      html += name;
      html += "'>";
      html += name;
      html += "</a></li>";
    }
    html += "</ul>";
  }
  s_share_server->send(200, "text/html", html);
}

static void share_handle_download() {
  if (!share_require_auth()) {
    return;
  }
  if (!s_share_server->hasArg("f")) {
    s_share_server->send(400, "text/plain", "missing f");
    return;
  }
  String name = s_share_server->arg("f");
  // Keep it inside the FFat root: no path separators, no traversal.
  if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
    s_share_server->send(403, "text/plain", "bad name");
    return;
  }
  File f = FFat.open("/" + name, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) {
      f.close();
    }
    s_share_server->send(404, "text/plain", "not found");
    return;
  }
  s_share_server->sendHeader("Content-Disposition",
                             "attachment; filename=" + name);
  s_share_server->streamFile(f, "application/octet-stream");
  f.close();
}

static void share_task(void *arg) {
  (void)arg;
  while (s_share_running) {
    s_share_server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  // Own the teardown so the main thread never frees the server mid-request.
  s_share_server->stop();
  delete s_share_server;
  s_share_server = NULL;
  s_share_task = NULL; // signals hw_share_stop() we are done
  vTaskDelete(NULL);
}

bool hw_share_start() {
  if (s_share_running) {
    return true;
  }
  // 8-digit numeric passwords: easy to type; WiFi still needs >= 8 chars.
  char pass[16], http_pass[16];
  share_random_numeric_pass(pass, sizeof(pass));
  share_random_numeric_pass(http_pass, sizeof(http_pass));
  s_share_ssid = "TWatch-Share";
  s_share_pass = pass;
  s_share_http_pass = http_pass;

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(s_share_ssid.c_str(), s_share_pass.c_str())) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  s_share_ip = WiFi.softAPIP().toString();

  s_share_server = new WebServer(80);
  s_share_server->on("/", share_handle_index);
  s_share_server->on("/dl", share_handle_download);
  s_share_server->begin();

  s_share_running = true;
  if (xTaskCreate(share_task, "app/share", 6144, NULL, 5, &s_share_task) !=
      pdPASS) {
    s_share_running = false;
    s_share_server->stop();
    delete s_share_server;
    s_share_server = NULL;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
  return true;
}

void hw_share_stop() {
  if (!s_share_running) {
    return;
  }
  s_share_running = false;
  while (s_share_task != NULL) { // wait for the task to free the server
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool hw_share_active() { return s_share_running; }

void hw_share_info(std::string &ssid, std::string &wifi_pass, std::string &ip,
                   std::string &http_user, std::string &http_pass) {
  ssid = s_share_ssid.c_str();
  wifi_pass = s_share_pass.c_str();
  ip = s_share_ip.c_str();
  http_user = SHARE_HTTP_USER;
  http_pass = s_share_http_pass.c_str();
}
#else // sim stubs
bool hw_share_start() { return false; }
void hw_share_stop() {}
bool hw_share_active() { return false; }
void hw_share_info(std::string &ssid, std::string &wifi_pass, std::string &ip,
                   std::string &http_user, std::string &http_pass) {
  ssid = "TWatch-Share";
  wifi_pass = "00000000";
  ip = "192.168.4.1";
  http_user = "share";
  http_pass = "11111111";
}
#endif

void hw_light_sleep_timed(uint32_t ms) {
#ifdef ARDUINO
  // Enable the timer wake source first; instance.lightSleep() then adds the
  // ext1 power-key wake and calls esp_light_sleep_start(). Both sources stay
  // armed, so the CPU wakes on whichever fires first. Touch is intentionally
  // NOT a wake source; the power key is always included, so the watch can
  // never get stuck asleep.
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  instance.lightSleep(
      (WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_TIMER));
#else
  (void)ms;
#endif
}

void hw_inc_brightness(uint8_t level) {
#ifdef ARDUINO
  instance.incrementalBrightness(level);
#endif
}

void hw_dec_brightness(uint8_t level) {
#ifdef ARDUINO
  instance.decrementBrightness(level);
#endif
}

uint8_t hw_get_disp_min_brightness() { return dev_conts_var.min_brightness; }

uint16_t hw_get_disp_max_brightness() { return dev_conts_var.max_brightness; }

uint8_t hw_get_min_charge_current() { return dev_conts_var.min_charge_current; }

uint16_t hw_get_max_charge_current() { return hw_safe_max_charger_current(); }

uint8_t hw_get_charge_level_nums() {
#if defined(USING_PPM_MANAGE)
  if (dev_conts_var.charge_steps == 0)
    return 0;
  uint8_t max_level =
      hw_safe_max_charger_current() / dev_conts_var.charge_steps;
  return max_level < dev_conts_var.charge_level_nums
             ? max_level
             : dev_conts_var.charge_level_nums;
#elif defined(USING_PMU_MANAGE)
  return hw_pmu_max_charge_level();
#else
  return hw_pmu_max_charge_level();
#endif
}

uint8_t hw_get_charge_steps() { return dev_conts_var.charge_steps; }

void hw_set_cpu_freq(uint32_t mhz) {
#ifdef ARDUINO
  setCpuFrequencyMhz(mhz);
#endif
}

void hw_disable_input_devices() {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
  instance.disableRotary();
#endif
}

void hw_enable_input_devices() {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
  instance.enableRotary();
#endif
}

void hw_enable_keyboard() {
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
  instance.enableKeyboard();
#endif
}

void hw_disable_keyboard() {
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
  instance.disableKeyboard();
#endif
}

void hw_flush_keyboard() {
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
  if (hw_get_device_online() & HW_KEYBOARD_ONLINE) {
    instance.kb.flush();
  }
#endif
}

bool hw_has_keyboard() { return hw_get_device_online() & HW_KEYBOARD_ONLINE; }

bool hw_has_indicator_led() {
  return hw_get_device_online() & HW_LED_INDIC_ONLINE;
}

bool hw_has_otg_function() {
#if defined(USING_PPM_MANAGE)
  return true;
#else
  return true;
#endif
}

#if defined(ARDUINO)
#include <Esp.h>
#endif
void hw_print_mem_info() {
#if defined(ARDUINO)
  printf("INTERNAL Memory Info:\n");
  printf("------------------------------------------\n");
  printf("  Total Size        :   %u B ( %.1f KB)\n", ESP.getHeapSize(),
         ESP.getHeapSize() / 1024.0);
  printf("  Free Bytes        :   %u B ( %.1f KB)\n", ESP.getFreeHeap(),
         ESP.getFreeHeap() / 1024.0);
  printf("  Minimum Free Bytes:   %u B ( %.1f KB)\n", ESP.getMinFreeHeap(),
         ESP.getMinFreeHeap() / 1024.0);
  printf("  Largest Free Block:   %u B ( %.1f KB)\n", ESP.getMaxAllocHeap(),
         ESP.getMaxAllocHeap() / 1024.0);
  printf("------------------------------------------\n");
  printf("SPIRAM Memory Info:\n");
  printf("------------------------------------------\n");
  printf("  Total Size        :  %u B (%.1f KB)\n", ESP.getPsramSize(),
         ESP.getPsramSize() / 1024.0);
  printf("  Free Bytes        :  %u B (%.1f KB)\n", ESP.getFreePsram(),
         ESP.getFreePsram() / 1024.0);
  printf("  Minimum Free Bytes:  %u B (%.1f KB)\n", ESP.getMinFreePsram(),
         ESP.getMinFreePsram() / 1024.0);
  printf("  Largest Free Block:  %u B (%.1f KB)\n", ESP.getMaxAllocPsram(),
         ESP.getMaxAllocPsram() / 1024.0);
  printf("------------------------------------------\n");
#endif
}

#if defined(ARDUINO) && defined(USING_IR_REMOTE)
IRsend irsend(IR_SEND); // T-Watch S3 GPIO2 pin to use.
#endif

#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
IRrecv irrecv(IR_SEND); // T-Watch S3 GPIO15 pin to use.
#endif

#if defined(ARDUINO) && defined(USING_IR_REMOTE)
static void ir_ensure_begin() {
  static bool isBegin = false;
  if (!isBegin) {
    isBegin = true;
    irsend.begin();
  }
}
#endif

void hw_set_remote_code(uint32_t nec_code) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  ir_ensure_begin();
  irsend.sendNEC(nec_code);
#endif
}

void hw_ir_send_raw(const uint16_t *data, uint16_t len, uint16_t khz) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  ir_ensure_begin();
  irsend.sendRaw(data, len, khz);
#endif
}

// --- AC "Super Turn On" -----------------------------------------------------
// Brute-force a power-on across the protocols Brazilian split ACs commonly
// rebrand from (Consul=Kelvinator, Elgin=Gree, Springer=Midea,
// Philco=TCL112...). Uses IRremoteESP8266's unified IRac so one path covers
// every vendor. The UI steps through these one at a time showing the name, so
// the user can see which protocol actually turned the unit on. Order kept in
// sync with the decode_type_t list in hw_ac_blast_send_on(). Many brands span
// several protocol families (Daikin, LG, TCL), so they get an entry each. Names
// hint the common Brazilian retail brands per family.
static const char *s_ac_blast_names[] = {
    "Midea/Springer/Komeco",
    "Coolix",
    "Gree/Elgin",
    "Kelvinator/Consul",
    "TCL112/Philco",
    "TCL96/Electrolux",
    "Neoclima/Komeco",
    "Whirlpool/Consul",
    "Samsung WindFree",
    "LG",
    "LG Dual Inverter",
    "Daikin",
    "Daikin2",
    "Daikin128",
    "Daikin152",
    "Daikin160",
    "Daikin176",
    "Fujitsu",
    "Electra",
    "Toshiba",
    "Carrier",
    "Hitachi",
    "Panasonic",
    "Haier",
    "Mitsubishi",
};
#define AC_BLAST_N (sizeof(s_ac_blast_names) / sizeof(s_ac_blast_names[0]))

uint8_t hw_ac_blast_count() { return (uint8_t)AC_BLAST_N; }

const char *hw_ac_blast_name(uint8_t i) {
  return (i < AC_BLAST_N) ? s_ac_blast_names[i] : "";
}

void hw_ac_blast_send_on(uint8_t i) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  // Same order and length as s_ac_blast_names.
  static const decode_type_t protos[] = {
      MIDEA,      COOLIX,     GREE,         KELVINATOR, TCL112AC,
      TCL96AC,    NEOCLIMA,   WHIRLPOOL_AC, SAMSUNG_AC, LG,
      LG2,        DAIKIN,     DAIKIN2,      DAIKIN128,  DAIKIN152,
      DAIKIN160,  DAIKIN176,  FUJITSU_AC,   ELECTRA_AC, TOSHIBA_AC,
      CARRIER_AC, HITACHI_AC, PANASONIC_AC, HAIER_AC,   MITSUBISHI_AC,
  };
  if (i >= AC_BLAST_N || !IRac::isProtocolSupported(protos[i])) {
    return;
  }
  ir_ensure_begin();
  static IRac irac(IR_SEND);
  irac.next.protocol = protos[i];
  irac.next.model = -1;
  irac.next.power = true;
  irac.next.mode = stdAc::opmode_t::kCool;
  irac.next.celsius = true;
  irac.next.degrees = 24;
  irac.next.fanspeed = stdAc::fanspeed_t::kAuto;
  irac.next.swingv = stdAc::swingv_t::kOff;
  irac.next.swingh = stdAc::swingh_t::kOff;
  irac.next.light = false;
  irac.next.beep = false;
  irac.next.econo = false;
  irac.next.filter = false;
  irac.next.turbo = false;
  irac.next.quiet = false;
  irac.next.clean = false;
  irac.next.sleep = -1;
  irac.next.clock = -1;
  irac.sendAc();
#else
  (void)i;
#endif
}

// mode/fan: AcMode/AcFan de ir_control.h (COOL=0,HEAT=1,FAN=2 /
// LOW=0,MID=1,HIGH=2,AUTO=3).
void hw_ac_electra_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRElectraAc ac(IR_SEND);
  static bool began = false;
  if (!began) {
    began = true;
    ac.begin();
  }

  ac.setPower(power);
  ac.setTemp(temp);

  uint8_t m;
  switch (mode) {
  case 1:
    m = kElectraAcHeat;
    break; // ACM_HEAT
  case 2:
    m = kElectraAcFan;
    break; // ACM_FAN
  default:
    m = kElectraAcCool; // ACM_COOL
  }
  ac.setMode(m);

  uint8_t f;
  switch (fan) {
  case 0:
    f = kElectraAcFanLow;
    break; // ACF_LOW
  case 1:
    f = kElectraAcFanMed;
    break; // ACF_MID
  case 2:
    f = kElectraAcFanHigh;
    break; // ACF_HIGH
  default:
    f = kElectraAcFanAuto; // ACF_AUTO
  }
  ac.setFan(f);

  ac.send();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
#endif
}

void hw_ac_midea_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan,
                      bool swing_toggle) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRac irac(IR_SEND);

  stdAc::opmode_t m;
  switch (mode) {
  case 1:
    m = stdAc::opmode_t::kHeat;
    break; // ACM_HEAT
  case 2:
    m = stdAc::opmode_t::kFan;
    break; // ACM_FAN
  default:
    m = stdAc::opmode_t::kCool; // ACM_COOL
  }

  stdAc::fanspeed_t f;
  switch (fan) {
  case 0:
    f = stdAc::fanspeed_t::kLow;
    break; // ACF_LOW
  case 1:
    f = stdAc::fanspeed_t::kMedium;
    break; // ACF_MID
  case 2:
    f = stdAc::fanspeed_t::kHigh;
    break; // ACF_HIGH
  default:
    f = stdAc::fanspeed_t::kAuto; // ACF_AUTO
  }

  ir_ensure_begin();
  irac.next.protocol = MIDEA;
  irac.next.model = -1;
  irac.next.power = power;
  irac.next.mode = m;
  irac.next.celsius = true;
  irac.next.degrees = temp;
  irac.next.fanspeed = f;
  irac.next.swingv =
      swing_toggle ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  irac.next.swingh = stdAc::swingh_t::kOff;
  irac.next.light = false;
  irac.next.beep = false;
  irac.next.econo = false;
  irac.next.filter = false;
  irac.next.turbo = false;
  irac.next.quiet = false;
  irac.next.clean = false;
  irac.next.sleep = -1;
  irac.next.clock = -1;
  irac.sendAc();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
  (void)swing_toggle;
#endif
}

// Coolix: muitos splits BR (controles tipo R05/R06 branded Midea/Springer/
// Komeco/Elgin) na verdade falam COOLIX no ar. Confirmado no hardware isolando
// o Super Turn On: so o Test 1 (Coolix) ligou o aparelho. Mesmo backend IRac.
void hw_ac_coolix_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan,
                       bool swing_toggle) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRac irac(IR_SEND);

  stdAc::opmode_t m;
  switch (mode) {
  case 1:
    m = stdAc::opmode_t::kHeat;
    break; // ACM_HEAT
  case 2:
    m = stdAc::opmode_t::kFan;
    break; // ACM_FAN
  default:
    m = stdAc::opmode_t::kCool; // ACM_COOL
  }

  stdAc::fanspeed_t f;
  switch (fan) {
  case 0:
    f = stdAc::fanspeed_t::kLow;
    break; // ACF_LOW
  case 1:
    f = stdAc::fanspeed_t::kMedium;
    break; // ACF_MID
  case 2:
    f = stdAc::fanspeed_t::kHigh;
    break; // ACF_HIGH
  default:
    f = stdAc::fanspeed_t::kAuto; // ACF_AUTO
  }

  ir_ensure_begin();
  irac.next.protocol = COOLIX;
  irac.next.model = -1;
  irac.next.power = power;
  irac.next.mode = m;
  irac.next.celsius = true;
  irac.next.degrees = temp;
  irac.next.fanspeed = f;
  irac.next.swingv =
      swing_toggle ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  irac.next.swingh = stdAc::swingh_t::kOff;
  irac.next.light = false;
  irac.next.beep = false;
  irac.next.econo = false;
  irac.next.filter = false;
  irac.next.turbo = false;
  irac.next.quiet = false;
  irac.next.clean = false;
  irac.next.sleep = -1;
  irac.next.clock = -1;
  irac.sendAc();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
  (void)swing_toggle;
#endif
}

void hw_ac_lg_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan,
                   bool swing, bool swing_toggle) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRLgAc ac(IR_SEND);
  static bool began = false;
  if (!began) {
    began = true;
    ac.begin();
    ac.setModel(
        AKB74955603); // LG2 split AC variant common in IRremoteESP8266 notes.
  }

  ac.setPower(power);
  ac.setTemp(temp);

  uint8_t m;
  switch (mode) {
  case 1:
    m = kLgAcHeat;
    break; // ACM_HEAT
  case 2:
    m = kLgAcFan;
    break; // ACM_FAN
  default:
    m = kLgAcCool; // ACM_COOL
  }
  ac.setMode(m);

  uint8_t f;
  switch (fan) {
  case 0:
    f = kLgAcFanLow;
    break; // ACF_LOW
  case 1:
    f = kLgAcFanMedium;
    break; // ACF_MID
  case 2:
    f = kLgAcFanHigh;
    break; // ACF_HIGH
  default:
    f = kLgAcFanAuto; // ACF_AUTO
  }
  ac.setFan(f);

  if (swing_toggle) {
    ac.setSwingV(swing ? kLgAcSwingVAuto : kLgAcSwingVOff);
  }
  ac.send();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
  (void)swing;
  (void)swing_toggle;
#endif
}

void hw_ac_samsung_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan,
                        bool swing) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRSamsungAc ac(IR_SEND);
  static bool began = false;
  if (!began) {
    began = true;
    ac.begin();
  }

  ac.setPower(power);
  ac.setTemp(temp);

  uint8_t m;
  switch (mode) {
  case 1:
    m = kSamsungAcHeat;
    break; // ACM_HEAT
  case 2:
    m = kSamsungAcFan;
    break; // ACM_FAN
  default:
    m = kSamsungAcCool; // ACM_COOL
  }
  ac.setMode(m);

  uint8_t f;
  switch (fan) {
  case 0:
    f = kSamsungAcFanLow;
    break; // ACF_LOW
  case 1:
    f = kSamsungAcFanMed;
    break; // ACF_MID
  case 2:
    f = kSamsungAcFanHigh;
    break; // ACF_HIGH
  default:
    f = kSamsungAcFanAuto; // ACF_AUTO
  }
  ac.setFan(f);
  ac.setSwing(swing);
  ac.send();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
  (void)swing;
#endif
}

void hw_ac_toshiba_send(bool power, uint8_t temp, uint8_t mode, uint8_t fan,
                        bool swing, bool swing_toggle) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
  static IRToshibaAC ac(IR_SEND);
  static bool began = false;
  if (!began) {
    began = true;
    ac.begin();
  }

  uint8_t m;
  switch (mode) {
  case 1:
    m = kToshibaAcHeat;
    break; // ACM_HEAT
  case 2:
    m = kToshibaAcFan;
    break; // ACM_FAN
  default:
    m = kToshibaAcCool; // ACM_COOL
  }
  ac.setMode(m);
  ac.setPower(power);
  ac.setTemp(temp);

  uint8_t f;
  switch (fan) {
  case 0:
    f = kToshibaAcFanMin;
    break; // ACF_LOW
  case 1:
    f = kToshibaAcFanMed;
    break; // ACF_MID
  case 2:
    f = kToshibaAcFanMax;
    break; // ACF_HIGH
  default:
    f = kToshibaAcFanAuto; // ACF_AUTO
  }
  ac.setFan(f);

  if (swing_toggle) {
    ac.setSwing(swing ? kToshibaAcSwingOn : kToshibaAcSwingOff);
  }
  ac.send();
#else
  (void)power;
  (void)temp;
  (void)mode;
  (void)fan;
  (void)swing;
  (void)swing_toggle;
#endif
}

#if defined(ARDUINO)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static int http_get_with_client(const char *url, char *out, uint32_t out_size,
                                WiFiClient &client) {
  HTTPClient http;
  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setUserAgent("twatch-s3");
  if (!http.begin(client, url))
    return -1;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return -1;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint32_t n = 0;
  uint32_t deadline = millis() + 12000;
  while (http.connected() && n < out_size - 1 &&
         (int32_t)(deadline - millis()) > 0) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = (out_size - 1) - n;
      size_t take = avail < want ? avail : want;
      int r = stream->readBytes(out + n, take);
      if (r > 0) {
        n += (uint32_t)r;
        deadline = millis() + 3000;
      }
    } else {
      delay(5);
    }
  }
  out[n] = 0;
  http.end();
  return (int)n;
}
#endif

void hw_rtc_sync_build(const char *date_str, const char *time_str) {
#ifdef ARDUINO
  // date_str = "Mmm dd yyyy", time_str = "hh:mm:ss" (PC local time at compile).
  char buildkey[40];
  snprintf(buildkey, sizeof(buildkey), "%s %s", date_str, time_str);

  Preferences prefs;
  if (!prefs.begin("clocksync", false)) {
    log_e("Failed to open clock sync NVS");
    return;
  }
  String last = prefs.getString("b", "");
  if (last != String(buildkey)) {
    char mon[4] = {0};
    int day = 1, year = 2025, hh = 0, mm = 0, ss = 0;
    sscanf(date_str, "%3s %d %d", mon, &day, &year);
    sscanf(time_str, "%d:%d:%d", &hh, &mm, &ss);
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *mp = strstr(months, mon);
    int mon_idx = mp ? (int)((mp - months) / 3) : 0;

    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = mon_idx;
    t.tm_mday = day;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    instance.rtc.setDateTime(t);
    instance.rtc.hwClockRead();
    if (prefs.putString("b", buildkey) == 0) {
      log_e("Failed to save clock sync marker");
    }
  }
  prefs.end();
#else
  (void)date_str;
  (void)time_str;
#endif
}

int hw_http_get(const char *url, char *out, uint32_t out_size) {
#if defined(ARDUINO)
  if (out == NULL || out_size == 0)
    return -1;
  out[0] = 0;
  if (WiFi.status() != WL_CONNECTED)
    return -1;

  if (strncmp(url, "https://", 8) == 0) {
    WiFiClientSecure secure_client;
    secure_client.setInsecure(); // skip certificate validation
    return http_get_with_client(url, out, out_size, secure_client);
  }

  WiFiClient plain_client;
  return http_get_with_client(url, out, out_size, plain_client);
#else
  (void)url;
  (void)out;
  (void)out_size;
  return -1;
#endif
}

void hw_get_remote_code(uint64_t &result) {
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
  decode_results results;
  if (irrecv.decode(&results)) {
    Serial.print("IR Code received: ");
    Serial.println(results.value, HEX);
    result = results.value;
    irrecv.resume(); // Receive the next value
  }
#else
  result = random(0, INT_MAX);
#endif
}

void hw_ir_function_select(bool enableSend) {
#if defined(ARDUINO) && defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
  if (enableSend) {
    instance.IRFunctionSelect(IR_FUNC_SENDER);
    irrecv.disableIRIn();
  } else {
    instance.IRFunctionSelect(IR_FUNC_RECEIVER);
    irrecv.enableIRIn();
  }
#endif
}

#ifdef USING_BME280

void hw_bme_enable(bool enable) {
#ifdef ARDUINO
  if (enable) {
    instance.bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                             Adafruit_BME280::SAMPLING_X1, // temperature
                             Adafruit_BME280::SAMPLING_X1, // pressure
                             Adafruit_BME280::SAMPLING_X1, // humidity
                             Adafruit_BME280::FILTER_X2);
  } else {
    instance.bme.setSampling(Adafruit_BME280::MODE_SLEEP);
  }
#endif
}

void hw_bme_get_data(float &temp, float &humi, float &press, float &alt) {
#ifdef ARDUINO
  temp = instance.bme.readTemperature();
  humi = instance.bme.readHumidity();
  press = instance.bme.readPressure() / 100.0F;
  alt = instance.bme.readAltitude(1013.25);

#else
  temp = random(0, 25);
  humi = random(40, 95);
  press = random(1000, 1200);
  alt = random(20, 60);
#endif
}

#endif /*USING_BME280*/

using TrackballEventCallback = void (*)(uint8_t dir);
using ButtonEventCallback = void (*)(uint8_t idx, uint8_t state);

#if defined(ARDUINO) && defined(USING_TRACKBALL)

static TrackballEventCallback _trackball_cb = NULL;
static ButtonEventCallback _button_cb = NULL;

static void trackballEventCallback(DeviceEvent_t event, void *params,
                                   void *user_data) {
  if (_trackball_cb && params) {
    TrackballDir_t dir = *(static_cast<TrackballDir_t *>(params));
    _trackball_cb(dir);
  }
}

static void buttonEventCallback(DeviceEvent_t event, void *params,
                                void *user_data) {
  if (_button_cb && params) {
    ButtonEventParam_t *p = static_cast<ButtonEventParam_t *>(params);
    _button_cb(p->id, p->event);
  }
}

#endif

void hw_set_trackball_callback(TrackballEventCallback callback) {
#if defined(ARDUINO) && defined(USING_TRACKBALL)
  // instance.setTrackballCallback(callback);
  if (callback) {
    instance.onEvent(trackballEventCallback, TRACKBALL_EVENT, NULL);
    _trackball_cb = callback;
  } else {
    instance.removeEvent(trackballEventCallback, TRACKBALL_EVENT);
    _trackball_cb = NULL;
  }
#endif
}

void hw_set_button_callback(ButtonEventCallback callback) {
#if defined(ARDUINO) && defined(USING_TRACKBALL)
  if (callback) {
    instance.onEvent(buttonEventCallback, BUTTON_EVENT, NULL);
    _button_cb = callback;
  } else {
    instance.removeEvent(buttonEventCallback, BUTTON_EVENT);
    _button_cb = NULL;
  }
#endif
}

const char *hw_get_device_power_tips_string() {
#if defined(USING_PPM_MANAGE)
  return "Select a shutdown method:\n"
         "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
         "2. Shutdown: Turn off the device (requires removing the USB-C port "
         "to shut down).\n"
         "After shutting down, press and hold the Power button or plug in a "
         "USB-C port to activate the device.";
#else
  return "Select a shutdown method:\n"
         "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
         "2. Shutdown: Turn off the device. After shutting down, press and "
         "hold the Power button or plug in a\n"
         "USB-C cable to activate the device.";
#endif
}

const char *hw_get_firmware_hash_string() {
#ifdef ARDUINO
  static char hash_string[33] = {0};
  snprintf(hash_string, sizeof(hash_string), "%s", ESP.getSketchMD5().c_str());
  return hash_string;
#else
  return "DummyHashString";
#endif
}

const char *hw_get_chip_id_string() {
#ifdef ARDUINO
  static char chipid[13] = {0};
  uint64_t chipmacid = 0LL;
  esp_efuse_mac_get_default((uint8_t *)(&chipmacid));
  snprintf(chipid, sizeof(chipid), "%04X%08X", (uint16_t)(chipmacid >> 32),
           (uint32_t)(chipmacid));
  return chipid;
#endif
  return "DummyChipIDString";
}

void hw_set_usb_rf_switch(bool to_usb) {
#ifdef ARDUINO
#if defined(HAS_USB_RF_SWITCH)
  instance.setRFSwitch(to_usb);
#endif
#endif
}

void hw_set_audio_effect_3d(bool enable) {
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
  instance.setAudioEffect3D(enable);
#endif
}

void hw_set_audio_effect_ab_class(bool enable) {
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
  if (enable) {
    instance.setAudioMode(AUDIO_CLASS_AB);
  } else {
    instance.setAudioMode(AUDIO_CLASS_D);
  }
#endif
}
