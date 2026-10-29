# Deauth no ESP32 (`ui_deauth.cpp`)

Status: **bypass aplicado** (`wifi_deauth_bypass.cpp` + flag no `flash.sh`).
Falta validar no hardware: ver "Como saber se está funcionando".

Uso pretendido: teste em **rede própria/autorizada**. Deauth derruba clientes
de verdade; em rede de terceiro é interferência.

---

## Por que não funcionava (e por que parecia "silencioso")

O `esp_wifi_80211_tx()` do ESP32 passa por uma checagem dentro do blob fechado
do WiFi, `libnet80211.a`:

```
ieee80211_raw_frame_sanity_check()
```

Ela recusa frames de gerenciamento "não permitidos" — entre eles **deauth
(0xC0)** e **disassoc (0xA0)**. O detalhe cruel: quando ela recusa,
`esp_wifi_80211_tx()` **continua devolvendo ESP_OK** e o frame simplesmente não
vai para o ar. Nada no código da aplicação denuncia a falha.

Verificado neste toolchain (esp32 core 3.3.10, S3):

```sh
$ xtensa-esp32-elf-nm --defined-only .../esp32s3-libs/3.3.10/lib/libnet80211.a | grep sanity
0000005c T ieee80211_raw_frame_sanity_check      # existe e é forte

$ xtensa-esp32-elf-objdump -d libnet80211.a        # é uma checagem real
  5c: entry a1, 64
  79: movi.n a6, 23      ; compara tamanho...
  7e: movi a6, 0x5dc     ; ...com o máximo de 1500
```

## O que fazemos agora

`wifi_deauth_bypass.cpp` define o mesmo símbolo devolvendo 0:

```c
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) { return 0; }
```

Isso cria uma **definição duplicada**, então o link precisa de
`-Wl,--allow-multiple-definition` (o `flash.sh` já passa via
`compiler.c.elf.extra_flags`). Provas:

- sem a flag: `multiple definition of 'ieee80211_raw_frame_sanity_check'` (erro de link);
- com a flag: o `.map` mostra o símbolo vindo de `wifi_deauth_bypass.cpp.o` e o
  disassembly do `.elf` vira `movi.n a2, 0; retw.n`.

Se você compila pela **Arduino IDE** (não pelo `flash.sh`), o flag tem que ir no
`platform.txt` do core:

```
compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition
```

## Referências reais (GitHub)

| Repo | O que pegar de lá |
|---|---|
| [tesa-klebeband/ESP32-Deauther](https://github.com/tesa-klebeband/ESP32-Deauther) | Implementação mínima e legível: `src/deauth.cpp`. O frame de 26 bytes, `WIFI_IF_AP`, `WiFi.softAP(ssid, pass, WiFi.channel(n))` e o `sniffer()` que casa `dest == BSSID do AP`. |
| [l3tchupkt/esp32-deauth-bypass](https://github.com/l3tchupkt/esp32-deauth-bypass) | Explica o `ieee80211_raw_frame_sanity_check` e as duas formas de furar (linker override com `-zmuldefs`, ou patch de binário com `objcopy`/`patch.py`). |
| [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder) | O firmware de referência em produção; usa o mesmo override. Ver também a [issue #1515](https://github.com/justcallmekoko/ESP32Marauder/issues/1515) — gente recebendo `ESP_ERR_INVALID_ARG` quando o bypass não está no link. |
| [SpacehuhnTech/esp8266_deauther](https://github.com/SpacehuhnTech/esp8266_deauther) | Referência de arquitetura (scan → lista de clientes → ataque contínuo), não de ESP32. |
| [Jeija/esp32-80211-tx](https://github.com/Jeija/esp32-80211-tx) | Experimento original de injeção de frames 802.11 no ESP32 (beacon spam, deauth). |

Nota: `ieee80211_raw_frame_sanity_check` é símbolo interno do blob. Uma
atualização do core ESP32 pode renomeá-lo — se um dia o deauth voltar a não
fazer nada, é o primeiro lugar a conferir (`nm` no `libnet80211.a` do seu core).

## Bugs que estavam junto

1. **O AP aparecia na lista de dispositivos.** Beacons e respostas têm o próprio
   AP como `src`/`dest`; sem filtrar, o alvo mais óbvio da lista era o roteador
   (que ignora deauth para si mesmo). Agora o MAC do AP é descartado em
   `observed_station_add()`.
2. **A seleção voltava para o primeiro item** a cada atualização da lista, e o
   ataque usava o **índice** do dropdown num snapshot novo — dava para atacar o
   MAC errado. Agora o alvo é guardado por MAC (`s_selected_station`) e a lista
   só é reconstruída quando o conjunto de MACs muda, preservando a escolha.
3. **Só atacava quando via tráfego.** Cliente parado nunca gera frame, então
   nada era enviado. Agora há uma rajada periódica de 16 frames no tick de
   500 ms, além da reação ao tráfego.
4. **O AP de ataque ficava no ar** depois de "Parar observador"; agora
   `WiFi.softAPdisconnect(true)` roda no stop.

## Como saber se está funcionando

Pelo serial, com o ataque rodando (tick de 500 ms):

```
[deauth] stations=4 frames=320 tx_ok=320 tx_fail=0 last_err=ESP_OK
```

- `tx_ok` subindo e o celular/notebook **caindo da rede** = bypass funcionando.
- `tx_ok` subindo e **nada acontecendo** = frame está sendo engolido; confira se
  o flag de link entrou (o `.map`/disassembly acima) e se o canal bate com o AP.
- `tx_fail` subindo com `ESP_ERR_INVALID_ARG` = falta o bypass no link.

## Limitações

- 2.4 GHz só, e só o canal do AP escolhido (a varredura precisa ter pego ele).
- WPA3/PMF (802.11w) protege clientes contra deauth forjado — nesses casos nada
  acontece, e isso é esperado.
- Deauth derruba o cliente, não "quebra" a senha: ele reconecta sozinho.
