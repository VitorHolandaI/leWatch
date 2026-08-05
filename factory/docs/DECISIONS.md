# Decisões do projeto (T-Watch S3 — factory)

Histórico de decisões e armadilhas. **Não compilado pelo Arduino** (`.md` em `docs/`).
Board: LilyGo T-Watch S3, FQBN `esp32:esp32:twatchs3`, painel **ST7789 IPS LCD** (não OLED).

---

## 1. Infravermelho (IR)

**Pino:** emissor no **GPIO2**, definido pela board variant (`pins_arduino.h`: `IR_SEND (2)`,
`USING_IR_REMOTE`). **Sem receptor** — não dá pra capturar controles; códigos têm que ser conhecidos.

**Lib:** `IRremoteESP8266` só transporta (`sendNEC`, `sendRaw`). Sem UI, sem banco de códigos.

**TVs (NEC, valor wire-order direto no `sendNEC`):**
| TV | On | Off |
|---|---|---|
| LG (toggle) | `0x20DF10EF` | mesmo |
| TCL/SEMP/AOC (Roku discrete) | `0x57E316E9` | `0x57E318E7` |

- TCL/SEMP/AOC são **Roku TVs** → mesmo mapa Roku, customer code `0x57E3`, On cmd `0x16` / Off `0x18`.
- TCL/SEMP confirmados; **AOC** pode ter customer code diferente (flag no código).
- **Armadilha bit-order:** `sendNEC` manda MSB-first cru; `encodeNEC` faz `reverseBits`. Códigos
  publicados já são wire-order → passar direto, **sem** `encodeNEC`. (Meu `0x57E3E817` antigo estava errado.)

**TCL Android/Google TV (RCA):** ≠ TCL Roku. Protocolo **RCA addr 0x0F** (via `RcaTV`/
`hw_ir_send_raw` 38kHz). Bytes de comando capturados do Flipper-IRDB (`TVs/TCL/
TCL_43S446.ir`, `TCL_65C635K.ir`): Power `0x54`, Up `0x9A`, Down `0x1A`, Left `0x6A`,
Right `0xEA`, OK/Select `0x2F`, Home `0x10`, Vol± `0xF4`/`0x74`, Mute `0xFC`, source `0x58`.
Antes estavam em 0 (setas mortas) → corrigido. Google TV não tem canal (CH=0). RCA a
38kHz — se falhar em HW, testar 56kHz (portadora RCA clássica).

**Roku D-pad (SEMP/TCL Roku):** códigos no firmware (addr `0x57E3`) batem **byte-a-byte**
com Flipper-IRDB `TCL_ROKU_US.ir` (Up 19/Down 33/Left 1E/Right 2D/OK 2A, bit-reversed).
Se não funcionar = mira/alcance/modelo, não o valor.

**AC "Super Turn On":** brute-force de power-on em **25 famílias** de protocolo via `IRac`
unificado (`hw_ac_blast_count/name/send_on`, botão em ui_ir_remote). Lista: Midea, Coolix,
Gree, Kelvinator, TCL112, TCL96, Neoclima, Whirlpool, Samsung, LG, LG2, Daikin +
Daikin2/128/152/160/176, Fujitsu, Electra, Toshiba, Carrier, Hitachi, Panasonic, Haier,
Mitsubishi. Motivo de tantas: **marca ≠ 1 protocolo** — Daikin/Fujitsu/LG/TCL têm várias
famílias; rebrands BR (pesquisa de campo): Consul=Kelvinator/Whirlpool, Elgin=Gree/TCL112,
Springer/Komeco=Midea/Neoclima, Philco=TCL112/Gree, Electrolux=TCL96/Midea-legado;
Samsung/LG/Daikin/Fujitsu=nativos (mas múltiplas sub-famílias). UI passa 1 por tick
mostrando o nome → user vê qual liga o split. Confirmação real de estado só vem por
captura (IRrecvDumpV3) no controle físico; blast serve pra achar a família rápido.
No hardware do usuário, o primeiro blast (`MIDEA`, label `Midea/Springer/Komeco`) liga o AC;
por isso o controle normal Midea/Springer/Komeco também usa `IRac` protocolo `MIDEA`, não
`IRMideaAC` direto.

