/**
 * @file      app_nfc.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-11
 *
 */

 #pragma once

 #if defined(ARDUINO) 

 #include <Arduino.h>

 #if defined(USING_ST25R3916)

 #include "nfc_include.h"

 /** @brief Decoded NDEF RTD text record. */
 typedef struct _ndefRtdText {
     uint8_t utfEncoding;          ///< 0 = UTF-8, 1 = UTF-16.
     ndefConstBuffer8 bufLanguageCode; ///< IANA language code (e.g. "en").
     ndefConstBuffer  bufSentence; ///< The text payload.
 } ndefRtdText;

 /** @brief Decoded NDEF RTD URI record. */
 typedef struct _RtdUri {
     ndefConstBuffer bufProtocol;  ///< URI abbreviation prefix (e.g. "https://").
     ndefConstBuffer bufUriString; ///< Remainder of the URI.
 } ndefRtdUri;

 /** @brief Called when an NFC tag enters/leaves the field (woken from poll). */
 typedef void (*notify_callback_t)();
 /** @brief Called per decoded NDEF record; @p data points at the typed payload. */
 typedef void (*ndef_event_callback_t)(ndefTypeId id, void*data);

 /**
  * @brief Initialise the ST25R3916 NFC reader and register callbacks.
  * @param notify_cb  Field-presence notifier (may be NULL).
  * @param event_cb   Per-record decode callback (may be NULL).
  * @return true on successful reader bring-up.
  */
 bool beginNFC(notify_callback_t notify_cb, ndef_event_callback_t event_cb);
 /** @brief Poll the reader once and dispatch any decoded records; call in a loop. */
 void loopNFCReader();
 /** @brief Power down the NFC reader and release its resources. */
 void deinitNFC();

 extern RfalNfcClass NFCReader; ///< Shared RFAL NFC reader instance.
 
 #endif

 
 #endif /*ARDUINO*/
 