# Arquivos tocados nesta sessão

Visão do que foi **realmente mexido** vs intocado. `NOVO` = arquivo criado;
`MOD` = modificado.

## Branch `home-redesign` — novo launcher

| Arquivo | | O que mudou |
|---|---|---|
| `ui_main.cpp` | MOD | Launcher reescrito: tileview horizontal de 3 tiles — **HOME** (relógio grande + fundo, sempre visível) ↔ **GRID** (grade rolável de mini-apps: ícone reusado + nome) → **APP** (container). Carrossel removido (`create_app`, `launcher_nav_cb`, `scrollbar_change_cb`, `desc_label`, relógio do topo). `setupClock` refatorado em `build_clock_face`+`style_clock_bg`, reusado por screensaver e home (`build_home_clock`). `create_app_cell` substitui `create_app`. `menu_show`/`menu_hidden` apontam pros novos tiles. Power path/screensaver intactos. |

Back: já unificado (back flutuante do `create_menu` / `create_floating_button`); home não tem back, grade volta por swipe à direita.

## Branch `wifi-networks` — redes salvas + WiFiMulti

| Arquivo | | O que mudou |
|---|---|---|
| `hal_interface.*` | MOD | WiFiMulti + redes salvas em NVS (namespace `wifinets`): `hw_wifi_saved_add/remove/count/get`, `hw_wifi_multi_connect` (conecta na salva de melhor sinal), `hw_wifi_off`. Store em memória no sim. WiFi off por padrão. |
| `ui_wireless.cpp` | MOD | 4 botões: scan, **Salvar** (grava SSID+senha no NVS), **Conectar** (WiFiMulti nas salvas), **Power** (desliga WiFi). Sub-página **Saved networks** (listar/remover). |

- Senhas ficam só no NVS (nunca no git). Weather/News funcionam após conectar.
- `hw_wifi_multi_connect` bloqueia até ~10s (scan+connect) — melhorar p/ assíncrono depois.

## Branch `alarm-mode` — som + vibração máxima no alarme

| Arquivo | | O que mudou |
|---|---|---|
| `hal_interface.*` | MOD | `hw_vibrate_max` (DRV2605 effect 47 = buzz 100%); som do alarme salvo no NVS (`hw_alarm_sound_set/get`, namespace `alarm`). |
| `ui_clock.cpp` | MOD | Seção Alarme ganha **dropdown de som** (lista de `hw_get_filesystem_music`, escolha gravada no NVS, pré-seleciona a salva). `ui_alarm_ring` toca o som escolhido (loop enquanto não dispensar) + vibra no **máximo** a cada 600ms; OK para o som. |

- NVS sobrevive ao deep-sleep do MODO ALARME → som escolhido toca no boot do alarme.
- Nota: `setHapticEffects(47)` deixa o vibrador forte até outro effect ser setado.

## Branch `sleep-alarm-fixes` — sono acorda só no power key + alarme não corta som + alarme mais alto