**Electrolux AC:** protocolo proprietário (não está na lib). Portado de
[`sys27/esphome-electrolux-ac`](https://github.com/sys27/esphome-electrolux-ac). 13 bytes, 38 kHz,
timings header `8950/4530`, bit `563`, one `1690`, zero `538`, footer `563`+gap `10000`.
Pacotes (host-verificados, Cool 24°C fan LOW): ON `C3 E1 00 00 06 00 04 00 00 04 00 00 57`,
OFF `...55`. Enviado por `hw_ir_send_raw` (`sendRaw`). Testado na origem no EXP26U339HW.

**Midea/Komeco AC:** adicionado via `IRMideaAC` da `IRremoteESP8266`, embrulhado por
`hw_ac_midea_send`. Komeco entra como alias separado (`AC Komeco (Midea)`) porque controles
de reposição aparecem como Midea/Komeco/Comfee compatíveis (`R05/BGE`, `R06/BGCE`, `R71A/CE`).
Ainda precisa teste em hardware real.

**Springer/Midea AC:** `AC Springer (Midea)` usa o mesmo caminho de `IRMideaAC`. Carrier/Springer
não foi tratado como uma entrada única porque há modelos/protocolos diferentes; adicionar depois
por protocolo/modelo confirmado.

**LG/Samsung/Toshiba AC:** adicionados por classes próprias da `IRremoteESP8266`:
`IRLgAc` (modelo `AKB74955603`), `IRSamsungAc` e `IRToshibaAC`, todos via wrappers HAL.
Ainda precisam teste em hardware real.

**Armadilha de include:** não incluir headers da `IRremoteESP8266` depois de `hal_interface.h`.
Esse header faz `using namespace std;` global; com isso `atomic_uint16_t` vira ambíguo entre
`std::atomic_uint16_t` e o typedef da lib. Solução local: em `hal_interface.cpp`, incluir
`LilyGoLib.h` primeiro para obter `USING_IR_REMOTE`/`IR_SEND`, depois headers IR, depois
`hal_interface.h`. Lib externa intacta.

Arquivos: `ui_ir_remote.cpp` (menu), `ir_control.h` (classes), `hal_interface.*`
(`hw_set_remote_code`, `hw_ir_send_raw`, `hw_ac_electra_send`, `hw_ac_midea_send`,
`hw_ac_lg_send`, `hw_ac_samsung_send`, `hw_ac_toshiba_send`).

---

## 2. UI / navegação

- **Back centralizado (único):** `USING_TOUCHPAD` desliga o back do header do `lv_menu`. Solução em
  `create_menu` (`ui_tools.cpp`): back flutuante no **top layer**, aparece em todo app, removido no
  `LV_EVENT_DELETE` do menu. **Removidos TODOS os backs caseiros** — 5 `create_radius_button`
  (ir/nrf24/wireless/camera/radio) + 14 `create_floating_button` (audio/gps/sys/sensor/nfc/etc.) +
  os `lv_menu_set_mode_root_back_btn` de nfc/power. Resultado: 1 back só, consistente.
- **Botões IR maiores** + lista com scroll vertical.
- **Navegação no launcher:** **swipe horizontal mantido** (carrossel com snap) **+** 2 botões nos
  cantos (◀ ▶) como alternativa, via `lv_obj_scroll_to_view(..., LV_ANIM_ON)`. (`ui_main.cpp`)
  - NÃO mexer no SCROLLABLE do panel: `remove_flag(SCROLLABLE)` trava o scroll programático (setinhas
    morrem) e `set_scroll_dir(NONE)` também bugou no hardware. Tentativa de swipe-off + fade revertida.
- **Hora + data no topo-centro do launcher** (`launcher_time_label`, timer 1s). Vive na tile do
  launcher → some nos apps automaticamente.
- **Sem tremor ao trocar de menu:** removido `hw_feedback()` de abrir-app, back central, `menu_show`
  e nav dos botões. Vibração só em botões funcionais.
- **Logo de boot:** "LilyGo" → **"Lewatch"**. A fonte `font_logo_84` só tem 6 glyphs (G L i l o y),
  então usei `font_alibaba_40` (fonte cheia). (`ui_main.cpp` start_logo)

---

## 3. Energia / sleep

LCD: **cor de fundo não economiza** (backlight LED é constante). O que importa: brilho, timeout,
panel sleep, e **sleep do MCU**.

- `lightSleep()` (lib) já corta rails (backlight/haptic/GPS/speaker/NFC) e mantém **RTC** (AXP2101).
  RAM preservada, acorda por ext1 (não reboot). É o "aperta e volta".
- **Wake do idle-sleep = SÓ power key** (`sleep-alarm-fixes`): o default do vendor
  `lightSleep()` arma **power key + touch** (`WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_TOUCH_PANEL`,
  `LilyGoWatchS3.h`). A pedido, todas as chamadas de idle-sleep (`hw_low_power_loop`,
  `power_key_light_sleep`, `hw_light_sleep_timed`) passam só `WAKEUP_SRC_POWER_KEY` → um toque
  não tira mais o relógio do sono de economia; só o botão. Power-key sempre incluso ⇒ nunca trava.
- `SCREEN_TIMEOUT` reduzido **10s → 5s** (`ui_main.cpp`).
- **Wrist-wake desativado** (BMA423 tem `FEATURE_TILT`/`enableTiltIRQ`, mas risco de falso-wake;
  fica off por escolha).
- **Power-key toggle:** short press liga/desliga a tela (brightness + `sleepDisplay`), via
  `instance.onEvent(POWER_EVENT)` → `PMU_EVENT_KEY_CLICKED`. (`hw_enable_power_key_toggle`)

---

## 4. Relógio / hora (RTC + NTP)

- **Fuso:** `GMT_OFFSET_SECOND` era `+8h` (China, default LilyGo) → **`-3h` (UTC-3)**. Era a causa
  do horário errado.
- **Sync do PC:** `hw_rtc_sync_build(__DATE__, __TIME__)` no boot grava a hora do PC no RTC **uma vez
  por upload novo** (marker em NVS `clocksync`). Boots normais não mexem. NTP afina depois.
- **NTP:** servidores BR (`a.st1.ntp.br`, `br.pool.ntp.org`). Nota: NTP é sempre UTC; o `-3` é quem
  faz o local.

---

## 5. Weather + Notícias (no menu WiFi)

Tudo **free, sem API key**. `hw_http_get` (HAL): `HTTPClient` com `WiFiClient` para HTTP e
`WiFiClientSecure setInsecure()` só para HTTPS. A função separa os caminhos para não alocar TLS em
URL `http://`; usa HTTP/1.0, sem reuse, streaming com timeout. Roda em **task FreeRTOS** (não trava
LVGL); `lv_timer` aplica na thread do LVGL.

- **Weather:** Open-Meteo, **Campina Grande-PB fixo** (lat -7.23, lon -35.88). Parse por substring,
  mapa WMO→texto. (ip-api geoloc descartado: não validável + ponto de falha extra.)
- **Notícias:** **Tom's Hardware RSS** (`https://www.tomshardware.com/feeds.xml`), 5 headers.
  GamersNexus (`https://gamersnexus.net/rss.xml`) existe, mas o feed põe o texto completo do artigo
  antes do próximo `<item>`; com buffer 16KB quase só aparece 1 manchete. Tom's é maior, mas as
  manchetes vêm compactas no começo. De-accent UTF-8→ASCII (senão vira quadradinho na fonte padrão).
- **Bugfix reboot pós-WiFi:** task do fetch 16KB→24KB, falha de `xTaskCreate` vira mensagem na tela,
  Open-Meteo usa HTTP direto para evitar TLS desnecessário.
- **Submenus separados:** o bloco na main_page escondia os botões de scan/connect. Agora são
  **2 sub-páginas** (`lv_menu_cont_create` + `lv_menu_page_create` + `lv_menu_set_load_page_event`):
  **Weather** (`ui_weather_attach`) e **News** (`ui_news_attach`). Fetch + poll compartilhados
  (`ensure_running`). main_page volta a mostrar SSID/senha/scan/connect.

---

## 6. Clock app + MODO ALARME

`ui_clock.cpp` (app "Clock" no launcher; ícone `img_configuration` — não há ícone de relógio que
linke no LVGL v9, `img_calendar` é v8-only).

- **Cronômetro** (start/stop/reset), **Countdown** (rollers, vibra ao zerar).
- **Alarme:** rollers hora/min + botão **MODO ALARME**.
- **Bug corrigido (hora+minuto):** `setAlarmByHours` + `setAlarmByMinutes` cada um reescreve os 4
  campos do PCF8563, então o 2º zerava a hora p/ don't-care → tocava só pelo minuto. Fix: um
  **`setAlarm(hora, minuto, 0xFF, 0xFF)`** (0xFF = NO_ALARM, dia/semana don't-care).
- **MODO ALARME** (`hw_enter_alarm_sleep`): arma alarme PCF8563 + corta todos os rails +
  `sleepDisplay` + **ext1 wake no RTC_INT (GPIO17, ANY_LOW)** + `esp_deep_sleep_start`. Só RTC vivo.
  Acorda via **reboot** no horário.
- **Boot ring** (`factory.ino`): `hw_woke_from_alarm()` (EXT1 + `isAlarmActive`) → `resetAlarm`
  (evita loop) → `ui_alarm_ring()` (overlay vibra + OK).

**Catch do RTC-wake:** a lib **não expõe RTC como wake source** no S3 (`WakeupSource_t` só tem
POWER/TOUCH/SENSOR), por isso o sleep custom em `hw_enter_alarm_sleep`. GPIO17 é RTC-IO no S3 → ok.

**Alarme não pode ser cortado pelo idle-sleep** (`sleep-alarm-fixes`): o ring é overlay em
`lv_layer_top` + som via `playerTask` (task FreeRTOS) em loop. O timeout de tela normal chamava
`hw_low_power_loop()` → `esp_light_sleep_start()`, que **congela TODAS as tasks** (inclusive o
`playerTask`) → som morria. Fix: flag `hw_alarm_ringing()` (set em `ui_alarm_ring`, clear em
`ring_dismiss_cb`) some na guarda do light-sleep em `ui_poll_timer_callback` (irmã de
`hw_record_active()`). A tela ainda pode ir pro screensaver (CPU 80MHz, som continua); dispensar =
power-key acorda + toca OK. *Não* apaga o backlight de vez pra não precisar de lógica de "grace" no
wake nem esconder o OK.

**Alarme mais alto = ganho de software** (`sleep-alarm-fixes`): este board é **MAX98357A**
(`USING_PCM_AMPLIFIER`), **não** ES8311 (`USING_AUDIO_CODEC` fica indefinido). O MAX98357A tem
ganho **fixo por resistor** e não há registrador de volume de codec — logo `hw_set_volume` é
compilado fora / inerte. Único jeito de "aumentar": escalar as amostras PCM em software
(`apply_pcm_gain`, satura int16) antes de `player.write`, aplicado em `mp3_write_output` e `playWAV`.
`hw_set_audio_gain(g)` fica 1 (unity) pra música/click/gravação e vira `ALARM_GAIN` (4) só no ring.
Ganho > 1 **clipa/distorce** — aceitável pra alarme (urgência). Ajusta `ALARM_GAIN` + reflash.

**Alarme normal one-shot apenas**: o app diário recorrente (`ui_alarm.cpp`) e o modo
"dormir após" / default 23h foram removidos. O caminho mantido é o do Clock: escolhe h:m,
arma `hw_rtc_set_alarm`, entra em `hw_enter_alarm_sleep`, acorda via RTC_INT, o boot chama
`hw_rtc_clear_alarm()` para manter one-shot e abre `ui_alarm_ring()`. Enquanto toca,
`hw_alarm_ringing()` barra idle/light-sleep para não congelar player/vibração; só o OK para.

---

## 7. Upload (esptool) — pendência conhecida

`OSError: [Errno 71]` no `_update_rts_state`: USB-JTAG/serial interno do S3 (`303a:1001`) tropeça no
toggle RTS do esptool. Contorno atual: **upload pela Arduino IDE** funciona. CLI: download mode
manual (BOOT+RESET) ou `--before no_reset`.

---

## Checklist de teste no hardware
1. MODO ALARME com alarme +2min (acorda/reboota/toca?). Falha de wake = polaridade RTC_INT.
2. Power-key toggle vs timeout/sleep (sem brigar).
3. Weather/notícias ao conectar WiFi.
4. RTC pega hora do PC no 1º boot pós-upload; NTP afina.
5. Clock app rola pra ver os 3.
