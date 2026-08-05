# Arquitetura — mapa de arquivos

Que arquivo cobre que parte do software. Firmware T-Watch S3 (LVGL v9 +
LilyGoLib, ESP32-S3/Arduino). Complementa `CHANGES.md` (o que mudou) e
`DECISIONS.md` (porquê). Board: FQBN `esp32:esp32:twatchs3`, painel ST7789.

## Camadas (visão rápida)

```
factory.ino / main.cpp        boot + loop LVGL
        │
ui_main.cpp                    launcher (tileview HOME↔GRID↔APP) + idle/sleep
        │  cada app = app_t {setup_func_cb, exit_func_cb}
ui_*.cpp                       telas/apps (uma por arquivo)
        │  chamam hw_*()
hal_interface.cpp/.h           HAL — TODA a fala com o hardware (fachada hw_*)
        │
LilyGoLib (instance.*)         driver do vendor (PMU/codec/mic/RTC/display/rádios)
hw_*.cpp                       drivers de rádio extra (SX/CC/NRF/LR)
```

Regra de ouro: **UI nunca fala com hardware direto** — só via `hw_*()` do
`hal_interface`. Por isso o simulador PC compila as mesmas `ui_*.cpp` com stubs.

---

## Núcleo / boot

| Arquivo | Cobre |
|---|---|
| `factory.ino` | Sketch Arduino. `setup()` (init HAL, NTP, sync RTC, power-key, detecção de boot-por-alarme → `ui_alarm_ring`), `loop()` = `lv_timer_handler()`. |
| `main.cpp` | Bootstrap comum (setup/loop chamados pelo runtime); ponto de entrada compartilhado com o sim. |
| `hal_interface.h` | **Contrato do HAL**: todas as decls `hw_*` + tipos (`app_t`, `audio_source_type_t`, params). O header que a UI inclui. |
| `hal_interface.cpp` | **Implementação do HAL** (~3.8k linhas): display/brilho, PMU/bateria/carga, sleep (`hw_low_power_loop`, `hw_light_sleep_timed`), áudio (player MP3/WAV, `playerTask`, ganho de software `hw_set_audio_gain`), **recorder** (`recordTask`, PDM mic→WAV FFat), **share** (`hw_share_*`: SoftAP + WebServer + Basic auth), HTTP (`hw_http_get`: HTTP sem TLS, HTTPS com `WiFiClientSecure`), RTC/alarme normal one-shot (`hw_rtc_set_alarm`, `hw_enter_alarm_sleep`, `hw_alarm_ringing` trava o sleep durante o ring), **IR AC blast** (`hw_ac_blast_*` via `IRac`), WiFi, IR, vibração, sensores. No sim: corpos sob `#ifdef ARDUINO` viram stubs `#else`. |

## UI — framework / launcher

| Arquivo | Cobre |
|---|---|
| `ui_main.cpp` | Launcher (tileview 3 tiles: HOME relógio+wallpaper ↔ GRID grade de apps ↔ APP), `create_app_cell`, `menu_show/hidden`, screensaver, **idle/sleep** (`ui_poll_timer_callback`: timeout 5s→screensaver→light-sleep). |
| `ui_define.h` | Tipos/macros compartilhados da UI, `app_t`, helpers de tela, protótipos das telas. |
| `ui_theme.cpp` | Tema/estilos LVGL (cores, fontes) global. |
| `ui_factory.cpp` | Tela de teste de fábrica (varredura de HW). |

## Apps — sistema / relógio

| Arquivo | App |
|---|---|
| `ui_clock.cpp` | Relógio: cronômetro, countdown, **alarme normal one-shot** (rollers + deep-sleep até RTC_INT, dropdown de som, ring overlay `ui_alarm_ring`). |
| `ui_calendar.cpp` | Calendário (data viva, ícone v9). |
| `ui_power.cpp` | Bateria/energia + overlay de confirmação de power-off (long-press). |
| `ui_sys.cpp` | Infos de sistema/configuração. |
| `ui_sensor.cpp` | Leitura de sensores (IMU/temperatura BMA423). |
| `ui_pedometer.cpp` | Pedômetro (step-counter on-chip BMA423, low-power). |
| `ui_monitor.cpp` | Monitor de recursos/status. |
| `ui_tools.cpp` | Utilitários diversos. |

## Apps — áudio

| Arquivo | App |
|---|---|
| `ui_audio.cpp` | Player de música (lista de MP3 do FS, slider de volume — **inerte neste board**, ver [[fact-audio-hardware-max98357]]/`DECISIONS.md` §3). |
| `ui_recorder.cpp` | **Gravador de voz**: Record/Stop, timer, lista (play + delete). Grava WAV 16kHz mono no FFat via `recordTask`; replay reusa o player. |
| `ui_microphone.cpp` | Visualização do mic (FFT/nível), não gravação. |