| Arquivo | | O que mudou |
|---|---|---|
| `hal_interface.*` | MOD | **(1)** Idle light-sleep agora acorda **só no power key** (toque não acorda mais): `hw_low_power_loop`, `power_key_light_sleep` e `hw_light_sleep_timed` passam `WAKEUP_SRC_POWER_KEY` (removido `WAKEUP_SRC_TOUCH_PANEL` do default do vendor). **(2)** Flag `hw_set_alarm_ringing`/`hw_alarm_ringing` (bool comum, vale no sim). **(3)** Ganho de software de playback: `s_pcm_gain` + `apply_pcm_gain` (satura int16) aplicado em `mp3_write_output` e `playWAV` (ramo `USING_PCM_AMPLIFIER`); `hw_set_audio_gain(g)` (1 = normal). Necessário porque o MAX98357A tem ganho fixo em HW e **não** há volume de codec neste board. |
| `ui_main.cpp` | MOD | `ui_poll_timer_callback`: guarda do light-sleep ganha `&& !hw_alarm_ringing()` — enquanto o alarme toca não entra em light-sleep (que congelaria o `playerTask` e cortaria o som). Tela ainda pode ir pro screensaver; power-key acorda pra dispensar. |
| `ui_clock.cpp` | MOD | `#define ALARM_GAIN 4`. `ui_alarm_ring` marca `hw_set_alarm_ringing(true)` e seta `hw_set_audio_gain(ALARM_GAIN)` antes de tocar. `ring_dismiss_cb` limpa a flag + `hw_set_audio_gain(1)`. **Rollers do alarme = hora/min atuais** (`hw_get_date_time`) em vez de fixo 07:00. |
| `ui_share.cpp` | NOVO | App **Share**: `hw_share_start` sobe SoftAP `TWatch-Share` (WPA2, senha 8díg aleatória) + `WebServer` numa task FreeRTOS com HTTP Basic auth (usuário `share`, senha 8díg aleatória separada); página `/` lista as gravações, `/dl?f=` serve download. Mostra rede/senhas/IP; back encerra o AP. Extrai os WAV do FFat (board sem SD/USB). HAL: `hw_share_start/stop/active/info`. Célula no launcher (ícone `img_wifi`). |
| `hal_interface.*` | MOD | **(share)** ver acima. **(Weather/News)** `hw_http_get` usa `WiFiClient` para HTTP e só instancia `WiFiClientSecure` em HTTPS; requests usam HTTP/1.0 sem reuse. **(alarme normal)** mantém só `hw_rtc_set_alarm`/`hw_enter_alarm_sleep`/ring one-shot; alarme recorrente removido. **(IR AC normal)** Midea/Springer/Komeco agora usa `IRac` protocolo `MIDEA`, mesmo backend do primeiro Super Turn On que funcionou no HW. **(IR AC blast)** `hw_ac_blast_count/name/send_on` — "Super Turn On" via `IRac` unificado, dispara power-on de **25 famílias** de protocolo (Midea/Coolix/Gree/Kelvinator/TCL112/TCL96/Neoclima/Whirlpool/Samsung/LG/LG2/Daikin+variantes/Fujitsu/Electra/Toshiba/Carrier/Hitachi/Panasonic/Haier/Mitsubishi). |
| `ui_weather.cpp` | MOD | Bugfix de reboot ao abrir Weather/News após conectar WiFi: Open-Meteo em HTTP direto, News em Tom's Hardware RSS (`https://www.tomshardware.com/feeds.xml`), task de fetch 16KB→24KB, falha de `xTaskCreate` mostra erro, parser aceita CDATA e pontuação UTF-8 comum. GamersNexus testado mas descartado porque o RSS põe artigo inteiro antes do próximo item; com buffer 16KB quase só aparecia 1 manchete. |
| `ir_control.h` | MOD | **TV TCL Android** (`TclAndroidTV`): bytes RCA de nav estavam 0 (setas mortas) → preenchidos do Flipper-IRDB (RCA addr 0x0F): Power 0x54, Up 0x9A, Down 0x1A, Left 0x6A, Right 0xEA, OK 0x2F, Home 0x10, Vol± 0xF4/0x74, Mute 0xFC, source 0x58. Decls do AC blast. |
| `ui_ir_remote.cpp` | MOD | Botão **"Super Turn On"** + label na página AC: `lv_timer` passa pelas 25 famílias (1 por 1.3s) mostrando o nome; user vê qual liga o split. Timer parado no back/exit. |
| `docs/ARCHITECTURE.md` | NOVO | Mapa arquivo→subsistema (camadas, núcleo, HAL, apps por categoria, drivers, sim, assets, tooling). |
| `ui_alarm.cpp` | REM | Removido app **Alarme diário recorrente** e a opção "dormir após (h)"/default 23h. |
| `hal_interface.*` | MOD | Removidas APIs do alarme recorrente (`hw_alarm_cfg_*`, `hw_alarm_arm_recurring`, `hw_rtc_alarm_ack`, `hw_alarm_recurring_fired`, `hw_alarm_should_deep_sleep`). Mantido o alarme normal one-shot via RTC/deep-sleep. |
| `factory.ino` | MOD | Boot: `hw_rtc_clear_alarm()` + `ui_alarm_ring()` só quando acordou do alarme normal do Clock; sem rearm diário. |
| `ui_main.cpp` | MOD | Removido foreground ring/auto deep-sleep noturno do alarme recorrente e a célula "Alarm" no launcher. Mantém guarda `!hw_alarm_ringing()` para não cortar o alarme normal. **Colon `:` do relógio fixo** (era escondido/mostrado a cada 1s em `clock_update_datetime` = piscava; agora sempre visível). |

