# Arquivos tocados nesta sessão

Visão do que foi **realmente mexido** vs intocado. `NOVO` = arquivo criado;
`MOD` = modificado.

## Build/sim — rodar o simulador direto do repo

| Arquivo | | O que mudou |
|---|---|---|
| `sim/CMakeLists.txt` | MOD | Aceita `factory/libraries/lvgl` (symlink local) além do layout `~/Arduino/factory`; adiciona `${LVGL}/src` ao include (LVGL 9 mudou o driver pra `src/drivers/sdl/`), force-include `compat.h`, `-DARDUINO_T_WATCH_S3 -DUSING_IR_REMOTE` (sem eles `MAIN_FONT` não existe) e `GLOB_RECURSE` em `src/` (as fontes estão em `src/font/`). Também linka `sim/stubs.cpp` — sem ele o link morria em `hw_get_radio_params`. O CMake estava quebrado antes disso. |
| `sim/Makefile` | MOD | `LVGL ?=` procura `../libraries/lvgl` (repo) antes de `../../libraries/lvgl` (Arduino). |
| `.gitignore` | MOD | `libraries/lvgl` — o symlink pro LVGL do Arduino (93 MB, não vai pro git). |
| `wireguard_secrets.h` | NOVO (local) | Copiado de `~/Arduino/factory` (628 B). Gitignored: `git status` não mostra e `git check-ignore` confirma. Sem ele o `__has_include` faz o VPN compilar como no-op. |

Smoke (headless, sem janela):

```sh
cmake -S sim -B /tmp/simbuild && cmake --build /tmp/simbuild -j
SDL_VIDEODRIVER=dummy timeout 10 /tmp/simbuild/sim   # exit 124 = rodou 10 s sem crash
```

`make -C sim quality` (quality-build + cppcheck + lizard + smoke) só funciona
**serial** — com `-j` ele constrói o mesmo `sim` duas vezes com flags diferentes
e o link quebra. `.NOTPARALLEL:` no Makefile trava isso.

### Upload do firmware

| Arquivo | | O que mudou |
|---|---|---|
| `flash.sh` | MOD | Passa a compilar **esta árvore** (`SKETCH` = pasta do script) e não mais `~/Arduino/factory`. Build persistente em `factory/build/` (gitignored, sobrevive a reboot) com atalho de upload: se nada mudou desde o último `.bin`, ele **não recompila**, só envia. `--build` (`-b`) força recompilar; sozinho ele detecta quando alguma fonte (`*.c/cpp/h/ino/csv`) ficou mais nova que o binário. Usa o `WireGuard-ESP32` vendorizado no repo e avisa se `wireguard_secrets.h` faltar. |

```sh
./flash.sh                 # usa build/ (upload em ~2 s quando nada mudou)
./flash.sh --build         # recompila e envia
./flash.sh /dev/ttyACM0 -b # porta + recompilar
```

Resultado da última rodada (2026-10-29):

| check | resultado |
|---|---|
| firmware `arduino-cli compile` (S3, com secrets) | OK — 2772421 B (88% flash), 89756 B de RAM |
| `quality-build` (`-Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Wformat=2 -Wundef`) | exit 0; 1615 warnings, praticamente todos dentro do LVGL (as flags valem pra ele também) |
| `cppcheck` (42 arquivos) | exit 0; 215 achados, todos `style`/`warning` do código antigo. Nos arquivos novos: só `cstyleCast` (padrão do repo), 1 falso positivo (`duplicateExpressionTernary` em `LV_SYMBOL_POWER " Desligar VPN" : LV_SYMBOL_POWER " Ligar VPN"`) e 2 `constParameterCallback` que conflitam com o typedef de callback do LVGL |
| `lizard` (714 funções, `-C 15 -L 80`) | 28 acima do limite, nenhuma nos arquivos novos (maior ali: `fill_device_dropdown`, 37 linhas / CCN 10) |
| `smoke` | exit 0 (10 s headless sem crash) |

O que **não** tem cobertura: nada rodou no hardware, e o sim não tem rádio — WiFi
e deauth ali são stubs (`sim/stubs.cpp`). `hal_interface.cpp` também é opaco pro
lizard (arquivo quase todo `#ifdef ARDUINO`, ele só parseia 15 funções).