## Apps — comunicação / rádio

| Arquivo | App |
|---|---|
| `ui_wireless.cpp` | WiFi: scan, salvar rede (NVS), conectar (WiFiMulti), power-off. Sub-páginas Weather/News. |
| `ui_share.cpp` | **Share**: sobe SoftAP `TWatch-Share` (WPA2, senha aleatória) + WebServer com HTTP Basic auth numa task; página web lista as gravações e serve download. Extrai os WAV do FFat (sem SD/USB). HAL `hw_share_*`. |
| `ui_weather.cpp` | Weather (Open-Meteo via HTTP) + Tech News (Tom's Hardware RSS) — task de fetch assíncrona, parser simples de JSON/RSS, attach nas sub-páginas do WiFi. |
| `ui_ble.cpp` / `ui_ble_kb.cpp` | Bluetooth / teclado BLE. |
| `ui_msg.cpp` / `ui_msgchat.cpp` | Mensagens / chat. |
| `ui_ir_remote.cpp` | Controle IR universal (TV/AC) — usa `ir_control.h`. |
| `ui_radio.cpp` | Rádio (SX1262 LoRa / SI4735 FM, conforme HW). |
| `ui_nrf24.cpp` | Interface NRF24. |
| `ui_camera_remote.cpp` | Disparo remoto de câmera (via BLE HID). |
| `ui_nfc.cpp` | NFC (ST25R3916) — **fora do launcher** por escolha ([[project-nfc-deactivated]]). |
| `ui_keyboard.cpp` | Teclado on-screen. |

## Drivers de baixo nível

| Arquivo | Cobre |
|---|---|
| `ir_control.h` | Hierarquia de classes IR: TV (NEC/Roku + RCA p/ TCL Android) + AC (Electra/Midea/LG/Samsung/Toshiba) + registries. "Super Turn On" de AC (`hw_ac_blast_*`) brute-forcea 25 famílias de protocolo. Detalhes em `docs/IR_CONTROL.md` + `docs/DECISIONS.md` §1. |
| `event_define.h` | Enums de evento de app + structs NDEF (NFC). |
| `app_nfc.cpp/.h` | Reader NFC (beginNFC/loop/deinit, callbacks NDEF). |
| `hw_sx1262.cpp` `hw_sx1280.cpp` `hw_lr1121.cpp` `hw_cc1101.cpp` `hw_nrf2401.cpp` | Drivers dos chips de rádio (RadioLib). **Não** compilados no sim. |

## Simulador PC

| Arquivo | Cobre |
|---|---|
| `sim/Makefile` | Build do sim (g++ + SDL2). Compila `ui_*` + `hal_interface.cpp` (stubs) + `stubs.cpp`; **não** os `hw_*` de rádio. |
| `sim/stubs.cpp` | Stubs de HW que faltam pro link no PC. |
| `sim/compat.h` | Shim Arduino→PC (tipos, `String`, etc.). |
| `sim/lv_conf.h` | Config LVGL do sim. |

Build/smoke: `make -C sim -j$(nproc)` → `SDL_VIDEODRIVER=dummy timeout 8 ./sim/sim` (exit 124 = ok).

## Assets

| Caminho | Cobre |
|---|---|
| `src/img_*.c` | Ícones/wallpapers convertidos pra LVGL (v9). Receita imagem→asset: [[reference-lvgl-image-from-wallpaper]]. |
| `src/font/*.c` | Fontes embutidas (alibaba/logo, vários tamanhos). |

## Tooling / docs

| Caminho | Cobre |
|---|---|
| `scripts/quality_report.sh` | cppcheck + lizard (CCN 15/len 80) + smoke opcional. |
| `scripts/perf_report.sh` | Hotspots lizard + size .elf/sim. |
| `scripts/gen_docs.sh` | Gera dot + doxygen (`docs/doxygen/`, gitignored). |
| `docs/CHANGES.md` | O que foi tocado por branch. |
| `docs/DECISIONS.md` | Decisões e armadilhas (IR, energia, RTC, áudio, alarme…). |
| `docs/IR_CONTROL.md` | Detalhe do subsistema IR. |
| `docs/power_up.{dot,svg,png}` | Mapa de power-up no boot. |

---

## Como achar onde mexer

- **Comportamento de uma tela/app** → `ui_<nome>.cpp`.
- **Qualquer coisa que toca hardware** (áudio, sleep, bateria, WiFi, RTC, mic) →
  `hal_interface.cpp` (procure o `hw_<coisa>`), decl em `hal_interface.h`.
- **Registro de app no launcher** → `ui_main.cpp` (`create_app_cell`).
- **Código IR** → `ir_control.h` + `ui_ir_remote.cpp`.
- **Só roda no HW, não no sim** → dentro de `#ifdef ARDUINO` (HAL) ou nos
  `hw_*.cpp` de rádio.