- **A validar em HW:** toque não acorda o relógio dormindo (só power key); alarme continua tocando ao passar o timeout de tela; alarme mais alto (clip no 4x); rollers abrem na hora atual; Share (conectar no AP + login HTTP + baixar WAV em http://192.168.4.1); Weather/News após conectar WiFi sem reboot; TCL Android setas/OK/Home; AC "Super Turn On" (qual família liga o split).

## Firmware — core / HAL

| Arquivo | | O que mudou |
|---|---|---|
| `factory.ino` | MOD | NTP BR (`a.st1.ntp.br`); `gmtOffset` virou `long`; chama `hw_rtc_sync_build(__DATE__,__TIME__)`; `hw_enable_power_key_toggle()`; detecção de boot por alarme (`hw_woke_from_alarm` → `ui_alarm_ring`) |
| `hal_interface.h` | MOD | `GMT_OFFSET_SECOND` +8→**-3**; decls: `hw_ir_send_raw`, `hw_http_get`, `hw_rtc_sync_build`, alarme (`hw_rtc_set_alarm/clear/woke_from_alarm/enter_alarm_sleep`), `hw_enable_power_key_toggle`, `hw_ac_electra_send`, `hw_ac_midea_send`, `hw_ac_lg_send`, `hw_ac_samsung_send`, `hw_ac_toshiba_send` |
| `hal_interface.cpp` | MOD | impl de tudo acima; `ir_ensure_begin` (refactor); fix do alarme (`setAlarm(h,m,0xFF,0xFF)`); power-key toggle cb; deep-sleep RTC; `hw_http_get` (HTTP/HTTPS+task); wraps Electra, Midea, LG, Samsung e Toshiba; headers IR isolados antes de `hal_interface.h` para evitar conflito `atomic_uint16_t` |

## Firmware — IR (controle genérico)

| Arquivo | | O que mudou |
|---|---|---|
| `ir_control.h` | NOVO | hierarquia de classes (peças 1–12): IrDevice → IrDeviceTV (NecTV/RcaTV + marcas) / IrDeviceAC (Electra/Midea/Komeco/Springer/LG/Samsung/Toshiba); registries separados `UniversalTVRemote` e `UniversalACRemote` |
| `ui_ir_remote.cpp` | MOD | reescrito: `IR Remote` abre raiz com botões grandes `TV`/`AC`; cada família tem dropdown de aparelho e grade de comandos; UI chama registries em vez de protocolo/raw direto; registra `TV TCL (Roku)` e `TV TCL (Android)` separadas; registra ACs Midea/Komeco/Springer/LG/Samsung/Toshiba |

## UI — launcher / navegação / back

| Arquivo | | O que mudou |
|---|---|---|
| `ui_main.cpp` | MOD | relógio no topo do launcher; botões de nav nos cantos; `SCREEN_TIMEOUT` 10→5s; logo de boot "Lewatch"; registra app Clock; remove tremor de navegação; (swipe revertido p/ original) |
| `ui_tools.cpp` | MOD | `create_menu`: back flutuante centralizado (top layer); remove tremor; esconde o back flutuante quando uma subpágina do `lv_menu` já mostra o back do header |
| `ui_wireless.cpp` | MOD | Weather/News viraram submenus; remove back caseiro |

### Back duplicado — removido o back caseiro (só isso) em:
`ui_nrf24` · `ui_audio` · `ui_monitor` · `ui_ble_kb` · `ui_keyboard` · `ui_ble` ·
`ui_gps` · `ui_microphone` · `ui_calendar` · `ui_sensor` · `ui_sys` · `ui_msgchat` ·
`ui_nfc` · `ui_radio` · `ui_camera_remote` · `ui_power`

## UI — apps novos

| Arquivo | | O que mudou |
|---|---|---|
| `ui_weather.cpp` | NOVO | clima (Open-Meteo) + tech news (Tom's Hardware RSS); `ui_weather_attach` + `ui_news_attach` |
| `ui_clock.cpp` | NOVO | cronômetro + countdown + alarme + tela de ring (MODO ALARME) |
| `ui_define.h` | MOD | decls `ui_weather_attach`, `ui_news_attach` |

## Simulador PC

| Arquivo | | |
|---|---|---|
| `sim/Makefile`, `sim/lv_conf.h`, `sim/compat.h`, `sim/stubs.cpp`, `sim/CMakeLists.txt` | NOVO | build SDL2 do UI no PC (sem flashar); `sim/Makefile` define `USING_IR_REMOTE` para mostrar o app IR no launcher |

## Tooling / docs

| Arquivo | | |
|---|---|---|
| `.clangd`, `compile_commands.json` | NOVO | navegação clangd (Neovim). Gitignored (paths absolutos) |
| `.gitignore` | NOVO | ignora build/, *.o, sim binário, CDB |
| `docs/DECISIONS.md`, `docs/IR_CONTROL.md`, `docs/CHANGES.md` | NOVO | histórico de decisões, arquitetura IR, este arquivo |

---

## NÃO tocado (intacto)

- **Drivers de rádio:** `hw_cc1101.cpp`, `hw_lr1121.cpp`, `hw_nrf2401.cpp`, `hw_sx1262.cpp`, `hw_sx1280.cpp`
- **Apps sem mudança:** `ui_factory.cpp`, `ui_msg.cpp`, `ui_theme.cpp`, `app_nfc.cpp/.h`
- **Config/assets:** `partitions.csv`, `event_define.h`, imagens/fontes em `src/`
- **Libs** (`libraries/`): nenhuma modificada — só consumidas
- `main.cpp` (entry da sim): **da LilyGo**, não modifiquei (só dei o build em volta)

> Observação: o commit "Baseline" já inclui as mudanças de IR/weather/clock/UI;
> o branch `sim-build` adicionou `sim/`; o branch `ir-classes` está com
> `ir_control.h` + HAL do Electra + docs/tooling (ainda não commitado).
