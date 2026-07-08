/**
 * @file      event_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-19
 *
 */
#pragma once

#include <cstdint>

/**
 * @brief Event kinds carried on the inter-task application event queue.
 */
enum app_event : uint8_t {
    APP_EVENT_PLAY,      ///< Start/continue audio playback of a file.
    APP_EVENT_PLAY_KEY,  ///< UI key press routed to the active player.
    APP_EVENT_RECOVER,   ///< Restore playback state after an interruption.
    APP_NFC_EVENT,       ///< An NFC tag was read; payload in nfcData_t.
};


#if defined(ARDUINO) && defined(USING_ST25R3916)
#include <LilyGoLib.h>

/** @brief Decoded NDEF URI record: protocol prefix + URI body. */
typedef struct {
    ndefConstBuffer bufProtocol;   ///< URI abbreviation prefix (e.g. "https://").
    ndefConstBuffer bufUriString;  ///< Remainder of the URI.
} ndefTypeURL;

/** @brief Decoded NDEF text record. */
typedef struct {
    uint8_t utfEncoding;            ///< 0 = UTF-8, 1 = UTF-16.
    ndefConstBuffer bufLanguageCode;///< IANA language code (e.g. "en").
    ndefConstBuffer bufSentence;    ///< The text payload.
} ndefTypeText;

/** @brief One decoded NDEF record, tagged by @ref nfcData_t::event. */
typedef struct {
    ndefTypeId event;               ///< Record type discriminator.
    union __ {
        ndefTypeWifi wifiConfig;    ///< Wi-Fi credential handover record.
        ndefTypeURL url;            ///< URI record.
        ndefTypeText text;          ///< Text record.
        ndefTypeRtdDeviceInfo devInfoData; ///< Device-info record.
    } data;                         ///< Active member selected by @ref event.
} nfcData_t;

/** @brief Application event queue item; @ref event selects the union member. */
typedef struct {
    enum app_event event;           ///< Event kind.
    union __ {
        nfcData_t nfc;              ///< Payload for APP_NFC_EVENT.
    } u;
} app_event_t;

/** @brief Audio-playback request passed to the player task. */
typedef struct {
    enum app_event event;           ///< APP_EVENT_PLAY / _PLAY_KEY / _RECOVER.
    const char *filename ;          ///< File to play (NULL for key/recover events).
} app_audio_play_t;

#endif /*ARDUINO*/
