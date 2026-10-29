#!/usr/bin/env bash
# Upload rapido do firmware: reaproveita o binario da build anterior e so
# recompila quando alguma fonte do sketch ficou mais nova que o .bin.
#
# Uso: ./flash.sh [porta] [--build]
#   porta    default /dev/ttyACM0
#   --build  recompila mesmo com o binario atualizado (-b)
#
# Dica: se ficar retrycando sem pegar, coloca o relogio em DOWNLOAD MODE
# (segura o BOOT e da reset/religa) — ai a porta fica estavel e o 1o upload passa.
set -u

# A pasta deste script é o sketch: compila o código do repo, não a cópia em
# ~/Arduino/factory (que ficava parada e fazia o teste rodar código velho).
SKETCH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Build persistente (gitignored em .gitignore: "build/"): em /tmp ele sumia no
# reboot e obrigava a recompilar tudo de novo só pra enviar.
BUILD="$SKETCH/build"
BIN="$BUILD/factory.ino.bin"
FQBN="esp32:esp32:twatchs3"
LIB="$SKETCH/libraries/WireGuard-ESP32"      # vendorizada neste repo
PORT="/dev/ttyACM0"
FORCE_BUILD=0

for arg in "$@"; do
  case "$arg" in
    -b|--build) FORCE_BUILD=1 ;;
    -*) echo "opcao desconhecida: $arg (use --build ou uma porta)" >&2; exit 2 ;;
    *)  PORT="$arg" ;;
  esac
done

# Fonte mais nova do sketch. Ignora build/ e sim/ (o sim não entra no firmware);
# o symlink libraries/lvgl não é seguido, então não varre 2 mil arquivos.
newest_source_mtime()
{
  find "$SKETCH" -not -path "$SKETCH/build/*" -not -path "$SKETCH/sim/*" \
    \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.ino' -o -name '*.csv' \) \
    -printf '%T@\n' 2>/dev/null | sort -rn | head -1
}

needs_build()
{
  [ "$FORCE_BUILD" -eq 1 ] && return 0
  [ -s "$BIN" ] || return 0
  local src_mtime bin_mtime
  src_mtime="$(newest_source_mtime)"
  [ -z "$src_mtime" ] && return 1
  bin_mtime="$(stat -c '%Y' "$BIN")"
  awk -v s="$src_mtime" -v b="$bin_mtime" 'BEGIN { exit !(s + 0 > b + 0) }'
}

echo ">> sketch: $SKETCH"
if needs_build; then
  if [ "$FORCE_BUILD" -eq 1 ]; then
    echo ">> --build: recompilando (build em $BUILD)..."
  else
    echo ">> fonte mais nova que o binario: recompilando (build em $BUILD)..."
  fi
  # Sem o header o __has_include() faz o VPN compilar como no-op: avisa alto.
  if [ ! -f "$SKETCH/wireguard_secrets.h" ]; then
    echo "!! $SKETCH/wireguard_secrets.h ausente — o VPN vai compilar como no-op." >&2
  fi
  # --allow-multiple-definition: wifi_deauth_bypass.cpp redefine
  # ieee80211_raw_frame_sanity_check (o blob do WiFi recusa deauth sem isso).
  arduino-cli compile --fqbn "$FQBN" --library "$LIB" --build-path "$BUILD" \
    --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
    "$SKETCH" || exit 1
else
  echo ">> usando o binario da build anterior:"
  echo "   $BIN ($(stat -c '%y' "$BIN" | cut -c1-16), $(stat -c '%s' "$BIN") bytes)"
  echo "   (--build recompila)"
fi

echo ">> esperando a porta $PORT e enviando (Ctrl-C pra parar)..."
n=0
until arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH"; do
  n=$((n+1))
  echo "   retry #$n (porta ocupada/sumiu) — reseta o relogio segurando BOOT p/ download mode..."
  sleep 0.3
done
echo ">> upload OK! abrindo serial (Ctrl-C pra sair)..."

# Abre o Serial Monitor logo apos o upload. A porta re-enumera no reset, entao
# espera ela voltar. Passa --monitor no upload nao serve aqui (queremos control).
sleep 1
for i in $(seq 1 20); do [ -e "$PORT" ] && break; sleep 0.3; done
arduino-cli monitor -p "$PORT" -c baudrate=115200
