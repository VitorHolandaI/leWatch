# Controle IR genérico (`ir_control.h`)

Hierarquia de classes para controlar aparelhos por infravermelho (TVs, ACs) a
partir de **comandos lógicos** (Power, Vol+, Temp+...), escondendo o protocolo
de cada aparelho. Construído incrementalmente, classe por classe.

Status: **TV + AC ligados na UI**. A UI abre `IR Remote` -> `TV` ou `AC`, e cada
família usa seu registry separado (`UniversalTVRemote` / `UniversalACRemote`).

---

## Ideia central

- **Comando lógico** = igual pra todo aparelho da família (toda TV tem Power/Vol/Ch).
- **Codificação IR** = difere por aparelho (NEC, RCA, protocolos de AC).
- **Strategy + herança:** a UI fala `execute(comando)` num ponteiro de base; cada
  classe concreta sabe codificar do seu jeito (polimorfismo via `virtual`).

```
IrDevice (raiz: name(), powerToggle())
├─ IrDeviceTV (execute(TvCommand))
│   ├─ NecTV (sendNEC via codeFor)
│   │   ├─ RokuRefTV (mapa Roku 0x57E3)
│   │   │   ├─ TclRokuTV   ├─ SempRokuTV   └─ AocRokuTV
│   │   └─ LgTV (mapa LG 0x20DF)
│   └─ RcaTV (sendRaw, frame RCA via cmdByte)
│       └─ TclAndroidTV
└─ IrDeviceAC (execute(AcCommand))
    ├─ ElectraAC (embrulha IRElectraAc da lib via HAL)
    ├─ MideaAC (embrulha IRMideaAC da lib via HAL)
    │   ├─ KomecoMideaAC (alias separado, mesmo protocolo Midea)
    │   └─ SpringerMideaAC (alias separado, mesmo protocolo Midea)
    ├─ LgAC (embrulha IRLgAc modelo AKB74955603 via HAL)
    ├─ SamsungAC (embrulha IRSamsungAc via HAL)
    └─ ToshibaAC (embrulha IRToshibaAC via HAL)

UniversalTVRemote (lista IrDeviceTV*)
UniversalACRemote (lista IrDeviceAC*)
```

## Responsabilidade por camada

| Classe | Papel |
|---|---|
| `IrDevice` | contrato mínimo comum (nome + power) — pro registry/blast |
| `IrDeviceTV` / `IrDeviceAC` | família: define o `execute(cmd)` tipado |
| `NecTV` / `RcaTV` | **encoder** do protocolo (como mandar) |
| `RokuRefTV` | **dado** compartilhado (mapa de códigos Roku) |
| `TclRokuTV`, `LgTV`... | **aparelho/marca** concreto (nome, e códigos se únicos) |
| `UniversalTVRemote` | registry de TVs; executa `TvCommand` no aparelho selecionado |
| `UniversalACRemote` | registry de ACs; executa `AcCommand` no aparelho selecionado |

Regra de herança: cria base de protocolo + folhas **quando há variantes** (Roku);
classe concreta única **quando não há** (LG). O design adapta à realidade.

## Como um comando flui

```cpp
IrDeviceTV* tv = new TclRokuTV();
tv->execute(TV_VOL_UP);
// NecTV::execute -> RokuRefTV::codeFor(TV_VOL_UP) -> 0x57E3F00F
//               -> hw_set_remote_code(0x57E3F00F) -> irsend.sendNEC(...) -> GPIO2
```

- **TVs NEC:** `codeFor(cmd)` devolve o hex → `hw_set_remote_code` (sendNEC).
- **RCA:** `cmdByte(cmd)` → monta frame de 24 bits → `hw_ir_send_raw` (sendRaw).
- **AC:** `IrDeviceAC` altera estado interno e cada classe concreta envia o estado
  completo via wrapper HAL (`hw_ac_*_send`) usando a classe da `IRremoteESP8266`.

## UI atual

- `IR Remote` abre uma tela raiz com botões grandes `TV` e `AC`.
- `TV` mostra dropdown de aparelho, depois **modo controle**: cruz de navegação
  (↑↓←→) com **OK** no centro, um botão largo **Home** logo abaixo, e — rolando
  pra baixo — a grade dos demais botões (Power, Vol +/-, Ch +/-, Mute, Input).
  TCL aparece por plataforma/protocolo: `TV TCL (Roku)` e `TV TCL (Android)`.
  Não existe um "TCL geral" seguro porque TVs TCL Brasil podem usar mapas IR diferentes.
  D-pad/OK/Home têm códigos NEC Roku/LG documentados, **não confirmados** em hardware;
  TCL Android (RCA) ainda não tem bytes de nav → essas teclas não enviam nada.