## Branch `deauth-fix` — deauth que não saía no ar

| Arquivo | | O que mudou |
|---|---|---|
| `wifi_deauth_bypass.cpp` | NOVO | Override de `ieee80211_raw_frame_sanity_check()` (símbolo do `libnet80211.a`, o blob fechado do WiFi) devolvendo 0. Sem ele o `esp_wifi_80211_tx()` devolve **ESP_OK** e descarta silenciosamente deauth/disassoc. Detalhes e referências em `docs/wifi-deauth.md`. |
| `flash.sh` | MOD | `--build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition"` — sem o flag o link morre com "multiple definition". |
| `ui_deauth.cpp` | MOD | **(1)** O AP saía na lista de dispositivos (beacon tem o AP como src); agora é filtrado. **(2)** Alvo guardado por MAC, não por índice do dropdown (que mudava com a lista ao vivo) — antes dava pra atacar o MAC errado. **(3)** Rajada periódica de 16 frames no tick de 500 ms: só reagir a tráfego deixava cliente parado intacto. **(4)** `WiFi.softAPdisconnect(true)` no stop. **(5)** Contador `s_stations_deauthed` → `s_deauth_frames` (era "matches", agora conta frame mesmo). |
| `docs/wifi-deauth.md` | NOVO | Diagnóstico verificado (`nm`/`objdump` no toolchain do projeto), os repos de referência, os bugs corrigidos e como validar no serial. |

## Branch `wifi-autosave` — conectar salva sozinho, lista no rodapé

| Arquivo | | O que mudou |
|---|---|---|
| `hal_interface.*` | MOD | **(1)** `hw_set_wifi_connect` devolve `bool` e **não imprime mais a senha** no serial. O autosave no `GOT_IP` virou `wifi_autosave_persist_pending()`/`wifi_autosave_attach()` e **loga o motivo** quando o NVS recusa — antes o retorno de `hw_wifi_saved_add` era descartado (a rede sumia sem aviso). **(2)** `wifi_saved_result_t` + `hw_wifi_saved_add_ex()`: `OK/INVALID_SSID/FULL/STORAGE_FAIL` em vez de `false` mudo. **(3)** `WIFI_MAX_SAVED` 8 → **10** (~160 B por rede no NVS, ~1,7 KB dos 20 KB da partição). **(4)** `hw_wifi_saved_capacity/contains/revision/get_password/take_failure`: a falha do autosave (que roda fora da task da UI) fica pendente e a UI a mostra no tick. |
| `ui_wireless.cpp` | MOD | Botão **Salvar rede removido**: `Conectar` já grava no NVS quando a rede sobe. `Conectar` usa a rede **do formulário** (`hw_set_wifi_connect`; sem SSID cai no WiFiMulti das salvas). Senha vazia = rede aberta, 1..7 chars é recusado. `dropdown_event`+`load_saved_password` preenchem a senha da rede salva ao trocar de SSID. Scan corrigido (`scan_started_tick` + 500 ms): antes o dropdown podia fechar vazio e "salvar" falhava com "SSID is null". Sub-página virou painel na própria página. |
| `ui_wifi_saved.cpp` | NOVO | Painel **Redes salvas (n/10)** dentro da página: lápis carrega SSID+senha no formulário, lixeira remove. Reconstroi só quando `hw_wifi_saved_revision()` muda, então roda no tick de 1 s sem reler o NVS. |
| `ui_define.h` / `ui_msg.cpp` | MOD | `wifi_saved_load_cb_t`, `ui_wifi_saved_panel_create/refresh`; `ui_msg_is_open()` pra não perder o aviso de "lista cheia" quando já tem pop-up na tela. |

- Senha fica em **texto puro** no NVS (`s<n>`/`p<n>` no namespace `wifinets`), como o WiFiMulti do ESP32 espera — WPA precisa do texto pra derivar a PSK, não dá pra guardar hash. Proteção só com NVS encryption/flash encryption (eFuse), que não está ligado.

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
