# Handoff — continue aqui

Sessão no repo `/home/vitor/Arduino/factory` (T-Watch S3, LVGL v9 + LilyGoLib,
ESP32-S3/Arduino, simulador SDL). Branch **`sleep-alarm-fixes`** (em cima de
`documentation`). User flasha SÓ via Arduino IDE → nada pode depender de flags
`-D` custom. Commits SEM trailer de atribuição, só detalhe de implementação.
IR/qualquer código = fatos pesquisados COM fonte, não chute. Respostas terse
(CAVEMAN MODE); código/commits/PR normais. Idioma do user: PT-BR.

## Estado atual

Tree **sujo** com mudanças ainda não commitadas desta continuação:
- Share agora tem HTTP Basic auth além da senha WPA2 do SoftAP.
- Weather/News: fix de reboot provável por fetch TLS/heap/stack; Open-Meteo usa
  HTTP direto, News usa Tom's Hardware RSS (`https://www.tomshardware.com/feeds.xml`).
- `hw_http_get` usa `WiFiClient` para HTTP e só instancia `WiFiClientSecure` em HTTPS.
- Task Weather/News subiu para 24KB e falha de `xTaskCreate` mostra erro em vez
  de ficar em loading.
- Alarme diário/recorrente removido; fica só o alarme normal one-shot do Clock.
- Controle normal Midea/Springer/Komeco agora usa o mesmo `IRac` protocolo `MIDEA`
  do primeiro Super Turn On, porque esse blast ligou o AC mas o controle normal não.

Commits desta sessão (mais novo em cima):
- `17e97f9` Refresh continue.md handoff with full session state
- `faa504a` doc: colon estático + boot alarme corrigido
- `f1e09dd` **colon `:` do relógio fixo** (parou de piscar)
- `bbcbe62` **fix**: boot mantém o one-shot do Clock realmente one-shot
- `cc25e95` doc alarme recorrente
- `5e62b9d` **app Alarme diário recorrente + auto deep-sleep noturno**
- `180083e` Super Turn On expandido p/ 25 famílias + docs
- `0cf560e` doc IR + handoff
- `aad55ca` **fix TCL Android TV codes + AC Super Turn On**
- `6158beb` **app Share** (SoftAP + web download das gravações)
- `c787e62` rollers do alarme = hora atual do RTC
- `b200b1b` `docs/ARCHITECTURE.md`
- `1d6fd13` **idle-sleep acorda só power key + alarme sobrevive ao sono + alarme +alto**

Build/verificação (rodar sempre depois de mexer):
```
make -C sim -j$(nproc) && SDL_VIDEODRIVER=dummy timeout 8 ./sim/sim   # exit 124 = ok
./scripts/quality_report.sh                                           # cppcheck+lizard
./scripts/quality_report.sh --build --smoke                            # completo
```
Último resultado desta continuação: `./scripts/quality_report.sh --build --smoke` = ok
(`quality build`, `cppcheck`, `lizard`, `sim smoke`).
Binário do sim = `sim/sim` (NÃO `./sim`, que é diretório). Makefile do sim faz
glob de `ui_*.cpp` → arquivo UI novo entra sozinho. `factory.ino` **não** compila
no sim (sim usa `main.cpp`). `hal_interface.cpp` compila no sim com corpos sob
`#ifdef ARDUINO` virando stub `#else`; funções novas precisam stub sim. Corpo IR
sob `#ifdef ARDUINO` → sim **não** valida enums `decode_type_t` (conferir na lib
`/home/vitor/Arduino/libraries/IRremoteESP8266/src/IRremoteESP8266.h`).

## Fato de hardware (corrige memória antiga)
Board = **MAX98357A** (`USING_PCM_AMPLIFIER`) + **PDM mic**
(`USING_PDM_MICROPHONE`), **NÃO** ES8311/`USING_AUDIO_CODEC`. Logo `hw_set_volume`
é inerte (ganho fixo em HW) → "mais alto" só via ganho de software. Recorder capta
por `instance.mic.readBytes` (PDM). Ver `memory/fact-audio-hardware-max98357.md`.

## Features entregues nesta sessão
1. **Idle-sleep acorda só power key** (toque não acorda): `hw_low_power_loop`,
   `power_key_light_sleep`, `hw_light_sleep_timed` passam só `WAKEUP_SRC_POWER_KEY`.
2. **Alarme não corta som ao idle**: flag `hw_alarm_ringing()` barra o light-sleep
   em `ui_poll_timer_callback`.
3. **Alarme mais alto**: `apply_pcm_gain` (satura int16) em `mp3_write_output` +
   `playWAV`; `hw_set_audio_gain`; `ALARM_GAIN 4` no ring (`ui_clock.cpp`).
