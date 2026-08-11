#!/usr/bin/env bash
# Compila UMA vez e re-tenta o upload até a porta aparecer.
# Serve quando o firmware atual boot-loopa (a /dev/ttyACM0 some/volta) e o
# Arduino IDE não pega a janela. Uso: ./flash.sh [porta]  (default /dev/ttyACM0)
#
# Dica: se ficar retrycando sem pegar, coloca o relógio em DOWNLOAD MODE
# (segura o BOOT e dá reset/religa) — aí a porta fica estável e o 1o upload passa.
set -u

SKETCH="/home/vitor/Arduino/factory"
BUILD="/tmp/twbuild"
FQBN="esp32:esp32:twatchs3"
LIB="$HOME/Arduino/libraries/WireGuard-ESP32"
PORT="${1:-/dev/ttyACM0}"

echo ">> compilando uma vez (build em $BUILD)..."
arduino-cli compile --fqbn "$FQBN" --library "$LIB" --build-path "$BUILD" "$SKETCH" || exit 1

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
