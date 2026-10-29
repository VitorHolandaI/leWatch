/**
 * @file      wifi_deauth_bypass.cpp
 * @brief     Libera deauth/disassoc no esp_wifi_80211_tx do ESP32.
 *
 * O blob fechado do WiFi (libnet80211.a) exporta
 * ieee80211_raw_frame_sanity_check(), que recusa frames de gerenciamento
 * "nao permitidos" — deauth (0xC0) e disassoc (0xA0) entre eles. Quando ela
 * recusa, o esp_wifi_80211_tx() ainda devolve ESP_OK e o frame simplesmente
 * nao sai no ar: nada no serial denuncia o problema. Verificado no toolchain
 * deste projeto (esp32 core 3.3.10, lib esp32s3-libs 3.3.10, simbolo
 * "T ieee80211_raw_frame_sanity_check").
 *
 * Mesma tecnica do justcallmekoko/ESP32Marauder e do
 * tesa-klebeband/ESP32-Deauther: definimos o simbolo devolvendo 0 (ESP_OK).
 * O link precisa de -Wl,--allow-multiple-definition (flash.sh ja passa); sem
 * isso o linker acusa "multiple definition of ieee80211_raw_frame_sanity_check".
 */
#include <stdint.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3)
{
    (void)arg;
    (void)arg2;
    (void)arg3;
    return 0;
}
