#!/usr/bin/env bash
# Firmware do leWatch: menu interativo pra compilar/enviar, ou direto por flags.
#
#   ./flash.sh                     menu (TTY)  |  build-se-preciso + envio (pipe)
#   ./flash.sh --build             recompila e envia
#   ./flash.sh --upload-only       so envia o binario que ja existe
#   ./flash.sh --only-compile      so compila
#   ./flash.sh --clean             recompila do zero (arduino-cli --clean)
#   ./flash.sh -m / --no-monitor   abre / nao abre o serial no fim
#   ./flash.sh /dev/ttyACM1        escolhe a porta
#
# Compila ESTA arvore (a pasta do script) — nao ~/Arduino/factory. O build fica
# em build/ (gitignored) e e reaproveitado quando nenhuma fonte mudou.
#
# Dica: se o upload ficar retrycando sem pegar, coloca o relogio em DOWNLOAD
# MODE (segura o BOOT e da reset/religa) — a porta fica estavel e o 1o passa.
set -u

SKETCH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SKETCH/build"
BIN="$BUILD/factory.ino.bin"
FQBN="esp32:esp32:twatchs3"
LIB="$SKETCH/libraries/WireGuard-ESP32"      # vendorizada neste repo
BAUD=115200

PORT="/dev/ttyACM0"
WANT_BUILD="auto"     # auto | force | never
WANT_UPLOAD=1
WANT_MONITOR="auto"   # auto | 1 | 0
WANT_CLEAN=0
FORCE_MENU=0

usage()
{
  cat <<'USAGE'
Firmware do leWatch: menu interativo pra compilar/enviar, ou direto por flags.

  ./flash.sh                     menu (TTY)  |  build-se-preciso + envio (pipe)
  ./flash.sh --build             recompila e envia
  ./flash.sh --upload-only       so envia o binario que ja existe
  ./flash.sh --only-compile      so compila
  ./flash.sh --clean             recompila do zero (arduino-cli --clean)
  ./flash.sh -m / --no-monitor   abre / nao abre o serial no fim
  ./flash.sh /dev/ttyACM1        escolhe a porta

Compila ESTA arvore (a pasta do script) — nao ~/Arduino/factory. O build fica
em build/ (gitignored) e e reaproveitado quando nenhuma fonte mudou.
USAGE
}

warn()
{
  printf '!! %s\n' "$*" >&2
}

die()
{
  warn "$*"
  exit 1
}

# ---- estado ---------------------------------------------------------------

# Fonte mais nova do sketch. Ignora build/ e sim/ (o sim nao entra no firmware);
# o symlink libraries/lvgl nao e seguido, entao nao varre 2 mil arquivos.
newest_source_mtime()
{
  find "$SKETCH" -not -path "$SKETCH/build/*" -not -path "$SKETCH/sim/*" \
    \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.ino' -o -name '*.csv' \) \
    -printf '%T@\n' 2>/dev/null | sort -rn | head -1
}

# 0 = o binario esta desatualizado (ou nem existe).
is_stale()
{
  [ -s "$BIN" ] || return 0
  local src_mtime bin_mtime
  src_mtime="$(newest_source_mtime)"
  [ -z "$src_mtime" ] && return 1
  bin_mtime="$(stat -c '%Y' "$BIN")"
  awk -v s="$src_mtime" -v b="$bin_mtime" 'BEGIN { exit !(s + 0 > b + 0) }'
}

# Politica do usuario: --build/--clean forcam, --upload-only nunca compila.
should_build()
{
  case "$WANT_BUILD" in
    force) return 0 ;;
    never) return 1 ;;
  esac
  is_stale
}

bin_state()
{
  if [ ! -s "$BIN" ]; then
    printf 'ausente'
    return
  fi
  if is_stale; then
    printf 'DESATUALIZADO (%s, %s bytes) — vai recompilar' \
      "$(stat -c '%y' "$BIN" | cut -c1-16)" "$(stat -c '%s' "$BIN")"
  else
    printf 'atualizado (%s, %s bytes)' \
      "$(stat -c '%y' "$BIN" | cut -c1-16)" "$(stat -c '%s' "$BIN")"
  fi
}

print_header()
{
  printf '\n== leWatch firmware ==\n'
  printf '   sketch : %s\n' "$SKETCH"
  if [ -e "$PORT" ]; then
    printf '   porta  : %s\n' "$PORT"
  else
    printf '   porta  : %s (nao existe agora)\n' "$PORT"
  fi
  printf '   binario: %s\n' "$(bin_state)"
}

# ---- acoes ----------------------------------------------------------------

do_build()
{
  local extra=()
  if [ "$WANT_CLEAN" -eq 1 ]; then
    extra=(--clean)
    printf '>> compilando do zero em %s...\n' "$BUILD"
  else
    printf '>> compilando em %s...\n' "$BUILD"
  fi
  # Sem o header o __has_include() faz o VPN compilar como no-op: avisa alto.
  [ -f "$SKETCH/wireguard_secrets.h" ] ||
    warn "$SKETCH/wireguard_secrets.h ausente — o VPN vai compilar como no-op."
  # --allow-multiple-definition: wifi_deauth_bypass.cpp redefine
  # ieee80211_raw_frame_sanity_check (o blob do WiFi recusa deauth sem isso).
  arduino-cli compile --fqbn "$FQBN" --library "$LIB" --build-path "$BUILD" \
    --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
    "${extra[@]}" "$SKETCH"
}