- `AC` mostra dropdown de aparelho e uma grade: Power, Temp +/-, Cool, Heat, Fan,
  Fan +/-, Swing.
- ACs atuais: `AC Electrolux (Electra)`, `AC Midea`, `AC Komeco (Midea)`,
  `AC Springer (Midea)`, `AC LG`, `AC Samsung`, `AC Toshiba`.
- Komeco entra como teste Midea porque há controles/reposições vendidos como
  Midea/Komeco/Comfee compatíveis (`R05/BGE`, `R06/BGCE`, `R71A/CE`). Confirmar
  em hardware. Ver seção **Komeco no Brasil** abaixo.
- Springer entra primeiro como `Springer (Midea)`; Carrier/Springer fica separado
  para depois porque há variação maior de protocolos.
- A UI não chama protocolos diretamente; ela chama os registries, que chamam as
  classes concretas por polimorfismo.

## Regra para marcas populares de AC no Brasil

Não adicionar marca "genérica" se ela usa vários OEM/protocolos. Preferir entradas
por protocolo/modelo testável, por exemplo `Komeco (Midea)` em vez de `Komeco`
solto. Isso evita uma UI cheia de nomes que mandam o código errado.

## Nota de compatibilidade C++/headers

`hal_interface.h` ainda contém `using namespace std;` no escopo global. Isso torna
`atomic_uint16_t` ambíguo dentro da `IRremoteESP8266` se os headers da lib forem
incluídos depois. Por isso `hal_interface.cpp` inclui `LilyGoLib.h` e os headers
IR antes de `hal_interface.h`, sem editar a lib externa.

## Como o IR é mandado fisicamente

Hex **não** vira voltagem; vira **tempo de pulsos**:

```
hex -> binário -> mark/space (µs) -> bursts de 38kHz no LED IR (GPIO2)
```

1. **Hex → bits.** `0x2A` = `00101010`.
2. **Bit → tempo.** NEC = *pulse-distance*: mark fixo (~560µs) + space variável
   (560µs = 0, 1690µs = 1), com header de sincronismo no início.
3. **Mark → portadora 38kHz.** Durante o "mark" o LED pisca 38.000×/s; o receptor
   da TV é sintonizado em 38kHz → ignora sol e outras fontes IR.

O que viaja no ar = **padrão de tempo**, não o hex. `sendNEC` gera esse tempo a
partir do hex; `sendRaw` recebe o tempo já pronto (montado por nós).

## Códigos validados (TV)

| Marca | Protocolo | Power | Fonte |
|---|---|---|---|
| Roku (TCL/SEMP/AOC) | NEC, `0x57E3` | `0x57E3E817` (toggle) | JP1/IRDB |
| LG | NEC, `0x20DF` | `0x20DF10EF` | IRDB/LIRC |
| TCL Android | RCA-38, device 15 | `0x2A` (byte) | IRDB/JP1 |

Demais comandos (Vol/Ch/Mute/Input) estão no `switch` de cada classe em `ir_control.h`.

### Navegação (D-pad + OK + Home) — códigos CAPTURADOS de fonte

| Tecla | Roku (`0x57E3`) | LG (`0x20DF`) |
|---|---|---|
| ↑ Up    | `0x57E39867` | `0x20DF02FD` |
| ↓ Down  | `0x57E3CC33` | `0x20DF827D` |
| ← Left  | `0x57E37887` | `0x20DFE01F` |
| → Right | `0x57E3B44B` | `0x20DF609F` |
| OK/Select | `0x57E354AB` | `0x20DF22DD` |
| Home    | `0x57E3C03F` | `0x20DF3EC1` |

**Fontes (jun/2026):**
- **Roku:** Flipper-IRDB `TVs/TCL/TCL_ROKU_US.ir` (logickworkshop). Flipper usa addr
  `EA C7` = wire `0x57E3` (bit-reversed); o comando wire é o bit-reverse de cada byte
  do Flipper. Conferido: Power Flipper `17 E8` → `0xE817` = código já existente.