4. **Rollers do alarme = hora atual** (`hw_get_date_time`).
5. **App Share** (`ui_share.cpp` + `hw_share_start/stop/active/info`): SoftAP
   `TWatch-Share` WPA2 (senha 8díg aleatória, mostra rede/senha/IP) + `WebServer`
   numa task com HTTP Basic auth (`share` + senha 8díg separada); `/` lista
   gravações, `/dl?f=` baixa. Extrai WAV do FFat.
6. **IR TCL Android/Google** (`ir_control.h` `TclAndroidTV`): bytes RCA de nav
   estavam 0 (setas mortas) → preenchidos do Flipper-IRDB (RCA addr 0x0F: Power
   0x54, Up 0x9A, Down 0x1A, Left 0x6A, Right 0xEA, OK 0x2F, Home 0x10). RCA a
   38kHz — se falhar em HW, testar 56kHz em `RcaTV::sendRca`.
7. **AC "Super Turn On"** (`hw_ac_blast_count/name/send_on` + botão em
   `ui_ir_remote.cpp`): via `IRac` unificado dispara power-on de **25 famílias**,
   1/tick (1.3s), mostra o nome → acha qual liga o split. `#ifdef ARDUINO`.
8. **Alarme normal one-shot apenas** (`ui_clock.cpp`): rollers h:m + som + botão
   "Ativar alarme". O app diário recorrente (`ui_alarm.cpp`) e o "dormir após"
   / default 23h foram removidos. Boot (`factory.ino`) limpa o RTC e toca uma vez;
   `hw_alarm_ringing()` impede idle/light-sleep enquanto o overlay do alarme toca.
9. **Colon `:` do relógio fixo** (`clock_update_datetime` em `ui_main.cpp`): era
   escondido/mostrado a cada 1s (piscava); agora sempre visível.
10. **Weather/News mais estável**: Open-Meteo usa HTTP direto; News usa Tom's
   Hardware RSS (`https://www.tomshardware.com/feeds.xml`); `hw_http_get` escolhe
   `WiFiClient` para HTTP e `WiFiClientSecure` só para HTTPS; task de fetch sobe
   para 24KB e falha de `xTaskCreate` mostra erro em vez de travar em loading.
   O alarme diário recorrente foi removido; manter só o one-shot do Clock.
11. **Controle normal Midea alinhado ao Super Turn On**: `hw_ac_midea_send` trocou
   `IRMideaAC` direto por `IRac` com protocolo `MIDEA`, mesmo caminho do primeiro
   blast que funcionou no hardware.

## Achados IR (contexto)
- Roku D-pad (SEMP/TCL Roku, addr `0x57E3`/EA C7) no firmware JÁ bate byte-a-byte
  com Flipper-IRDB `TCL_ROKU_US.ir`. Se Roku não funciona = mira/modelo, não valor.
- Rebrands BR de AC (pesquisa do user): marca ≠ 1 protocolo. Consul=Kelvinator/
  Whirlpool, Elgin=Gree/TCL112, Springer/Komeco=Midea/Neoclima, Philco=TCL112/Gree,
  Electrolux=TCL96/Midea; Samsung/LG/Daikin/Fujitsu nativos (várias sub-famílias).
- Sem receptor IR no relógio → captura definitiva = Arduino+receptor (IRrecvDumpV3).

## A validar em HARDWARE (não dá no sim)
- Toque não acorda relógio dormindo; power-key acorda.
- Alarme normal one-shot toca e segue tocando até OK, sem idle/light-sleep cortar
  som/vibração.
- Recorder captura/replay; Share (conectar AP + login HTTP + baixar WAV em http://192.168.4.1).
- Weather/News após conectar WiFi: sem reboot; News deve mostrar Tom's Hardware.
- AC Midea/Springer/Komeco: botão normal Power deve ligar igual ao primeiro
  Super Turn On; validar Temp/Mode/Fan depois.
- TCL Android: setas/OK/Home/Power. AC Super Turn On: qual família liga o split.
- Colon do relógio parado (não pisca).

## Pendências / próximos passos possíveis
- Export por USB MSC (alternativa ao Share).
- Blackout real do backlight durante alarme (hoje segura no screensaver).
- Ícone dedicado p/ Share/Recorder (hoje reusam img_wifi/microphone).
- Nada mergeado na main; stack de branches em `memory/project-roadmap.md`.

## Regras da sessão
CAVEMAN MODE (terse; código/commits normais). Arduino IDE only. Commits sem
Co-Authored-By. IR = fonte, não chute; sempre citar fontes ao pesquisar.