do_upload()
{
  if [ ! -s "$BIN" ]; then
    warn "sem binario em $BIN — compile antes (--build)"
    return 1
  fi
  [ -e "$PORT" ] || warn "porta $PORT nao existe agora; o loop abaixo espera por ela"
  printf '>> enviando pra %s (Ctrl-C pra parar)...\n' "$PORT"
  local tries=0
  until arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH"; do
    tries=$((tries + 1))
    printf '   retry #%d — segura BOOT + reset p/ download mode...\n' "$tries"
    sleep 0.3
  done
  printf '>> upload OK\n'
}

do_monitor()
{
  printf '>> abrindo serial em %s a %d baud (Ctrl-C pra sair)...\n' "$PORT" "$BAUD"
  sleep 1
  local i
  for i in $(seq 1 20); do [ -e "$PORT" ] && break; sleep 0.3; done
  arduino-cli monitor -p "$PORT" -c baudrate="$BAUD"
}

want_monitor()
{
  case "$WANT_MONITOR" in
    1) return 0 ;;
    0) return 1 ;;
  esac
  [ -t 1 ]
}

run_actions()
{
  if should_build || [ "$WANT_UPLOAD" -eq 0 ]; then
    do_build || return 1
  else
    printf '>> reaproveitando o binario: %s\n' "$(bin_state)"
    printf '   (--build recompila)\n'
  fi
  [ "$WANT_UPLOAD" -eq 1 ] || return 0
  do_upload || return 1
  if want_monitor; then
    do_monitor
  fi
  return 0
}

# ---- menu -----------------------------------------------------------------

choose_port()
{
  local candidates=() choice entry count
  for entry in /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$entry" ] && candidates+=("$entry")
  done
  count=${#candidates[@]}

  printf '\nPortas seriais:\n'
  if [ "$count" -eq 0 ]; then
    printf '   (nenhuma encontrada — relogio plugado?)\n'
  else
    local i=1
    for entry in "${candidates[@]}"; do
      if [ "$entry" = "$PORT" ]; then
        printf '   %d) %s  <- atual\n' "$i" "$entry"
      else
        printf '   %d) %s\n' "$i" "$entry"
      fi
      i=$((i + 1))
    done
  fi
  printf '   m) digitar manualmente (atual: %s)\n' "$PORT"
  printf '   v) voltar\n'

  read -r -p 'porta> ' choice || return 0
  case "${choice:-}" in
    ''|v|V) return 0 ;;
    m|M)
      read -r -p 'caminho> ' entry || return 0
      [ -n "${entry:-}" ] && PORT="$entry"
      ;;
    *[!0-9]*)
      warn "opcao invalida: '${choice}' (esperado numero, m ou v)"
      return 0
      ;;
    *)
      if [ "$count" -eq 0 ]; then
        warn "nenhuma porta listada — use m pra digitar o caminho"
      elif [ "$choice" -ge 1 ] && [ "$choice" -le "$count" ]; then
        PORT="${candidates[$((choice - 1))]}"
      else
        warn "fora do intervalo: '$choice' (1..$count)"
      fi
      ;;
  esac
  printf '>> porta: %s\n' "$PORT"
}

run_menu()
{
  local choice
  while true; do
    print_header
    printf '\n  1) Compilar (se preciso) e enviar + serial\n'
    printf '  2) Enviar o binario atual + serial\n'
    printf '  3) So compilar\n'
    printf '  4) Compilar (se preciso) e enviar, sem serial\n'
    printf '  5) Compilar do zero e enviar + serial\n'
    printf '  6) Escolher porta\n'
    printf '  q) Sair\n'
    read -r -p 'opcao> ' choice || return 0

    case "${choice:-}" in
      1) WANT_BUILD="auto";  WANT_UPLOAD=1; WANT_MONITOR=1; WANT_CLEAN=0
         run_actions && return 0 ;;
      2) WANT_BUILD="never"; WANT_UPLOAD=1; WANT_MONITOR=1; WANT_CLEAN=0
         run_actions && return 0 ;;
      3) WANT_BUILD="auto";  WANT_UPLOAD=0; WANT_CLEAN=0
         run_actions ;;
      4) WANT_BUILD="auto";  WANT_UPLOAD=1; WANT_MONITOR=0; WANT_CLEAN=0
         run_actions && return 0 ;;
      5) WANT_BUILD="force"; WANT_UPLOAD=1; WANT_MONITOR=1; WANT_CLEAN=1
         run_actions && return 0 ;;
      6) choose_port ;;
      q|Q) return 0 ;;
      *) warn "opcao invalida: '${choice}' (esperado 1-6 ou q)" ;;
    esac
  done
}

# ---- entrada --------------------------------------------------------------

parse_args()
{
  while [ "$#" -gt 0 ]; do
    case "$1" in
      -h|--help)    usage; exit 0 ;;
      -i|--menu)    FORCE_MENU=1 ;;
      -b|--build)   WANT_BUILD="force" ;;
      -u|--upload-only) WANT_BUILD="never" ;;
      -c|--clean)   WANT_BUILD="force"; WANT_CLEAN=1 ;;
      -o|--only-compile) WANT_UPLOAD=0 ;;
      -m|--monitor) WANT_MONITOR=1 ;;
      -n|--no-monitor) WANT_MONITOR=0 ;;
      -*)           usage >&2; die "opcao desconhecida: '$1'" ;;
      *)            PORT="$1" ;;
    esac
    shift
  done
}

main()
{
  parse_args "$@"
  if [ "$FORCE_MENU" -eq 1 ] || { [ "$#" -eq 0 ] && [ -t 0 ] && [ -t 1 ]; }; then
    run_menu
    return 0
  fi
  run_actions
}

main "$@"