- **LG:** captura ESPHome do controle `AKB74475481` (gist TheGroundZero). 6/6 batem.

Padrão NEC `prefix + cmd + ~cmd` (complemento confere em todos). TCL Android (RCA)
sem bytes de nav capturados → retorna 0 (não envia). Ainda assim, **validar na TV
real**: bases comunitárias podem ter variações por modelo/região.

> **Nota Flipper Zero:** o Flipper-IRDB (e o software do Flipper) é boa fonte de IR
> capturado/comunitário. Para extrair p/ `sendNEC`, lembrar do bit-reverse
> Flipper→wire em address E comando (vide conversão acima).

## Komeco no Brasil (AC)

Komeco **não é um protocolo** — é marca BR que terceiriza/OEM. Evidência de
mercado (lojistas de reposição BR, jun/2026): o mesmo controle compatível é
vendido cobrindo **Komeco / Midea / Comfee / Carrier / Springer**, com referência
**R06/BGCE** (e variantes `R05/BGE`, `LE-7424`). R06/BGCE é controle da **família
Midea** → por isso `AC Komeco (Midea)` herda de `MideaAC`.

- Fontes (jun/2026): Leroy Merlin, Cibrel (LE-7424), Frioshopping (R06/BGCE) —
  todas listam Komeco junto de Midea/Comfee no mesmo SKU de controle.
- Fórum webarcondicionado: relatos de que Komeco compartilha o **mesmo código de
  setup** de controle universal com **Electrolux e Midea**. Códigos universal
  citados: `160/107/256/489/669/1001` (índices de marca do *universal*, NÃO nome
  de protocolo — não usáveis direto na `IRremoteESP8266`).
- Controles universais (OFA): setup Komeco `0040/0046/0088` (hobby-hour.com).

**Ordem de teste (sourced):**
1. `AC Komeco (Midea)` — controle de reposição R06/BGCE é Midea.
2. `AC Electrolux (Electra)` — fórum aponta Komeco/Electrolux com mesmo setup.

**Não confirmado:** nenhuma fonte liga Komeco ao protocolo Coolix (palpite
descartado). pyhvac (frawau) **não** lista Komeco. Se nenhum dos dois responder,
o caminho é **capturar o controle real** com receptor IR (`IRrecvDumpV2`) e
identificar o protocolo — não chutar.

> Honestidade: o mapeamento Komeco→Midea tem base de mercado (controles de
> reposição), mas ainda **não foi confirmado neste hardware**.

> **Bit-order:** os hex de NEC são *wire-order*, passados direto no `sendNEC`
> (que envia MSB-first). NÃO usar `encodeNEC` (ela inverte bits). `0xEAC7` (JP1) =
> `0x57E3` no sendNEC — é o mesmo código bit-reversed.

## Como adicionar um aparelho

- **Marca Roku nova:** 3 linhas (classe + `name()`), herda tudo.
- **TV NEC nova (outra marca):** subclasse de `NecTV` com `name()` + `codeFor()`.
- **Marca que diverge num código:** `override codeFor()` só nela.
- **TCL nova:** adicionar como variante específica (Roku, Android/Google, etc.), não
  como `TclTV` genérico, para não misturar protocolos incompatíveis.

## Incertezas (testar no hardware — sem receptor IR)

- **TCL Android (RCA):** lib não tem RCA → montamos `sendRaw`. Portadora RCA
  clássica é ~56kHz; usamos 38kHz ("RCA-38"). Timings não confirmados.
- **AOC/SEMP Roku:** códigos inferidos como Roku; confirmar na TV real.
- **AOC clássico** (não-Roku) = NEC `0x00BD` (família separada, ainda não criada).

## Arquivos

- `ir_control.h` — hierarquia + registries TV/AC (header-only por enquanto).
- `ui_ir_remote.cpp` — UI separada por família, dropdown de aparelho e grade de comandos.
- `hal_interface.*` — `hw_set_remote_code` (sendNEC), `hw_ir_send_raw` (sendRaw),
  `hw_ac_electra_send`, `hw_ac_midea_send`, `hw_ac_lg_send`, `hw_ac_samsung_send`,
  `hw_ac_toshiba_send`.
- Diagrama UML + tabela completa de códigos: ver `docs/DECISIONS.md`.
